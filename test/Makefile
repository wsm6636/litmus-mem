obj-m=test1.o

KVERSION=$(shell uname -r)
BLDDIR=/lib/modules/$(KVERSION)/build
#KBUILD_EXTRA_SYMBOLS += ../Module.symvers
#export KBUILD_EXTRA_SYMBOLS
all:
	make -C $(BLDDIR) M=$(PWD) modules

clean:
	make -C $(BLDDIR) M=$(PWD) clean
