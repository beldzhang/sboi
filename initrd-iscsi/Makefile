

MODULES = iscsi-tcp libiscsi-tcp libiscsi

all:
	test -f tree/usr/bin/busybox
	mkdir -p build
	rm -fr build/*
	cp -a tree/* build/ ; :
	cp -a bin-`arch`/* build/ ; :
	ln -sf usr/lib usr/bin usr/sbin build

	chmod +x build/init build/bin/busybox
	for m in $(MODULES); do \
		f=`modinfo -F filename $$m`; \
		mkdir -p build/`dirname $$f`; \
		cp -a $$f build/`dirname $$f`; \
	done
	cp -a /lib/modules/`uname -r`/modules.builtin build/lib/modules/`uname -r`
	cp -a /lib/modules/`uname -r`/modules.order   build/lib/modules/`uname -r`
	depmod -b build
	ldconfig -r build
	cd build && find . | cpio -o -H newc 2> /dev/null | xz -9 -T 0 -C crc32 > ../initrd.xz
