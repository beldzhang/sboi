### simple blockdev over ip

__1) build and install module__

    [#] cd module && make modules install

__2) build and run server__

    [#] cd server
    [#] make
    [#] dd if=/dev/zero of=/tmp/rootfs.img bs=1G count=8
    [#] ./server

__3) connect, mkfs, mount__

    [#] modprobe sboi addr=<server-ip> port=12345
    [#] mkfs.ext4 /dev/sboi
    [#] mkdir -p /tmp/remote
    [#] mount /dev/sboi /tmp/remote

__4) copy root fs and fix, disconnect__

    [#] rsync -a -P -x / /tmp/remote
    [#] vim /tmp/remote/etc/fstab
    [#] # change device of mount point(/) to "/dev/sboi"
    [#] rmmod sboi

__5) build initramfs__

    5.1 add sboi.ko to initramfs build tree
    5.2 edit /init, change {mount nfsroot} to
        modprobe sboi addr=<server-ip> port=12345
        mount /dev/sboi /real/root/path
    5.3 build initramfs

__6) prepare dhcp, dnsmasq, tftp, rpi pxeboot__

  excellent guides:
  - [Raspberry Pi PXE Boot – Network booting a Pi 4 without an SD card](https://linuxhit.com/raspberry-pi-pxe-boot-netbooting-a-pi-4-without-an-sd-card/)
  - [Diskless Boot for a Raspberry Pi over PXE and iSCSI](https://tech.xlab.si/blog/pxe-boot-raspberry-pi-iscsi/)

__7) remove sd card, boot rpi__
