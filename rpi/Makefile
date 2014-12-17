CXX=g++
CXX_OPTS= -Wall -g -O2 

CC=cc
CC_OPTS=

%.o: %.c                                                                         
	$(CXX) -c $(CXX_OPTS) $< -o $@ 

all: camera_server.o 
	$(CC) camera_server.o -o camera_server $(LDFLAGS) $(CC_OPTS) 

install:
	$(INSTALL) -m 755 camera_server $(DESTDIR)/usr/local/bin/

clean:
	rm -rf camera_server
	rm -rf *.o *~ *.mod

