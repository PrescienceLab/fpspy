obj-m += fpvm_dev.o
fpvm_dev-objs += fpvm_kmod.o idt_stubs.o

KERNEL_ROOT=./testing_playground/obj/linux

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

#all:
#	make -C $(KERNEL_ROOT) M=$(PWD) modules
#clean:
#	make -C $(KERNEL_ROOT) M=$(PWD) clean
