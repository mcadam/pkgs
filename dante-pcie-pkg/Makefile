#
#  File     : Makefile
#  Created  : Thu Feb 10 18:17:06 EST 2011
#  Updated  : 2016-09-23
#  Author   : Aidan Williams
#  Synopsis : Makefile to build Dante PCIe Linux ALSA kernel module
#
#  Portions of this software are copyright (c) 2004-2016 Audinate Pty Ltd and/or
#  its licensors. All intellectual property rights in such portions of the
#  Software and documentation are owned by Audinate and are protected by
#  United States copyright laws, other applicable copyright laws
#  and international treaty provisions. Audinate and its suppliers retain
#  all rights not expressly granted.
#
#  Audinate Copyright Header Version 2
#

ifneq ($(KERNELRELEASE),)
obj-m := dante-pcie.o
else
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
