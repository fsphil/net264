
CC=gcc
CFLAGS=-g -O3 -Wall
LDFLAGS=-g

all: net264

net264: net264.o
	$(CC) $(LDFLAGS) net264.o -o net264

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

install: all
	mkdir -p ${DESTDIR}/usr/bin
	install -m 755 net264 ${DESTDIR}/usr/bin

clean:
	rm -f *.o net264

