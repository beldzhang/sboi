
### Diskless Raspberry Pi over NBD/iSCSI

__1) prepare NBD server or iSCSI target__
    _reference to nbd-server or and target docs_

__2) connect, mkfs, mount, copy root-fs, disconnect__

  NBD)
    [#] modprobe nbd
    [#] nbd-client <server-ip> /dev/nbd0 -N export-name
    [#] mkfs.ext4 /dev/nbd

  iSCSI)
    [#] _reference to open-iscsi doc_
    [#] mkfs.ext4 /dev/sda  # _not partitioned_

  uploading)
    [#] mkdir -p /tmp/remote
    [#] mount /dev/nbd0 /tmp/remote  # _use /dev/sda for iSCSI_
    [#] rsync -avx / /tmp/remote
    [#] vim /tmp/remote/etc/fstab  # _change mount point(/) to "/dev/nbd0" or "/dev/sda" in /etc/fstab_

__3) build initramfs__

  4.1) download [__busybox-armv7l__](https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv7l),
       copy to `initrd-nbd/tree/bin/busybox`

  4.2) copy binaries required for nbd-client/iscsistart
       file listed in list-$arch.txt

  4.3) edit param in initrd-nbd/tree/init, use `static ip` or `dhcp`

  4.4) build

    [#] cd initrd-nbd && make

__4) boot from sd card, rootfs on NBD/iSCSI__

  5.1) copy initrd/initrd.xz to /boot

  5.2) edit /boot/config.txt, add

    initramfs initrd.xz followkernel

  5.3) reboot

__5) prepare dhcp, dnsmasq, tftp, rpi pxeboot__

  put all boot files to server.
  excellent guides:
  - [Raspberry Pi PXE Boot – Network booting a Pi 4 without an SD card](https://linuxhit.com/raspberry-pi-pxe-boot-netbooting-a-pi-4-without-an-sd-card/)
  - [Diskless Boot for a Raspberry Pi over PXE and iSCSI](https://tech.xlab.si/blog/pxe-boot-raspberry-pi-iscsi/)

__6) remove sd card, boot rpi__
