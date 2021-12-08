

driver := sboi

obj-m += $(driver).o

KSRC ?= /lib/modules/$(shell uname -r)/build

modules clean:
	$(MAKE) -C $(KSRC) M=$(PWD) $@

install:
	mkdir -p /lib/modules/$(shell uname -r)/kernel/updates
	cp -a $(driver).ko /lib/modules/$(shell uname -r)/kernel/updates

up:
	insmod $(driver).ko addr=10.10.1.210 port=12345

down:
	rmmod $(driver)

info:
	modinfo $(driver).ko
