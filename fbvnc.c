#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "fbvnc.h"

static void term_setup(struct termios *ti)
{
	struct termios termios;
	char *hide = "\x1b[?25l";
	char *clear = "\x1b[2J\x1b[H";
	char *msg = "\t\t\t*** fbvnc ***\r\n";

	write(STDIN_FILENO, hide, strlen(hide));
	write(STDOUT_FILENO, clear, strlen(clear));
	write(STDOUT_FILENO, msg, strlen(msg));
	tcgetattr (0, &termios);
	*ti = termios;
	cfmakeraw(&termios);
	tcsetattr(0, TCSANOW, &termios);
}

static void term_cleanup(struct termios *ti)
{
	char *show = "\x1b[?25h";
	tcsetattr(0, TCSANOW, ti);
	write(STDIN_FILENO, show, strlen(show));
}

int main(int argc, char * argv[])
{
	int port = DEF_PORT;
	char * host = "127.0.0.1";
	int fd, retval;
	fd_set sel_in;
	struct timeval tv;
	int already_requested = 0;
	int must_draw = 0;
	int must_redraw = 1;
	struct termios ti;

	if (argc>=2)
		host = argv[1];
	if (argc>=3)
		port = atoi(argv[2]);
	fd = vncproto_init(host, port);
	if(fd==-1)
		return -1;

	term_setup(&ti);
	if (request_vnc_refresh(fd) == -1) {
		term_cleanup(&ti);
		return -1;
	}
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
				tv.tv_sec = 0;
				tv.tv_usec = 200000;
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
	vncproto_free();
	term_cleanup(&ti);
	return 0;
}
