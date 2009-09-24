# Makefile for vt52vnc, (c) 1999 Milan Pikula

CC = gcc
CCOPTS = -Wall -O2 -ggdb
AS = as
ASOPTS =
LD = ld
LDOPTS =
OBJS= vncproto.o vt52vnc.o draw.o

all:	$(OBJS)
	$(CC) $(CCOPTS) -o vt52vnc $(OBJS)

.c.o:
	$(CC) $(CCOPTS) -c -o $*.o $<

.S.o:
	$(AS) $(ASOPTS) -o $*.o $<

clean:
	rm -f core *.o *~ *.bak *.tmp

miniclean:
	rm -f *.o

