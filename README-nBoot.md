

### prepare disk image on server

  _create or open a disk image named with <disk-name>_


### upload bootpkg

  1) build initramfs in initrd/ dir

   1.1) rpi os 32/64

    `[~]# make clean rpios bootpkg`

   1.2) archlinux based

    `[~]# make clean arch bootpkg`

   1.3) ubuntu based

    `[~]# make clean ubuntu bootpkg`

  2) in manager, upload it to <disk-name>


### upload system

#### manually: via sboi

  1) install sboi module

  2) connect to server

    `[~]# modprobe sboi addr=<server-ip> port=6821 open=<disk-name>:<password>`

  3) mkfs, mount, uploading

    `[~]# mkfs.ext4 /dev/sboi0`
    `[~]# mount /dev/sboi0 /tmp/remote`
    `[~]# rsync -avx --delete / /tmp/remote`

  4) fix fstab entries

     __remove all local mount point__

  5) disconn

    `[~]# umount /tmp/remote`
    `[~]# rmmod sboi`

#### manager:

  _upload to selected disk though wizard_


### diskless boot

  1) config auto-add in nbmgr, or add client mac by hand

  2) power on pi
