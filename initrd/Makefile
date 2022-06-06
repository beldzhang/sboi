

rpios:
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
	modinfo sboi > /dev/null
	which nameif > /dev/null   # package: net-tools
	chmod +x archlinux/etc/initcpio/sboi-udhcpc
	mkinitcpio -D /usr/lib/initcpio -D archlinux/etc/initcpio -A sboi -g initramfs-linux.img


.PHONY: ubuntu

ubuntu:
	modinfo sboi > /dev/null
	which nameif > /dev/null   # package: net-tools
	chmod +x ubuntu/hooks/sboi
	mkinitramfs -d ubuntu -o initrd.img


FWDIR  ?= $(shell grep ^/dev/mmcblk0p1 /proc/mounts | awk '{print $$2}')
PKGVER ?= $(shell . /etc/os-release; echo "$$ID-$${VERSION_ID:-rolling}" )
INITRD ?= $(shell ls initrd.xz initr*.img 2> /dev/null )

bootpkg:
	test -n "$(FWDIR)"
#	test `ls initr*.img | wc -l` -eq 1
	test `echo $(INITRD) | wc -w` -eq 1
	mkdir -p .tmpfs-pkg
	mount -t tmpfs none .tmpfs-pkg
	mkdir -p .tmpfs-pkg/boot
	cp -a $(FWDIR)/* .tmpfs-pkg/boot
	find .tmpfs-pkg/boot -name '*.bak' -exec rm {} +
	cat /etc/os-release > .tmpfs-pkg/boot/os-release
	if ! modinfo sboi > .tmpfs-pkg/boot/modinfo-sboi; then \
		modinfo ../module/sboi.ko > .tmpfs-pkg/boot/modinfo-sboi; \
	fi
	uname -m     > .tmpfs-pkg/boot/modinfo-arch
	cp -av $(INITRD) .tmpfs-pkg/boot
	if [ -z "`grep ^initramfs .tmpfs-pkg/boot/config.txt`" ]; then \
		echo "initramfs $(INITRD) followkernel" >> .tmpfs-pkg/boot/config.txt; \
	fi
	cd .tmpfs-pkg && tar cf ../bootpkg-$(PKGVER).tar boot
	umount .tmpfs-pkg


clean:
	rm -fv initrd.xz initr*.img bootpkg-*.tar
