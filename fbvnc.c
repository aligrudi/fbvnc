/*
 * fbvnc - a small linux framebuffer vnc viewer
 *
 * Copyright (C) 2009-2013 Ali Gholami Rudi
 *
 * This program is released under the modified BSD license.
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

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define OUT(msg)	write(1, (msg), strlen(msg))

#define VNC_PORT	"5900"
#define SCRSCRL		2
#define MAXRES		(1 << 16)

static int cols, rows;		/* framebuffer dimensions */
static int bpp;			/* bytes per pixel */
static int srv_cols, srv_rows;	/* server screen dimensions */
static int or, oc;		/* visible screen offset */
static int mr, mc;		/* mouse position */
static int nodraw;		/* don't draw anything */

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

static void fbmode_bits(int *rr, int *rg, int *rb)
{
	int mode = FBM_COLORS(fb_mode());
	*rr = (mode >> 8) & 0xf;
	*rg = (mode >> 4) & 0xf;
	*rb = (mode >> 0) & 0xf;
}

static int vnc_init(int fd)
{
	char vncver[16];
	int rr, rg, rb;

	struct vnc_client_init clientinit;
	struct vnc_server_init serverinit;
	struct vnc_client_pixelfmt pixfmt_cmd;
	int connstat = VNC_CONN_FAILED;

	read(fd, vncver, 12);
	strcpy(vncver, "RFB 003.003\n");
	write(fd, vncver, 12);

	read(fd, &connstat, sizeof(connstat));

	if (ntohl(connstat) != VNC_CONN_NOAUTH)
		return -1;

	clientinit.shared = 1;
	write(fd, &clientinit, sizeof(clientinit));
	read(fd, &serverinit, sizeof(serverinit));

	if (fb_init())
		return -1;
	srv_cols = ntohs(serverinit.w);
	srv_rows = ntohs(serverinit.h);
	cols = MIN(srv_cols, fb_cols());
	rows = MIN(srv_rows, fb_rows());
	bpp = FBM_BPP(fb_mode());
	mr = rows / 2;
	mc = cols / 2;

	read(fd, buf, ntohl(serverinit.len));
	pixfmt_cmd.type = VNC_CLIENT_PIXFMT;
	pixfmt_cmd.format.bpp = bpp << 3;
	pixfmt_cmd.format.depth = bpp << 3;
	pixfmt_cmd.format.bigendian = 0;
	pixfmt_cmd.format.truecolor = 1;

	fbmode_bits(&rr, &rg, &rb);
	pixfmt_cmd.format.rmax = htons((1 << rr) - 1);
	pixfmt_cmd.format.gmax = htons((1 << rg) - 1);
	pixfmt_cmd.format.bmax = htons((1 << rb) - 1);
	/* assuming colors packed as RGB; shall handle other cases later */
	pixfmt_cmd.format.rshl = rg + rb;
	pixfmt_cmd.format.gshl = rb;
	pixfmt_cmd.format.bshl = 0;
	write(fd, &pixfmt_cmd, sizeof(pixfmt_cmd));
	return 0;
}

static int vnc_free(void)
{
	fb_free();
	return 0;
}

static int vnc_refresh(int fd, int inc)
{
	struct vnc_client_fbup fbup_req;
	fbup_req.type = VNC_CLIENT_FBUP;
	fbup_req.inc = inc;
	fbup_req.x = htons(oc);
	fbup_req.y = htons(or);
	fbup_req.w = htons(cols);
	fbup_req.h = htons(rows);
	return write(fd, &fbup_req, sizeof(fbup_req)) != sizeof(fbup_req);
}

static void drawfb(char *s, int x, int y, int w, int h)
{
	int sc;		/* screen column offset */
	int bc, bw;	/* buffer column offset / row width */
	int i;
	sc = MAX(0, x - oc);
	bc = x > oc ? 0 : oc - x;
	bw = x + w < oc + cols ? w - bc : w - bc - (x + w - oc - cols);
	for (i = y; i < y + h; i++)
		if (i - or >= 0 && i - or < rows && bw > 0)
			fb_set(i - or, sc, s + ((i - y) * w + bc) * bpp, bw);
}

static void xread(int fd, void *buf, int len)
{
	int nr = 0;
	int n;
	while (nr < len && (n = read(fd, buf + nr, len - nr)) > 0)
		nr += n;
	if (nr < len) {
		printf("partial vnc read!\n");
		exit(1);
	}
}

static int vnc_event(int fd)
{
	struct vnc_rect uprect;
	char msg[1 << 12];
	struct vnc_server_fbup *fbup = (void *) msg;
	struct vnc_server_cuttext *cuttext = (void *) msg;
	struct vnc_server_colormap *colormap = (void *) msg;
	int i, j;
	int n;

	if (read(fd, msg, 1) != 1)
		return -1;
	switch (msg[0]) {
	case VNC_SERVER_FBUP:
		xread(fd, msg + 1, sizeof(*fbup) - 1);
		n = ntohs(fbup->n);
		for (j = 0; j < n; j++) {
			int x, y, w, h;
			xread(fd, &uprect, sizeof(uprect));
			x = ntohs(uprect.x);
			y = ntohs(uprect.y);
			w = ntohs(uprect.w);
			h = ntohs(uprect.h);
			if (x >= srv_cols || x + w > srv_cols)
				return -1;
			if (y >= srv_rows || y + h > srv_rows)
				return -1;
			for (i = 0; i < h; i++) {
				xread(fd, buf, w * bpp);
				if (!nodraw)
					drawfb(buf, x, y + i, w, 1);
			}
		}
		break;
	case VNC_SERVER_BELL:
		break;
	case VNC_SERVER_CUTTEXT:
		xread(fd, msg + 1, sizeof(*cuttext) - 1);
		xread(fd, buf, ntohl(cuttext->len));
		break;
	case VNC_SERVER_COLORMAP:
		xread(fd, msg + 1, sizeof(*colormap) - 1);
		xread(fd, buf, ntohs(colormap->n) * 3 * 2);
		break;
	default:
		fprintf(stderr, "unknown vnc msg: %d\n", msg[0]);
		return -1;
	}
	return 0;
}

static int rat_event(int fd, int ratfd)
{
	char ie[4];
	struct vnc_client_ratevent me = {VNC_CLIENT_RATEVENT};
	int mask = 0;
	int or_ = or, oc_ = oc;
	if (read(ratfd, &ie, sizeof(ie)) != 4)
		return -1;
	/* ignore mouse movements when nodraw */
	if (nodraw)
		return 0;
	mc += ie[1];
	mr -= ie[2];

	if (mc < oc)
		oc = MAX(0, oc - cols / SCRSCRL);
	if (mc >= oc + cols && oc + cols < srv_cols)
		oc = MIN(srv_cols - cols, oc + cols / SCRSCRL);
	if (mr < or)
		or = MAX(0, or - rows / SCRSCRL);
	if (mr >= or + rows && or + rows < srv_rows)
		or = MIN(srv_rows - rows, or + rows / SCRSCRL);
	mc = MAX(oc, MIN(oc + cols - 1, mc));
	mr = MAX(or, MIN(or + rows - 1, mr));
	if (ie[0] & 0x01)
		mask |= VNC_BUTTON1_MASK;
	if (ie[0] & 0x04)
		mask |= VNC_BUTTON2_MASK;
	if (ie[0] & 0x02)
		mask |= VNC_BUTTON3_MASK;
	if (ie[3] > 0)		/* wheel up */
		mask |= VNC_BUTTON4_MASK;
	if (ie[3] < 0)		/* wheel down */
		mask |= VNC_BUTTON5_MASK;

	me.y = htons(mr);
	me.x = htons(mc);
	me.mask = mask;
	write(fd, &me, sizeof(me));
	if (or != or_ || oc != oc_)
		if (vnc_refresh(fd, 0))
			return -1;
	return 0;
}

static int press(int fd, int key, int down)
{
	struct vnc_client_keyevent ke = {VNC_CLIENT_KEYEVENT};
	ke.key = htonl(key);
	ke.down = down;
	return write(fd, &ke, sizeof(ke));
}

static void showmsg(void)
{
	OUT("\x1b[H\t\t\t*** fbvnc ***\r");
}

static int kbd_event(int fd, int kbdfd)
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
				if (k == 0x03)	/* esc-^C: quit */
					return -1;
			}
			break;
		case 0x0d:
			k = 0xff0d;
			break;
		case 0x0:	/* c-space: stop/start drawing */
			if (!nodraw) {
				nodraw = 1;
				showmsg();
			} else {
				nodraw = 0;
				if (vnc_refresh(fd, 0))
					return -1;
			}
		default:
			k = (unsigned char) key[i];
		}
		if (k >= 'A' && k <= 'Z' || strchr(":\"<>?{}|+_()*&^%$#@!~", k))
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
	OUT("\033[2J");		/* clear the screen */
	OUT("\033[?25l");	/* hide the cursor */
	showmsg();
	tcgetattr(0, &termios);
	*ti = termios;
	cfmakeraw(&termios);
	tcsetattr(0, TCSANOW, &termios);
}

static void term_cleanup(struct termios *ti)
{
	tcsetattr(0, TCSANOW, ti);
	OUT("\r\n\033[?25h");	/* show the cursor */
}

static void mainloop(int vnc_fd, int kbd_fd, int rat_fd)
{
	struct pollfd ufds[3];
	int pending = 0;
	int err;
	ufds[0].fd = kbd_fd;
	ufds[0].events = POLLIN;
	ufds[1].fd = vnc_fd;
	ufds[1].events = POLLIN;
	ufds[2].fd = rat_fd;
	ufds[2].events = POLLIN;
	if (vnc_refresh(vnc_fd, 0))
		return;
	while (1) {
		err = poll(ufds, 3, 500);
		if (err == -1 && errno != EINTR)
			break;
		if (!err)
			continue;
		if (ufds[0].revents & POLLIN)
			if (kbd_event(vnc_fd, kbd_fd) == -1)
				break;
		if (ufds[1].revents & POLLIN) {
			if (vnc_event(vnc_fd) == -1)
				break;
			pending = 0;
		}
		if (ufds[2].revents & POLLIN)
			if (rat_event(vnc_fd, rat_fd) == -1)
				break;
		if (!pending++)
			if (vnc_refresh(vnc_fd, 1))
				break;
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
	if ((vnc_fd = vnc_connect(host, port)) < 0) {
		fprintf(stderr, "could not connect!\n");
		return 1;
	}
	if (vnc_init(vnc_fd) < 0) {
		close(vnc_fd);
		fprintf(stderr, "vnc init failed!\n");
		return 1;
	}
	term_setup(&ti);

	/* entering intellimouse for using mouse wheel */
	rat_fd = open("/dev/input/mice", O_RDWR);
	write(rat_fd, "\xf3\xc8\xf3\x64\xf3\x50", 6);
	read(rat_fd, buf, 1);

	mainloop(vnc_fd, 0, rat_fd);

	term_cleanup(&ti);
	vnc_free();
	close(vnc_fd);
	close(rat_fd);
	return 0;
}
