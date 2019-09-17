obj-m += mypipe.o
KERNELBUILD := /lib/modules/$(shell uname -r)/build
default:
	make -C $(KERNELBUILD) M=$(PWD) modules
clean:
	make -C $(KERNELBUILD) M=$(PWD) clean