

driver := sboi

obj-m += $(driver).o

KREL ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KREL)/build

modules clean:
	$(MAKE) -C $(KSRC) M=$(PWD) $@

install:
	mkdir -p /lib/modules/$(KREL)/kernel/updates
	cp -a $(driver).ko /lib/modules/$(KREL)/kernel/updates
	depmod $(KREL)

uninstall:
	rm -f /lib/modules/$(KREL)/kernel/updates/$(driver).ko
	rmdir --ignore-fail-on-non-empty /lib/modules/$(KREL)/kernel/updates
	depmod $(KREL)

up:
	insmod $(driver).ko addr=10.10.1.210 port=12345

down:
	rmmod $(driver)

info:
	modinfo $(driver).ko
