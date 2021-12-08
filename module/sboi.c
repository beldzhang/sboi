/*
	license..: GPL-2.0
	copyright: beldzhang@gmail.com
	reference: https://www.kernel.org/doc/html/latest/process/license-rules.html
*/

#include <linux/module.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/blk-mq.h>
#include <net/tcp.h>
#include <linux/hdreg.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/sched/mm.h>

#define SBOI_NAME		"sboi"
#define SBOI_DESC 		"simple blockdev over ip"
#define SBOI_VERSION 	"0.01.0001"

#define SOCKET_RECV 0
#define SOCKET_SEND 1

#define SBOI_INFO  SBOI_NAME " info: "
#define SBOI_WARN  SBOI_NAME " warn: "
#define SBOI_ERROR SBOI_NAME " error: "

char * addr;  // server address string
u16 port;      // server port

u32 sboi_server;    // server address
struct socket *sboi_skt;
struct gendisk *sboi_disk;
struct blk_mq_tag_set tag_set;
int sboi_major;
u32 disk_size;

static int sboi_sock_xmit(struct socket *sock, int send, void *buf, int size, int msg_flags)
{
	int rc, result = 0;
	struct msghdr msg;
	struct kvec iov;
	unsigned int noreclaim_flag;
	if(!sock)
	{
		return -EINVAL;
	}

	noreclaim_flag = memalloc_noreclaim_save();
	while (size > 0)
	{
		sock->sk->sk_allocation = GFP_NOIO;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;
		if (send)
			rc = kernel_sendmsg(sock, &msg, &iov, 1, size);
		else
			rc = kernel_recvmsg(sock, &msg, &iov, 1, size, 0);
		if (rc <= 0)
		{
			if (rc == 0)
				result = -EPIPE;
			else
				result = rc;
			break;
		}
		size -= rc;
		buf += rc;
		result += rc;
	}

	memalloc_noreclaim_restore(noreclaim_flag);

	return result;
}

static void free_sock(void)
{
	if (sboi_skt)
	{
		struct socket *tmp_sock = sboi_skt;
		kernel_sock_shutdown(tmp_sock, SHUT_RDWR);
		sboi_skt = NULL;
		sock_release(tmp_sock);
	}
}

static int sboi_xfer_request(struct request *req)
{
	struct bio_vec *bvec;
	struct bio_vec bvecrec;
	int nsect = 0;
	char *buffer;
	int size, rc, flags;
	struct req_iterator iter;

	u8  wri = rq_data_dir(req) == WRITE ? 1 : 0;
	u32 pos = blk_rq_pos(req);
	u16 len = blk_rq_sectors(req);

	sboi_sock_xmit(sboi_skt, SOCKET_SEND, &wri, 1, 0);
	sboi_sock_xmit(sboi_skt, SOCKET_SEND, &pos, 4, 0);
	sboi_sock_xmit(sboi_skt, SOCKET_SEND, &len, 2, 0);

	rq_for_each_segment(bvecrec, req, iter)
	{
		bvec = &bvecrec;
		buffer = kmap(bvec->bv_page);
		if (wri)
			flags = (iter.iter.bi_idx < (iter.bio->bi_vcnt - 1) || iter.bio->bi_next) ? MSG_MORE : 0;
		else
			flags = MSG_WAITALL;
		size = bvec->bv_len;
		rc = sboi_sock_xmit(sboi_skt, wri ? SOCKET_SEND : SOCKET_RECV, buffer + bvec->bv_offset, size, flags);
		kunmap(bvec->bv_page);
		if (rc <= 0)
		{
			return rc;
		}
		nsect += size;
	}

	return nsect;
}

static void conn_err(int step, int err)
{
	printk(SBOI_ERROR "conn fail <%d, %d>\n", step, err);
}

static int sboi_connect(void)
{
	struct sockaddr_in addr;
	int rc;
	struct socket *tmp_sock = NULL;

	rc = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &tmp_sock);
	if (rc < 0)
	{
		conn_err(1, rc);
		return rc;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = sboi_server;
	rc = kernel_connect(tmp_sock, (struct sockaddr *)&addr, sizeof(addr), 0);
	if (rc < 0)
	{
		conn_err(2, rc);
		goto error2;
	}
	tcp_sock_set_nodelay(tmp_sock->sk);

	rc = sboi_sock_xmit(tmp_sock, SOCKET_RECV, &disk_size, 4, 0);
	if (rc != 4)
	{
		conn_err(3, rc);
		goto error;
	}

	sboi_skt = tmp_sock;
	printk(SBOI_INFO "connected.\n");
	return 0;

error:
	kernel_sock_shutdown(tmp_sock, SHUT_RDWR);

error2:
	if (tmp_sock)
	{
		sock_release(tmp_sock);
	}
	return rc;
}


static blk_status_t do_sboi_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	unsigned long flags;

	blk_mq_start_request(req);
	sboi_xfer_request(req);
	spin_lock_irqsave(&req->q->queue_lock, flags);
	blk_mq_end_request(req, 0);
	spin_unlock_irqrestore(&req->q->queue_lock, flags);

	return BLK_STS_OK;
}

static int sboi_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
	struct hd_geometry geo;

	switch (cmd)
	{
		case HDIO_GETGEO:
			geo.sectors = 63;
			geo.heads = 255;
			geo.cylinders = disk_size / 63 / 255;
			geo.start = 0;
			if (copy_to_user((void __user *)arg, &geo, sizeof(geo)))
				return -EFAULT;
			return 0;

		default:
			break;
	}

	return -ENOTTY;
}

static int sboi_open_net(void)
{
	int rc;

	printk(SBOI_INFO "connect to %s:%d\n", addr, port);
	rc = sboi_connect();
	if (rc != 0)
		return rc;

	printk("  /dev/%s: size=%lldMB\n", SBOI_NAME, (u64)disk_size * 512 / 1048576);

	return 0;
}

static void sboi_close_net(void)
{
	if (sboi_skt == NULL)
		return;

	free_sock();
}

static struct block_device_operations sboi_fops =
{
	.owner = THIS_MODULE,
	.ioctl = sboi_ioctl,
};

static const struct blk_mq_ops sboi_mq_ops = {
	.queue_rq = do_sboi_rq
};

static int __init sboi_init(void)
{
 	int rc;

	printk("%s %s\n", SBOI_DESC, SBOI_VERSION);

	if (!addr)
		return -EFAULT;

	if (!port)
		return -EINVAL;

	sboi_server = in_aton(addr);

	if ((sboi_major = register_blkdev(0, SBOI_NAME)) < 0)
	{
		return -EIO;
	}

	rc = sboi_open_net();
	if (rc < 0)
	{
		unregister_blkdev(sboi_major, SBOI_NAME);
		return rc;
	}

	sboi_disk = alloc_disk(1);
	if (!sboi_disk)
		printk(SBOI_WARN "alloc_disk() fail.\n");
	sboi_disk->queue = blk_mq_init_sq_queue(&tag_set,
	                                   &sboi_mq_ops,
	                                   128,
	                                   BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING);

	blk_queue_flag_set(QUEUE_FLAG_NONROT, sboi_disk->queue);
	blk_queue_max_hw_sectors(sboi_disk->queue, 32768);
	sboi_disk->queue->limits.max_sectors = 512;
	sboi_disk->major = sboi_major;
	sboi_disk->first_minor = 0;
	sboi_disk->fops = &sboi_fops;
	sprintf(sboi_disk->disk_name, SBOI_NAME);
	set_capacity(sboi_disk, disk_size);

	add_disk(sboi_disk);

	return 0;
}

static void __exit sboi_cleanup(void)
{
	if (sboi_disk)
	{
		del_gendisk(sboi_disk);
		blk_cleanup_queue(sboi_disk->queue);
		blk_mq_free_tag_set(&tag_set);
		put_disk(sboi_disk);
	}

	sboi_close_net();
	unregister_blkdev(sboi_major, SBOI_NAME);
	printk(SBOI_INFO "unloaded.\n");
}


module_init(sboi_init);
module_exit(sboi_cleanup);

module_param(addr, charp,  0444);
module_param(port, ushort, 0444);

MODULE_DESCRIPTION(SBOI_DESC " " SBOI_VERSION);
MODULE_LICENSE("GPL");
