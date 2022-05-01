/*
	license..: GPL-2.0
	copyright: beldzhang@gmail.com
	reference: https://www.kernel.org/doc/html/latest/process/license-rules.html
*/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/blk-mq.h>
#include <net/tcp.h>
#include <linux/hdreg.h>
#include <linux/reboot.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/sched/mm.h>
#include <linux/math64.h>

#define SBOI_NAME     "sboi"
#define SBOI_DESC     "simple blockdev over ip"
#define SBOI_VERSION  "0.02.0001"

#define SOCKET_RECV 0
#define SOCKET_SEND 1

#define SBOI_INFO  SBOI_NAME " info: "
#define SBOI_WARN  SBOI_NAME " warn: "
#define SBOI_ERROR SBOI_NAME " error: "


#pragma pack(push, 1)

struct sboi_pdu_head {
	u32 magic;
	u8  cmmd;
	u8  index;
	u16 rsv1;
	u64 offset;
	int length;
	u32 rsv2[3];
};

struct sboi_pdu_disk {
	u16 type;
	u16 flag;
	u32 rsvd;
	u64 size;
};

#pragma pack(pop)


#define SBOI_MAGIC      0x694f6273  // 'sbOi'

#define SBOI_MAX        16

#define SBOI_CMD_READ   0x01
#define SBOI_CMD_WRITE  0x02
#define SBOI_CMD_OPEN   0x03
#define SBOI_CMD_CLOSE  0x04

#define SBOI_RSP_READ   0x11
#define SBOI_RSP_WRITE  0x12
#define SBOI_RSP_OPEN   0x13
#define SBOI_RSP_CLOSE  0x14


struct sboi_disk_item {
	int index;
	int minor;
	u16 type;
	u16 flag;
	u64 size;

	struct blk_mq_tag_set  tag_set;
	struct gendisk        *gen_disk;
};

struct tagset_cmddata {
	int disk_index;
};

static char *addr;  // server address string
static u16   port;  // server port
static char *open;  // open string

static u32            sboi_server; // server address
static struct socket *sboi_skt;
static struct mutex   skt_mutex;
static int sboi_major;

static int                   __disk_count = 0;
static struct sboi_disk_item __disks[SBOI_MAX];


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

	struct sboi_pdu_head head_cmd, head_rsp;

	memset(&head_cmd, 0, sizeof(head_cmd));
	memset(&head_rsp, 0, sizeof(head_rsp));

	head_cmd.magic = SBOI_MAGIC;
	head_cmd.cmmd = rq_data_dir(req) == WRITE ? SBOI_CMD_WRITE : SBOI_CMD_READ;
	head_cmd.index = ((struct tagset_cmddata *)blk_mq_rq_to_pdu(req))->disk_index;
	head_cmd.offset = blk_rq_pos(req) * 512;
	head_cmd.length = blk_rq_sectors(req) * 512;

	if (sboi_sock_xmit(sboi_skt, SOCKET_SEND, &head_cmd, sizeof(head_cmd), 0) != sizeof(head_cmd)) {
		return -EPIPE;
	}

	if (head_cmd.cmmd == SBOI_CMD_READ) {
		if (sboi_sock_xmit(sboi_skt, SOCKET_RECV, &head_rsp, sizeof(head_rsp), 0) != sizeof(head_rsp)) {
			return -EBADF;
		}
		if (head_rsp.magic != SBOI_MAGIC || head_rsp.cmmd != SBOI_RSP_READ) {
			return -EINVAL;
		}
		if (head_rsp.index != head_cmd.index ||
		    head_rsp.offset != head_cmd.offset ||
		    head_rsp.length != head_cmd.length) {
			return -EIO;
		}
	}

	rq_for_each_segment(bvecrec, req, iter)
	{
		bvec = &bvecrec;
		buffer = kmap(bvec->bv_page);
		if (head_cmd.cmmd == SBOI_CMD_WRITE)
			flags = (iter.iter.bi_idx < (iter.bio->bi_vcnt - 1) || iter.bio->bi_next) ? MSG_MORE : 0;
		else
			flags = MSG_WAITALL;
		size = bvec->bv_len;
		rc = sboi_sock_xmit(sboi_skt, head_cmd.cmmd == SBOI_CMD_WRITE ? SOCKET_SEND : SOCKET_RECV, buffer + bvec->bv_offset, size, flags);
		kunmap(bvec->bv_page);
		if (rc <= 0)
		{
			return rc;
		}
		nsect += size;
	}

	if (head_cmd.cmmd == SBOI_CMD_WRITE) {
		if (sboi_sock_xmit(sboi_skt, SOCKET_RECV, &head_rsp, sizeof(head_rsp), 0) != sizeof(head_rsp)) {
			return -EPIPE;
		}
		if (head_rsp.magic != SBOI_MAGIC || head_rsp.cmmd != SBOI_RSP_WRITE) {
			return -EINVAL;
		}
		if (head_rsp.index != head_cmd.index ||
		    head_rsp.offset != head_cmd.offset ||
		    head_rsp.length != head_cmd.length) {
				return -EIO;
		}
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
	int rc, i;
	struct socket *tmp_sock = NULL;

	struct sboi_pdu_head open_cmd, open_rsp;
	struct sboi_pdu_disk disk_info[SBOI_MAX];

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

	memset(&open_cmd, 0, sizeof(open_cmd));
	memset(&open_rsp, 0, sizeof(open_rsp));

	open_cmd.magic = SBOI_MAGIC;
	open_cmd.cmmd  = SBOI_CMD_OPEN;
	open_cmd.length = open ? strlen(open) : 0;
	rc = sboi_sock_xmit(tmp_sock, SOCKET_SEND, &open_cmd, sizeof(open_cmd), 0);
	if (rc != sizeof(open_cmd)) {
		conn_err(3, rc);
		goto error;
	}
	if (open) {
		rc = sboi_sock_xmit(tmp_sock, SOCKET_SEND, open, strlen(open), 0);
		if (rc != open_cmd.length) {
			conn_err(10, rc);
			goto error;
		}
	}

	rc = sboi_sock_xmit(tmp_sock, SOCKET_RECV, &open_rsp, sizeof(open_rsp), 0);
	if ( rc != sizeof(open_rsp)) {
		conn_err(4, rc);
		goto error;
	}

	if (open_rsp.magic != SBOI_MAGIC || open_rsp.cmmd != SBOI_RSP_OPEN) {
	}
	if (open_rsp.index > SBOI_MAX ||
	    open_rsp.length < sizeof(struct sboi_pdu_disk) * open_rsp.index ||
	    open_rsp.length > sizeof(struct sboi_pdu_disk) * SBOI_MAX) {
	}

	rc = sboi_sock_xmit(tmp_sock, SOCKET_RECV, disk_info, open_rsp.length, 0);
	if (rc != open_rsp.length) {
		conn_err(5, rc);
		goto error;
	}

	__disk_count = open_rsp.index;
	for (i = 0; i < __disk_count; i++) {
		__disks[i].index = i;
		__disks[i].type = disk_info[i].type;
		__disks[i].flag = disk_info[i].flag;
		__disks[i].size = disk_info[i].size;
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
	mutex_lock(&skt_mutex);
	sboi_xfer_request(req);
	mutex_unlock(&skt_mutex);
	spin_lock_irqsave(&req->q->queue_lock, flags);
	blk_mq_end_request(req, 0);
	spin_unlock_irqrestore(&req->q->queue_lock, flags);

	return BLK_STS_OK;
}

static int sboi_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
	struct sboi_disk_item *item = bdev->bd_disk->private_data;
	struct hd_geometry geo;

	switch (cmd)
	{
		case HDIO_GETGEO:
			geo.sectors = 63;
			geo.heads = 255;
			geo.cylinders = div64_u64(item->size, 63 * 255);
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
	int rc, i;

	printk(SBOI_INFO "connect to %s:%d\n", addr, port);
	rc = sboi_connect();
	if (rc != 0)
		return rc;

	for(i = 0; i < __disk_count; i++) {
		printk("  %s%d: type=0x%4.4x(0x%4.4x), size=%lldMB\n",
		       SBOI_NAME,
		       i,
		       __disks[i].type,
		       __disks[i].flag,
		       __disks[i].size >> 20);
	}

	return 0;
}

static void sboi_close_net(void)
{
	struct sboi_pdu_head cmd;

	if (sboi_skt == NULL)
		return;

	memset(&cmd, 0, sizeof(cmd));
	cmd.magic = SBOI_MAGIC;
	cmd.cmmd = SBOI_CMD_CLOSE;
	if (sboi_sock_xmit(sboi_skt, SOCKET_SEND, &cmd, sizeof(cmd), 0) != sizeof(cmd)) {
	}
	if (sboi_sock_xmit(sboi_skt, SOCKET_RECV, &cmd, sizeof(cmd), 0) != sizeof(cmd)) {
	}

	free_sock();
}

static int sboi_notify_reboot(struct notifier_block *this, unsigned long code, void *x)
{
	if ((code == SYS_DOWN) || (code == SYS_HALT) || (code == SYS_POWER_OFF))
	{
		sboi_close_net();
	}
	return NOTIFY_OK;
}

static struct block_device_operations sboi_fops =
{
	.owner = THIS_MODULE,
	.ioctl = sboi_ioctl,
};

static struct notifier_block sboi_reboot_notifier =
{
	.notifier_call = sboi_notify_reboot,
	.next          = NULL,
	.priority      = 0,
};

static int sboi_init_request(struct blk_mq_tag_set *set, struct request *rq,
			    unsigned int hctx_idx, unsigned int numa_node)
{
	struct tagset_cmddata *data = blk_mq_rq_to_pdu(rq);
	struct sboi_disk_item *item = set->driver_data;

	data->disk_index = item->index;
	return 0;
}

static const struct blk_mq_ops sboi_mq_ops = {
	.queue_rq     = do_sboi_rq,
	.init_request = sboi_init_request
};


int alloc_tag_set(int _index)
{
	struct blk_mq_tag_set *set = &__disks[_index].tag_set;

	memset(set, 0, sizeof(*set));
	set->ops = &sboi_mq_ops;
	set->nr_hw_queues = 1;
	set->queue_depth = 128;
	set->numa_node = NUMA_NO_NODE;
	set->cmd_size = sizeof(struct tagset_cmddata);
	set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING;
	set->driver_data = &__disks[_index];

	return blk_mq_alloc_tag_set(set);
}

static int __init sboi_init(void)
{
	int rc, i;
	struct sboi_disk_item *item;
	struct gendisk * disk;

	BUILD_BUG_ON(sizeof(struct sboi_pdu_head) != 32);
	BUILD_BUG_ON(sizeof(struct sboi_pdu_disk) != 16);

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

	memset(__disks, 0, sizeof(__disks));
	rc = sboi_open_net();
	if (rc < 0)
	{
		unregister_blkdev(sboi_major, SBOI_NAME);
		return rc;
	}

	mutex_init(&skt_mutex);
	for(i = 0; i < __disk_count; i++) {
		item = &__disks[i];

		#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0))
		disk = alloc_disk(1);
		if (!disk)
			printk(SBOI_WARN "alloc_disk() fail.\n");

		rc = alloc_tag_set(i);
		if (rc) {
			printk(SBOI_ERROR "alloc_tag() failed.\n");
			return -ENOMEM;
		}
		disk->queue = blk_mq_init_queue(&item->tag_set);
		#else
		rc = alloc_tag_set(i);
		if (rc) {
			printk(SBOI_ERROR "alloc_tag() failed.\n");
			return -ENOMEM;
		}
		disk = blk_mq_alloc_disk(&item->tag_set, NULL);
		if (IS_ERR(disk)) {
			printk(SBOI_ERROR "alloc_disk() failed.\n");
			return -ENOMEM;
		}
		#endif

		item->gen_disk = disk;
		item->minor = i;
		blk_queue_flag_set(QUEUE_FLAG_NONROT, disk->queue);
		blk_queue_max_hw_sectors(disk->queue, 32768);
		disk->queue->limits.max_sectors = 512;
		disk->major = sboi_major;
		disk->first_minor = item->minor;
		disk->minors = 1;
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0))
		disk->flags |= GENHD_FL_NO_PART;
		#else
		disk->flags |= GENHD_FL_NO_PART_SCAN;
		#endif
		disk->fops = &sboi_fops;
		disk->private_data = item;
		sprintf(disk->disk_name, SBOI_NAME "%d", item->minor);
		set_capacity(disk, item->size >> 9);

		#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
		add_disk(disk);
		#else
		if (add_disk(disk)) {
		};
		#endif
	}
	register_reboot_notifier(&sboi_reboot_notifier);
	return 0;
}

static void __exit sboi_cleanup(void)
{
	int i;
	struct sboi_disk_item *item;

	for (i = 0; i < __disk_count; i++) {
		item = &__disks[i];
		del_gendisk(item->gen_disk);
		blk_cleanup_queue(item->gen_disk->queue);
		blk_mq_free_tag_set(&item->tag_set);
		put_disk(item->gen_disk);
	}

	sboi_close_net();
	unregister_reboot_notifier(&sboi_reboot_notifier);
	unregister_blkdev(sboi_major, SBOI_NAME);
	printk(SBOI_INFO "unloaded.\n");
}


module_init(sboi_init);
module_exit(sboi_cleanup);

module_param(addr, charp,  0400);
module_param(port, ushort, 0400);
module_param(open, charp,  0400);

MODULE_DESCRIPTION(SBOI_DESC " " SBOI_VERSION);
MODULE_LICENSE("GPL");
