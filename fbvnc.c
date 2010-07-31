/*
 * fbvnc - a small linux framebuffer vnc viewer
 *
 * Copyright (C) 2009-2010 Ali Gholami Rudi
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, as published by the
 * Free Software Foundation.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/input.h>
#include "draw.h"
#include "vnc.h"

#define VNC_PORT		"5900"

#define MAXRES			(1 << 21)
#define MIN(a, b)		((a) < (b) ? (a) : (b))

static int cols, rows;
static int mr, mc;		/* mouse position */

static char buf[MAXRES];

static int vnc_connect(char *addr, char *port)
{
	struct addrinfo hints, *addrinfo;
	int fd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(addr, port, &hints, &addrinfo))
		return -1;
	fd = socket(addrinfo->ai_family, addrinfo->ai_socktype,
			addrinfo->ai_protocol);

	if (connect(fd, addrinfo->ai_addr, addrinfo->ai_addrlen) == -1) {
		close(fd);
		freeaddrinfo(addrinfo);
		return -1;
	}
	freeaddrinfo(addrinfo);
	return fd;
}

int vnc_init(int fd)
{
	char vncver[] = "RFB 003.003\n";
	struct vnc_client_init clientinit;
	struct vnc_server_init serverinit;
	struct vnc_client_pixelfmt pixfmt_cmd;
	int connstat = VNC_CONN_FAILED;

	write(fd, vncver, 12);
	read(fd, vncver, 12);

	read(fd, &connstat, sizeof(connstat));

	if (ntohl(connstat) != VNC_CONN_NOAUTH)
		return -1;

	clientinit.shared = 1;
	write(fd, &clientinit, sizeof(clientinit));
	read(fd, &serverinit, sizeof(serverinit));

	fb_init();
	cols = MIN(ntohs(serverinit.w), fb_cols());
	rows = MIN(ntohs(serverinit.h), fb_rows());
	mr = rows / 2;
	mc = cols / 2;

	read(fd, buf, ntohl(serverinit.len));
	pixfmt_cmd.type = VNC_CLIENT_PIXFMT;
	pixfmt_cmd.format.bpp = 8;
	pixfmt_cmd.format.depth = 8;
	pixfmt_cmd.format.bigendian = 0;
	pixfmt_cmd.format.truecolor = 1;

	pixfmt_cmd.format.rmax = htons(3);
	pixfmt_cmd.format.gmax = htons(7);
	pixfmt_cmd.format.bmax = htons(7);
	pixfmt_cmd.format.rshl = 0;
	pixfmt_cmd.format.gshl = 2;
	pixfmt_cmd.format.bshl = 5;

	write(fd, &pixfmt_cmd, sizeof(pixfmt_cmd));
	return fd;
}

int vnc_free(void)
{
	fb_free();
	return 0;
}

int vnc_refresh(int fd, int inc)
{
	struct vnc_client_fbup fbup_req;
	fbup_req.type = VNC_CLIENT_FBUP;
	fbup_req.inc = inc;
	fbup_req.x = htons(0);
	fbup_req.y = htons(0);
	fbup_req.w = htons(cols);
	fbup_req.h = htons(rows);
	write(fd, &fbup_req, sizeof(fbup_req));
	return 0;
}

static void drawfb(char *s, int x, int y, int w, int h)
{
	fbval_t slice[1 << 14];
	int i, j;
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			unsigned char *p = (void *) &s[i * w + j];
			slice[j] = fb_color(*p, *p, *p);
		}
		fb_set(y + i, x, slice, w);
	}
}

int vnc_event(int fd)
{
	struct vnc_rect uprect;
	char msg[1 << 12];
	struct vnc_server_fbup *fbup = (void *) msg;
	struct vnc_server_cuttext *cuttext = (void *) msg;
	struct vnc_server_colormap *colormap = (void *) msg;
	int nr, i, j;
	int n;

	if ((nr = read(fd, msg, 1)) != 1)
		return -1;

	switch (msg[0]) {
	case VNC_SERVER_FBUP:
		nr = read(fd, msg + 1, sizeof(*fbup) - 1);
		n = ntohs(fbup->n);
		for (j = 0; j < n; j++) {
			int x, y, w, h;
			nr = read(fd, &uprect, sizeof(uprect));
			if (nr != sizeof(uprect))
				return -1;
			x = ntohs(uprect.x);
			y = ntohs(uprect.y);
			w = ntohs(uprect.w);
			h = ntohs(uprect.h);
			if (x >= cols || x + w > cols)
				return -1;
			if (y >= rows || y + h > rows)
				return -1;
			for (i = 0; i < w * h; ) {
				nr = read(fd, buf + i, w * h - i);
				if (nr <= 0)
					return -1;
				i += nr;
			}
			drawfb(buf, x, y, w, h);
		}
		break;
	case VNC_SERVER_BELL:
		break;
	case VNC_SERVER_CUTTEXT:
		nr = read(fd, msg + 1, sizeof(*cuttext) - 1);
		nr = read(fd, buf, cuttext->len);
		break;
	case VNC_SERVER_COLORMAP:
		nr = read(fd, msg + 1, sizeof(*colormap) - 1);
		nr = read(fd, buf, ntohs(colormap->n) * 3 * 2);
		break;
	default:
		fprintf(stderr, "unknown vnc msg: %d\n", msg[0]);
		return -1;
	}
	return 0;
}

int rat_event(int fd, int ratfd)
{
	char ie[3];
	struct vnc_client_ratevent me = {VNC_CLIENT_RATEVENT};
	int mask = 0;
	if (read(ratfd, &ie, sizeof(ie)) != 3)
		return -1;
	mc += ie[1];
	mr -= ie[2];
	mc = MAX(0, MIN(cols - 1, mc));
	mr = MAX(0, MIN(rows - 1, mr));
	if (ie[0] & 0x01)
		mask |= VNC_BUTTON1_MASK;
	if (ie[0] & 0x02)
		mask |= VNC_BUTTON1_MASK;
	if (ie[0] & 0x04)
		mask |= VNC_BUTTON1_MASK;
	me.y = htons(mr);
	me.x = htons(mc);
	me.mask = mask;
	write(fd, &me, sizeof(me));
	return 0;
}

static int press(int fd, int key, int down)
{
	struct vnc_client_keyevent ke = {VNC_CLIENT_KEYEVENT};
	ke.key = htonl(key);
	ke.down = down;
	return write(fd, &ke, sizeof(ke));
}

int kbd_event(int fd, int kbdfd)
{
	char key[1024];
	int i, nr;

	if ((nr = read(kbdfd, key, sizeof(key))) <= 0 )
		return -1;
	for (i = 0; i < nr; i++) {
		int k = -1;
		int mod[4];
		int nmod = 0;
		switch (key[i]) {
		case 0x08:
		case 0x7f:
			k = 0xff08;
			break;
		case 0x09:
			k = 0xff09;
			break;
		case 0x1b:
			if (i + 2 < nr && key[i + 1] == '[') {
				if (key[i + 2] == 'A')
					k = 0xff52;
				if (key[i + 2] == 'B')
					k = 0xff54;
				if (key[i + 2] == 'C')
					k = 0xff53;
				if (key[i + 2] == 'D')
					k = 0xff51;
				if (key[i + 2] == 'H')
					k = 0xff50;
				if (k > 0) {
					i += 2;
					break;
				}
			}
			k = 0xff1b;
			if (i + 1 < nr) {
				mod[nmod++] = 0xffe9;
				k = key[++i];
			}
			break;
		case 0x0d:
			k = 0xff0d;
			break;
		case 0x03:
			return -1;
		default:
			k = (unsigned char) key[i];
		}
		if (isupper(k) || strchr(":\"<>?{}|+_()*&^%$#@!~", k))
			mod[nmod++] = 0xffe1;
		if (k >= 1 && k <= 26) {
			k = 'a' + k - 1;
			mod[nmod++] = 0xffe3;
		}
		if (k > 0) {
			int j;
			for (j = 0; j < nmod; j++)
				press(fd, mod[j], 1);
			press(fd, k, 1);
			press(fd, k, 0);
			for (j = 0; j < nmod; j++)
				press(fd, mod[j], 0);
		}
	}
	return 0;
}

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

static void mainloop(int vnc_fd, int kbd_fd, int rat_fd)
{
	struct pollfd ufds[3];
	int pending = 0;
	int update = 1;
	int err;
	ufds[0].fd = kbd_fd;
	ufds[0].events = POLLIN;
	ufds[1].fd = vnc_fd;
	ufds[1].events = POLLIN;
	ufds[2].fd = rat_fd;
	ufds[2].events = POLLIN;
	while (1) {
		if (update && !pending) {
			if (vnc_refresh(vnc_fd, 1) == -1)
				break;
			pending = 1;
			update = 0;
		}
		err = poll(ufds, 3, 500);
		if (err == -1 && errno != EINTR)
			break;
		if (!err)
			continue;
		if (ufds[0].revents & POLLIN) {
			if (kbd_event(vnc_fd, kbd_fd) == -1)
				break;
			update = 1;
		}
		if (ufds[1].revents & POLLIN) {
			if (vnc_event(vnc_fd) == -1)
				break;
			pending = 0;
		}
		if (ufds[2].revents & POLLIN) {
			if (rat_event(vnc_fd, rat_fd) == -1)
				break;
			update = 1;
		}
	}
}

int main(int argc, char * argv[])
{
	char *port = VNC_PORT;
	char *host = "127.0.0.1";
	struct termios ti;
	int vnc_fd, rat_fd;
	if (argc >= 2)
		host = argv[1];
	if (argc >= 3)
		port = argv[2];
	if ((vnc_fd = vnc_connect(host, port)) == -1) {
		fprintf(stderr, "could not connect!\n");
		return 1;
	}
	if (vnc_init(vnc_fd) == -1) {
		fprintf(stderr, "vnc init failed!\n");
		return 1;
	}
	term_setup(&ti);
	rat_fd = open("/dev/input/mice", O_RDONLY);

	mainloop(vnc_fd, 0, rat_fd);

	term_cleanup(&ti);
	vnc_free();
	close(vnc_fd);
	close(rat_fd);
	return 0;
}
