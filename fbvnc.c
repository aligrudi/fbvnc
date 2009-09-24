/*
 * fbvnc - A small linux framebuffer vnc viewer
 *
 * Copyright (C) 1999 Milan Pikula
 * Copyright (C) 2009 Ali Gholami Rudi
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, as published by the
 * Free Software Foundation.
 */
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

static void mainloop(int fd)
{
	fd_set sel_in;
	struct timeval tv;
	int pending = 0;
	int retval = 0;
	while(1) {
		if (!retval && !pending) {
			if (request_vnc_refresh(fd) == -1)
				break;
			pending = 1;
		}
		tv.tv_sec = 0;
		tv.tv_usec = 500000;
		FD_ZERO(&sel_in);
		FD_SET(fd, &sel_in);
		FD_SET(0, &sel_in);
		retval = select(fd + 1, &sel_in, NULL, NULL, &tv);
		if (!retval)
			continue;
		if (FD_ISSET(0, &sel_in))
			if (parse_kbd_in(0, fd) == -1)
				break;
		if (FD_ISSET(fd, &sel_in)) {
			if (parse_vnc_in(fd) == -1)
				break;
			pending = 0;
		}
	}
}

int main(int argc, char * argv[])
{
	int port = DEF_PORT;
	char * host = "127.0.0.1";
	struct termios ti;
	int fd;
	if (argc >= 2)
		host = argv[1];
	if (argc >= 3)
		port = atoi(argv[2]);
	if ((fd = vncproto_init(host, port)) == -1)
		return -1;
	term_setup(&ti);

	mainloop(fd);

	term_cleanup(&ti);
	vncproto_free();
	return 0;
}
