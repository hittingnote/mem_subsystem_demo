kern_tgt := demo_page_list
ifneq ($(KERNELRELEASE),)
	$(kern_tgt)-objs := kern_main.o demo_kern_core.o
	obj-m := $(kern_tgt).o
	EXTRA_CFLAGS += -D__COMPILE_KERNEL_CODE
else
	BUILDSYSTEM_DIR := /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
	cc := gcc
	cflags := -g
	src := $(wildcard user_*.c)
	obj := $(patsubst %.c,%.o, $(src))
	target := user_app
	include := common.h

all: $(target) $(include)
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules

$(target): $(obj)
	$(cc) -g $^ -o $(target)

$(obj): %.o: %.c $(include)
	$(cc) -g -c $(abspath $<) -o $@

.PHONY: clean
clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean
ifneq ($(shell ls $(target) 2> /dev/null),)
	rm $(target)
endif

install: uninstall
	insmod $(kern_tgt).ko

uninstall:
ifneq ($(shell lsmod | grep $(kern_tgt) 2> /dev/null),)
	rmmod $(kern_tgt)
endif

endif
