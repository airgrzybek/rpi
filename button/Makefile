obj-m := button.o 

#KDIR  := /lib/modules/$(shell uname -r)/build
KDIR  := /home/dpowazka/rpi/rpi-buildroot/output/build/linux-custom/
export CROSS_COMPILE=/home/dpowazka/rpi/rpi-buildroot/output/host/usr/bin/arm-buildroot-linux-uclibcgnueabihf-
export ARCH=arm
PWD   := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
