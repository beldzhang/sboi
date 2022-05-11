

NBD = $(shell modinfo -F filename nbd)

all:
	test -f tree/usr/bin/busybox
	mkdir -p build
	rm -fr build/*
	cp -a tree/* build/ ; :
	cp -a bin-`arch`/* build/ ; :
	ln -sf usr/lib usr/bin usr/sbin build

	chmod +x build/init build/bin/busybox
	mkdir -p build/`dirname $(NBD)`
	cp -av $(NBD) build/`dirname $(NBD)`
	depmod -b build
	ldconfig -r build
	cd build && find . | cpio -o -H newc 2> /dev/null | xz -9 -T 0 -C crc32 > ../initrd.xz
