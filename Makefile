CC = cc
CFLAGS = -Wall -O2
LDFLAGS = -lz

OBJS = fbvnc.o draw.o

all: fbvnc
.c.o:
	$(CC) -c $(CFLAGS) $<
fbvnc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
clean:
	rm -f *.o fbvnc
