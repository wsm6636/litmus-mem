-include make.conf
LIBLITMUS ?= /home/wsm/litmus/liblitmus
include ${LIBLITMUS}/inc/config.makefile
CPPFLAGS += -Iinclude/
CFLAGS +=-D_PERIOD=${period} -D_DEADLINE=${deadline} -D_EXEC_COST=${cost} 
all=${a}
.PHONY:all clean  
all:${all}  
#clean:
#	rm -f ${all} *.o *.d
obj-blackscholes=${a}.o
${all}:${obj-blackscholes} -lm  
clean:
	rm -f ${all} *.o *.d

include ${LIBLITMUS}/inc/depend.makefile
