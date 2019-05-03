-include make.conf
LIBLITMUS ?= /home/wsm/litmus/liblitmus
include ${LIBLITMUS}/inc/config.makefile
CPPFLAGS += -Iinclude/
#CFLAGS +=-D_PERIOD=100 -D_DEADLINE=100 -D_EXEC_COST=10
all=blackscholes
.PHONY:all clean  
all:${all}  
clean:
	rm -f ${all} *.o *.d
obj-blackscholes=blackscholes.o
blackscholes: ${obj-blackscholes} -lm  

include ${LIBLITMUS}/inc/depend.makefile
