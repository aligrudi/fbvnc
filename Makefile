CC = cc
CFLAGS = -std=gnu89 -pedantic -Wall -O2
LDFLAGS =

all: fbvnc
.c.o:
	$(CC) -c $(CFLAGS) $<
fbvnc: vncproto.o fbvnc.o draw.o
	$(CC) $(LDFLAGS) -o $@ $^
clean:
	rm -f *.o fbvnc
