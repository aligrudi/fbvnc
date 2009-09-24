#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#define byterate 1920

#include "vt52vnc.h"

int main(int argc, char * argv[])
{
	int port = DEF_PORT;
	char * host = "127.0.0.1";
	int fd, retval;
	struct termios ti1, ti2;
	fd_set sel_in;
	struct timeval tv;
	unsigned long stat;
	int already_requested = 0;
	int must_draw = 0;
	int must_redraw = 1;

	if (argc>=2)
		host = argv[1];
	if (argc>=3)
		port = atoi(argv[2]);
	fd = vncproto_init(host, port);
	if(fd==-1)
		return -1;

	tcgetattr (0, &ti1);
	ti2 = ti1;
	cfmakeraw(&ti2);

	if (request_vnc_refresh(fd) == -1)
		return -1;
	tcsetattr(0, TCSANOW, &ti2);

	tv.tv_sec = 0;
	tv.tv_usec = 500000;

	while(1) {
		FD_ZERO(&sel_in);
		FD_SET(fd, &sel_in);
		FD_SET(0, &sel_in);
		retval = select(fd+1, &sel_in, NULL, NULL, &tv);
		if (retval == 0) {
			if (!already_requested && !must_redraw && !must_draw) {
				if (request_vnc_refresh(fd) == -1)
					break;
				already_requested = 1;
			}
			if (must_draw || must_redraw) {
				must_draw = drawdelta();
				if (!must_draw) {
					must_draw=must_redraw;
					must_redraw=0;
				}
				stat = get_stat();
				tv.tv_sec = stat/byterate;
				tv.tv_usec = (stat - tv.tv_sec*byterate)*1000000/byterate;
			} else {
				tv.tv_sec = 0;
				tv.tv_usec = 200000;
			}
		}
		if (FD_ISSET(fd, &sel_in)) {
			already_requested = 0;
			if (parse_vnc_in(fd) == -1)
				break;
			must_redraw = 1;
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}
		if (FD_ISSET(0, &sel_in)) {
			if (parse_kbd_in(0, fd) == -1)
				break;
		}
	}
	tcsetattr(0, TCSANOW, &ti1);
#define W(a,b) write((a),(b),strlen(b))
	W(1, "\n\x1bH\x1bJ");
	return 0;
}
