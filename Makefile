CC = cc
CFLAGS = -Wall -O2
LDFLAGS = -lz

all: fbvnc
.c.o:
	$(CC) -c $(CFLAGS) $<
fbvnc: fbvnc.o draw.o
	$(CC) $(LDFLAGS) -o $@ $^
clean:
	rm -f *.o fbvnc
