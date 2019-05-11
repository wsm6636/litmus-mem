-include make.conf
LIBLITMUS ?=/home/wsm/litmus/liblitmus
include ${LIBLITMUS}/inc/config.makefile
CPPFLAGS += -Iinclude/
LDFLAGS += -L${LIBLITMUS}

TARGET   = fluidanimate  
OBJS     = serial.o cellpool.o

# To enable visualization comment out the following lines (don't do this for benchmarking)
#OBJS     += fluidview.o
#CXXFLAGS += -DENABLE_VISUALIZATION
#LIBS     += -lglut

all: serial 
#fluidcmp

serial: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o $(TARGET) -llitmus

%.o : %.cpp
	$(CXX) $(CXXFLAGS) -D_GNU_SOURCE -D_XOPEN_SOURCE=600 -c $<

#fluidcmp: fluidcmp.cpp
#	rm -rf fluidcmp
#	$(CXX) fluidcmp.cpp -o fluidcmp
clean:
	rm -rf $(TARGET) 
	rm -rf fluidcmp

include ${LIBLITMUS}/inc/depend.makefile

