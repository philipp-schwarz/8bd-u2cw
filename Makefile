obj-m = 8bd-u2cw.o

KVERSION = $(shell uname -r)

all:
	make -C /lib/modules/$(KVERSION)/build V=1 M=$(PWD) modules
clean:
	test ! -d /lib/modules/$(KVERSION) || make -C /lib/modules/$(KVERSION)/build V=1 M=$(PWD) clean


install:
	cp 8bd-u2cw.ko /lib/modules/$(KVERSION)/kernel/drivers/input/joystick
	depmod
	cp 99-8bd-u2cw.rules /etc/udev/rules.d
	udevadm control --reload-rules
	udevadm trigger
	modprobe 8bd-u2cw
uninstall:
	rm -f /lib/modules/$(KVERSION)/kernel/drivers/input/joystick/8bd-u2cw.ko
	depmod
	rm -f /etc/udev/rules.d/99-8bd-u2cw.rules
	udevadm control --reload-rules
	udevadm trigger
	rmmod 8bd-u2cw
