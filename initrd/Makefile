

all:
	cd ../module && make modules
	test -f tree/bin/busybox
	test -f ../module/sboi.ko
	mkdir -p build
	rm -fr build/*
	cp -a tree/* build/
	chmod +x build/init build/bin/busybox
	mkdir -p build/lib/modules/`uname -r`/updates
	cp -av ../module/sboi.ko build/lib/modules/`uname -r`/updates
	depmod -b build
	cd build && find . | cpio -o -H newc 2> /dev/null | xz -9 -T 0 -C crc32 > ../initrd.xz
