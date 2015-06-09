obj-m := tempsensor.o 

USER := grzybek
BROOT_PATH := /home/$(USER)/projects/rpi/rpi-buildroot
LINUX_DIR_NAME := linux-rpi-4.0.y
#KDIR  := /lib/modules/$(shell uname -r)/build
KDIR  := $(BROOT_PATH)/output/build/$(LINUX_DIR_NAME)/

export CROSS_COMPILE=$(BROOT_PATH)/output/host/usr/bin/arm-buildroot-linux-uclibcgnueabihf-
export ARCH=arm

PWD   := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

debug:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
