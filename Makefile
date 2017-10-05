MODULE_NAME = asgn1
# EXTRA_CFLAGS += -Werror


obj-m   := $(MODULE_NAME).o


KDIR    := /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)



all: module mmap_test

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

mmap_test: mmap_test.c
	gcc -g -W -Wall mmap_test.c -o mmap_test

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f mmap_test mmap_test.o

help:
	$(MAKE) -C $(KDIR) M=$(PWD) help

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

