ifneq ($(KERNELRELEASE),)
	mmap_kern-objs := kern_main.o
	obj-m := mmap_kern.o
	EXTRA_CFLAGS += -D__KERNEL__
else
	BUILDSYSTEM_DIR := /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
	cc := gcc
	cflags := -g
	src := $(wildcard user_*.c)
	target := user_app
	include := common.h

all: $(target)
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules

$(target): $(patsubst %.c,%.o, $(src))
	$(cc) $(cflags) $^ -o $@

$(patsubst %.c,%.o, $(src)): %.o: %.c $(include)
	$(cc) -c $(cflags) $< -o $@

.PHONY: clean
clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean
ifneq ($(shell ls $(target) 2> /dev/null),)
	rm $(target)
endif

.PHONY: install
install: uninstall
	insmod mmap_kern.ko

uninstall:
ifneq ($(shell lsmod | grep mmap_kern 2> /dev/null),)
	rmmod mmap_kern
endif

endif