### simple blockdev over ip

__1) build and run server__

    [#] cd server
    [#] make
    [#] dd if=/dev/zero of=/tmp/rootfs.img bs=1G count=8
    [#] ./server

__2) build and install module__

    [#] cd module && make modules install

__3) connect, mkfs, mount, copy root-fs, disconnect__

    [#] modprobe sboi addr=<server-ip> port=12345
    [#] mkfs.ext4 /dev/sboi
    [#] mkdir -p /tmp/remote
    [#] mount /dev/sboi0 /tmp/remote  # _changed to /dev/sboi0 from /devsboi_
    [#] rsync -avx / /tmp/remote
    [#] vim /tmp/remote/etc/fstab  # _change mount point(/) to "/dev/sboi" in /etc/fstab_
    [#] rmmod sboi

__4) build initramfs__

  4.1) download [__busybox-armv7l__](https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv7l),
       copy to `initrd/tree/bin/busybox`

  4.2) edit param in initrd/tree/init, use `static ip` or `dhcp`

  4.3) build

    [#] cd initrd && make

__5) boot from sd card, rootfs on sboi__

  5.1) copy initrd/initrd.xz to /boot

  5.2) edit /boot/config.txt, add

    initramfs initrd.xz followkernel

  5.3) reboot

__6) prepare dhcp, dnsmasq, tftp, rpi pxeboot__

  put all boot files to server.
  excellent guides:
  - [Raspberry Pi PXE Boot – Network booting a Pi 4 without an SD card](https://linuxhit.com/raspberry-pi-pxe-boot-netbooting-a-pi-4-without-an-sd-card/)
  - [Diskless Boot for a Raspberry Pi over PXE and iSCSI](https://tech.xlab.si/blog/pxe-boot-raspberry-pi-iscsi/)

__7) remove sd card, boot rpi__
