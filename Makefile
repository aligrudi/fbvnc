CC = cc
CFLAGS = -Wall -Os
LDFLAGS =

all: fbvnc
.c.o:
	$(CC) -c $(CFLAGS) $<
fbvnc: fbvnc.o draw.o
	$(CC) $(LDFLAGS) -o $@ $^
clean:
	rm -f *.o fbvnc
