# Makefile for streamcluster
-include make.conf
LIBLITMUS ?=/home/wsm/litmus/liblitmus
include ${LIBLITMUS}/inc/config.makefile
CPPFLAGS += -Iinclude/

TARGET=streamcluster1

.PHONY:all clean
OBJS=streamcluster1.o

LDFLAGS += -L${LIBLITMUS}

all: $(OBJS) 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LIBS) -o $(TARGET) -llitmus 
clean:
	rm -f $(TARGET) *.o *.d

include ${LIBLITMUS}/inc/depend.makefile


