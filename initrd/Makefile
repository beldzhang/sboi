

all:
	cd ../module && make modules
	test -f tree/bin/busybox
	test -f ../module/sboi.ko
	mkdir -p build
	rm -fr build/*
	cp -a tree/* build/
	mkdir -p build/mnt/sboi build/mnt/root build/mnt/overlay
	chmod +x build/init build/bin/busybox
	mkdir -p build/lib/modules/`uname -r`/updates
	cp -a ../module/sboi.ko build/lib/modules/`uname -r`/updates
	mkdir -p build/lib/modules/`uname -r`/kernel
	cp -a `modinfo -F filename overlay` build/lib/modules/`uname -r`/kernel
	cp -a /lib/modules/`uname -r`/modules.builtin build/lib/modules/`uname -r`
	cp -a /lib/modules/`uname -r`/modules.order   build/lib/modules/`uname -r`
	depmod -b build
	cd build && find . | cpio -o -H newc 2> /dev/null | xz -9 -T 0 -C crc32 > ../initrd.xz


arch:
	cp -auv archlinux/* /
	chmod +x /etc/initcpio/sboi-udhcpc
	mkinitcpio -A sboi -g initrd-arch.img
