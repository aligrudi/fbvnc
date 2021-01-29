/*
 * FBVNC: a small Linux framebuffer VNC viewer
 *
 * Copyright (C) 2009-2021 Ali Gholami Rudi
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/input.h>
#include <zlib.h>
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
static int nodraw;		/* do not draw anything */
static int nodraw_ref;		/* pending screen redraw */
static long vnc_nr;		/* number of bytes received */
static long vnc_nw;		/* number of bytes sent */

static char buf[MAXRES];

static z_stream zstr;
static char *z_out;
static int z_outlen;
static int z_outsize;
static int z_outpos;

static int vread(int fd, void *buf, long len)
{
	long nr = 0;
	long n;
	while (nr < len && (n = read(fd, buf + nr, len - nr)) > 0)
		nr += n;
	vnc_nr += nr;
	if (nr < len)
		printf("fbvnc: partial vnc read!\n");
	return nr < len ? -1 : len;
}

static int vwrite(int fd, void *buf, long len)
{
	int nw = write(fd, buf, len);
	if (nw != len)
		printf("fbvnc: partial vnc write!\n");
	vnc_nw += len;
	return nw < len ? -1 : nw;
}

static int z_init(void)
{
	zstr.zalloc = Z_NULL;
	zstr.zfree = Z_NULL;
	zstr.opaque = Z_NULL;
	zstr.avail_in = 0;
	zstr.next_in = Z_NULL;
	if (inflateInit(&zstr) != Z_OK) {
		fprintf(stderr, "fbvnc: failed to initialize a zlib stream\n");
		return 1;
	}
	return 0;
}

static int z_push(void *src, int len)
{
	z_outlen = 0;
	z_outpos = 0;
	zstr.next_in = src;
	zstr.avail_in = len;
	while (zstr.avail_in > 0) {
		int nr;
		zstr.avail_out = sizeof(buf);
		zstr.next_out = (void *) buf;
		if (inflate(&zstr, Z_NO_FLUSH) != Z_OK)
			return 1;
		nr = sizeof(buf) - zstr.avail_out;
		if (z_outlen + nr > z_outsize) {
			char *old = z_out;
			while (z_outlen + nr > z_outsize)
				z_outsize = MAX(z_outsize, 4096) * 2;
			z_out = malloc(z_outsize);
			if (z_outlen)
				memcpy(z_out, old, z_outlen);
			free(old);
		}
		memcpy(z_out + z_outlen, buf, nr);
		z_outlen += nr;
	}
	return 0;
}

static int z_read(void *dst, int len)
{
	if (z_outpos + len > z_outlen)
		return 1;
	memcpy(dst, z_out + z_outpos, len);
	z_outpos += len;
	return 0;
}

static int z_free(void)
{
	inflateEnd(&zstr);
	free(z_out);
	return 0;
}

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
	int mode = FBM_CLR(fb_mode());
	*rr = (mode >> 8) & 0xf;
	*rg = (mode >> 4) & 0xf;
	*rb = (mode >> 0) & 0xf;
}

static int vnc_init(int fd)
{
	char vncver[16];
	int rr, rg, rb;
	struct vnc_clientinit clientinit;
	struct vnc_serverinit serverinit;
	struct vnc_setpixelformat pixfmt_cmd;
	struct vnc_setencoding enc_cmd;
	u32 enc[] = {htonl(VNC_ENC_ZLIB), htonl(VNC_ENC_RRE), htonl(VNC_ENC_RAW)};
	int connstat = VNC_CONN_FAILED;

	/* handshake */
	if (vread(fd, vncver, 12) < 0)
		return -1;
	strcpy(vncver, "RFB 003.003\n");
	vwrite(fd, vncver, 12);
	if (vread(fd, &connstat, sizeof(connstat)) < 0)
		return -1;
	if (ntohl(connstat) != VNC_CONN_NOAUTH)
		return -1;
	clientinit.shared = 1;
	vwrite(fd, &clientinit, sizeof(clientinit));
	if (vread(fd, &serverinit, sizeof(serverinit)) < 0)
		return -1;
	if (vread(fd, buf, ntohl(serverinit.len)) < 0)
		return -1;
	srv_cols = ntohs(serverinit.w);
	srv_rows = ntohs(serverinit.h);

	/* set up the framebuffer */
	if (fb_init(getenv("FBDEV")))
		return -1;
	cols = MIN(srv_cols, fb_cols());
	rows = MIN(srv_rows, fb_rows());
	bpp = FBM_BPP(fb_mode());
	mr = rows / 2;
	mc = cols / 2;

	/* send framebuffer configuration */
	pixfmt_cmd.type = VNC_SETPIXELFORMAT;
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
	vwrite(fd, &pixfmt_cmd, sizeof(pixfmt_cmd));

	/* send pixel format */
	enc_cmd.type = VNC_SETENCODING;
	enc_cmd.pad = 0;
	enc_cmd.n = htons(3);
	vwrite(fd, &enc_cmd, sizeof(enc_cmd));
	vwrite(fd, enc, ntohs(enc_cmd.n) * sizeof(enc[0]));

	/* initialize zlib */
	z_init();
	return 0;
}

static int vnc_free(void)
{
	z_free();
	fb_free();
	return 0;
}

static int vnc_refresh(int fd, int inc)
{
	struct vnc_updaterequest fbup_req;
	fbup_req.type = VNC_UPDATEREQUEST;
	fbup_req.inc = inc;
	fbup_req.x = htons(oc);
	fbup_req.y = htons(or);
	fbup_req.w = htons(cols);
	fbup_req.h = htons(rows);
	return vwrite(fd, &fbup_req, sizeof(fbup_req)) < 0 ? -1 : 0;
}

static void fb_set(int r, int c, void *mem, int len)
{
	memcpy(fb_mem(r) + c * bpp, mem, len * bpp);
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

static void drawrect(char *pixel, int x, int y, int w, int h)
{
	int i;
	if (x < 0 || x + w >= srv_cols || y < 0 || y + h >= srv_rows)
		return;
	for (i = 0; i < w; i++)
		memcpy(buf + i * bpp, pixel, bpp);
	for (i = 0; i < h; i++)
		drawfb(buf, x, y + i, w, 1);
}

static int readrect(int fd)
{
	struct vnc_rect uprect;
	int x, y, w, h;
	int i;
	if (vread(fd, &uprect, sizeof(uprect)) <  0)
		return -1;
	x = ntohs(uprect.x);
	y = ntohs(uprect.y);
	w = ntohs(uprect.w);
	h = ntohs(uprect.h);
	if (x < 0 || w < 0 || x + w > srv_cols)
		return -1;
	if (y < 0 || h < 0 || y + h > srv_rows)
		return -1;
	if (uprect.enc == htonl(VNC_ENC_RAW)) {
		for (i = 0; i < h; i++) {
			if (vread(fd, buf, w * bpp) < 0)
				return -1;
			if (!nodraw)
				drawfb(buf, x, y + i, w, 1);
		}
	}
	if (uprect.enc == htonl(VNC_ENC_RRE)) {
		char pixel[8];
		u32 n;
		vread(fd, &n, 4);
		vread(fd, pixel, bpp);
		if (!nodraw)
			drawrect(pixel, x, y, w, h);
		for (i = 0; i < ntohl(n); i++) {
			u16 pos[4];
			vread(fd, pixel, bpp);
			vread(fd, pos, 8);
			if (!nodraw)
				drawrect(pixel, x + ntohs(pos[0]), y + ntohs(pos[1]),
					ntohs(pos[2]), ntohs(pos[3]));
		}
	}
	if (uprect.enc == htonl(VNC_ENC_ZLIB)) {
		int zlen;
		char *zdat;
		vread(fd, &zlen, 4);
		zdat = malloc(ntohl(zlen));
		vread(fd, zdat, ntohl(zlen));
		z_push(zdat, ntohl(zlen));
		free(zdat);
		for (i = 0; i < h; i++) {
			z_read(buf, w * bpp);
			if (!nodraw)
				drawfb(buf, x, y + i, w, 1);
		}
	}
	return 0;
}

static int vnc_event(int fd)
{
	char msg[1 << 12];
	struct vnc_update *fbup = (void *) msg;
	struct vnc_servercuttext *cuttext = (void *) msg;
	struct vnc_setcolormapentries *colormap = (void *) msg;
	int i;
	int n;

	if (vread(fd, msg, 1) < 0)
		return -1;
	switch (msg[0]) {
	case VNC_UPDATE:
		vread(fd, msg + 1, sizeof(*fbup) - 1);
		n = ntohs(fbup->n);
		for (i = 0; i < n; i++)
			if (readrect(fd))
				return -1;
		break;
	case VNC_BELL:
		break;
	case VNC_SERVERCUTTEXT:
		vread(fd, msg + 1, sizeof(*cuttext) - 1);
		vread(fd, buf, ntohl(cuttext->len));
		break;
	case VNC_SETCOLORMAPENTRIES:
		vread(fd, msg + 1, sizeof(*colormap) - 1);
		vread(fd, buf, ntohs(colormap->n) * 3 * 2);
		break;
	default:
		fprintf(stderr, "fbvnc: unknown vnc msg %d\n", msg[0]);
		return -1;
	}
	return 0;
}

static int rat_event(int fd, int ratfd)
{
	char ie[4] = {0};
	struct vnc_pointerevent me = {VNC_POINTEREVENT};
	int mask = 0;
	int or_ = or, oc_ = oc;
	if (ratfd > 0 && read(ratfd, &ie, sizeof(ie)) != 4)
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
	vwrite(fd, &me, sizeof(me));
	if (or != or_ || oc != oc_)
		if (vnc_refresh(fd, 0))
			return -1;
	return 0;
}

static int press(int fd, int key, int down)
{
	struct vnc_keyevent ke = {VNC_KEYEVENT};
	ke.key = htonl(key);
	ke.down = down;
	vwrite(fd, &ke, sizeof(ke));
	return 0;
}

static void showmsg(void)
{
	char msg[128];
	sprintf(msg, "\x1b[HFBVNC \t\t nr=%-8ld\tnw=%-8ld\r", vnc_nr, vnc_nw);
	OUT(msg);
}

static void nodraw_set(int val)
{
	if (val && !nodraw)
		showmsg();
	if (!val && nodraw)
		nodraw_ref = 1;
	nodraw = val;
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
			nodraw_set(1 - nodraw);
		default:
			k = (unsigned char) key[i];
		}
		if ((k >= 'A' && k <= 'Z') || strchr(":\"<>?{}|+_()*&^%$#@!~", k))
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
	rat_event(vnc_fd, -1);
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
		if (!nodraw && nodraw_ref) {
			nodraw_ref = 0;
			if (vnc_refresh(vnc_fd, 0))
				break;
		}
		if (!pending++)
			if (vnc_refresh(vnc_fd, 1))
				break;
	}
}

static void signalreceived(int sig)
{
	if (sig == SIGUSR1 && !nodraw)		/* disable drawing */
		nodraw_set(1);
	if (sig == SIGUSR2 && nodraw)		/* enable drawing */
		nodraw_set(0);
}

int main(int argc, char * argv[])
{
	char *port = VNC_PORT;
	char *host = "127.0.0.1";
	struct termios ti;
	int vnc_fd, rat_fd;
	if (argc >= 2 && argv[1][0] && strcmp("-", argv[1]))
		host = argv[1];
	if (argc >= 3)
		port = argv[2];
	if ((vnc_fd = vnc_connect(host, port)) < 0) {
		fprintf(stderr, "fbvnc: could not connect!\n");
		return 1;
	}
	if (vnc_init(vnc_fd) < 0) {
		close(vnc_fd);
		fprintf(stderr, "fbvnc: vnc init failed!\n");
		return 1;
	}
	if (getenv("TERM_PGID") != NULL && atoi(getenv("TERM_PGID")) == getppid())
		if (tcsetpgrp(0, getppid()) == 0)
			setpgid(0, getppid());
	term_setup(&ti);

	/* entering intellimouse for using mouse wheel */
	rat_fd = open("/dev/input/mice", O_RDWR);
	write(rat_fd, "\xf3\xc8\xf3\x64\xf3\x50", 6);
	read(rat_fd, buf, 1);
	signal(SIGUSR1, signalreceived);
	signal(SIGUSR2, signalreceived);

	mainloop(vnc_fd, 0, rat_fd);

	term_cleanup(&ti);
	vnc_free();
	close(vnc_fd);
	close(rat_fd);
	return 0;
}
