obj-m := mytoggle.o

KDIR          ?= /opt/src/linux-6.12.69
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

# 注意：M= 后面必须加引号，否则路径里有空格会被 make 拆成两个参数
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M="$(PWD)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) KBUILD_MODPOST_WARN=1 modules

clean:
	$(MAKE) -C $(KDIR) M="$(PWD)" clean
