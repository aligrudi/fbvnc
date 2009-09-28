#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "draw.h"
#include "fbvnc.h"

#define MAXRESOL		(1 << 21)

/* vnc part */
static CARD16 cols = 0;
static CARD16 rows = 0;

/* and buffer for screen updates */
static CARD8 updates[MAXRESOL];
static int redraw;

int vncproto_init(char * addr, int port)
{
	struct sockaddr_in si;
	rfbProtocolVersionMsg vmsg;
	rfbClientInitMsg clientinit;
	rfbServerInitMsg serverinit;
	rfbSetPixelFormatMsg pixformmsg;
	struct hostent * he;
	int servsock, i;
	CARD32 i32;
	CARD8 x;
#if 0
	CARD8 passwd[128];
#endif
	rfbSetEncodingsMsg * encodingsmsgp;

	encodingsmsgp = (rfbSetEncodingsMsg *)malloc(sizeof(rfbSetEncodingsMsg)+sizeof(CARD32));
	if (encodingsmsgp == NULL) {
		perror("malloc: Cannot initiate communication");
		return -1;
	}

	si.sin_family = AF_INET;
	si.sin_port = htons(port);
	he = gethostbyname(addr);
	if (he)
		si.sin_addr.s_addr = *((unsigned long *)(he->h_addr));
	else if (inet_aton(addr, &(si.sin_addr)) < 0) {
		fprintf(stderr, "Cannot resolve hostname");
		return -1;
	}

	servsock = socket(PF_INET, SOCK_STREAM, 0);
	if (servsock == -1) {
		perror("Cannot create socket");
		return -1;
	}
	if (connect(servsock, (struct sockaddr *)&si, sizeof(si)) < 0) {
		perror("Cannot connect");
		close(servsock);
		return -1;
	}
	sprintf(vmsg, rfbProtocolVersionFormat, rfbProtocolMajorVersion, rfbProtocolMinorVersion);
	write(servsock, vmsg, sz_rfbProtocolVersionMsg);
	read(servsock, vmsg, sz_rfbProtocolVersionMsg);

	i32 = rfbConnFailed;
	read(servsock, &i32, sizeof(i32));
	i32 = ntohl(i32);

	switch(i32) {
		case rfbConnFailed: /* conn failed */
			puts("Remote server says: Connection failed");
			i32 = 0;
			read(servsock, &i32, sizeof(i32));
			while (i32-- && (read(servsock, &x, sizeof(x))==sizeof(x)))
				printf ("%c", x);
			puts("");
			close(servsock);
			return -1;
		case rfbNoAuth:
			break;
		case rfbVncAuth:
			puts ("We don't support DES yet");
			close(servsock);
			return -1;
#if 0
			p = getpass("Enter password: ");
			if (!p) {
				close(servsock);
				return -1;
			}
#endif
	}

	/* ClientInitialisation */
	clientinit.shared = 1; /* share */
	write(servsock, &clientinit, sizeof(clientinit));

	read(servsock, &serverinit, sizeof(serverinit));

	fb_init();
	cols = ntohs(serverinit.framebufferWidth);
	if (cols > fb_cols())
		cols = fb_cols();
	rows = ntohs(serverinit.framebufferHeight);
	if (rows > fb_rows())
		rows = fb_rows();

	i32 = ntohl(serverinit.nameLength);
	for (i=0; i<i32; i++)
		read(servsock, &x, 1);
	
	pixformmsg.type = rfbSetPixelFormat;
	pixformmsg.format.bitsPerPixel = 8;
	pixformmsg.format.depth = 8;
	pixformmsg.format.bigEndian = 0; /* don't care */
	pixformmsg.format.trueColour = 1;

	pixformmsg.format.redMax = htons(3);
	pixformmsg.format.greenMax = htons(7);
	pixformmsg.format.blueMax = htons(7);

	pixformmsg.format.redShift = 0;
	pixformmsg.format.greenShift = 2;
	pixformmsg.format.blueShift = 5;

	write(servsock, &pixformmsg, sizeof(pixformmsg));

	encodingsmsgp->type = rfbSetEncodings;
	encodingsmsgp->nEncodings = htons(1);
	*((CARD32 *)((char *) encodingsmsgp +
		sizeof(rfbSetEncodingsMsg))) = htonl(rfbEncodingRaw);
	write(servsock, encodingsmsgp, sizeof(*encodingsmsgp)+sizeof(CARD32));
	return servsock;
}

int vncproto_free(void)
{
	fb_free();
	return 0;
}

int request_vnc_refresh(int fd, int inc)
{
	rfbFramebufferUpdateRequestMsg updreq;
	updreq.type = rfbFramebufferUpdateRequest;
	updreq.incremental = inc;
	updreq.x = htons(0);
	updreq.y = htons(0);
	updreq.w = htons(cols);
	updreq.h = htons(rows);
	write(fd, &updreq, sizeof(updreq));
	return 0;
}

static void update_fb(CARD8 *buffer, rfbRectangle r)
{
	fbval_t slice[1 << 14];
	int i, j;
	for (i = 0; i < r.h; i++) {
		for (j = 0; j < r.w; j++) {
			unsigned char *p = &buffer[i * r.w + j];
			slice[j] = fb_color(*p, *p, *p);
		}
		fb_set(r.y + i, r.x, slice, r.w);
	}
}

int must_redraw(void)
{
	int ret = redraw;
	redraw = 0;
	return ret;
}

int parse_vnc_in(int fd)
{
	rfbFramebufferUpdateRectHeader uprect;
	rfbServerToClientMsg msg;
	rfbServerToClientMsg * vomsgp = &msg;
	int i, j, k;

	i = read (fd, vomsgp, sizeof(CARD8));
	if (i != sizeof(CARD8))
		return -1;
	switch (vomsgp->type) {
		case rfbFramebufferUpdate:
			i = read(fd, (char *) vomsgp + sizeof(CARD8),
				sizeof(rfbFramebufferUpdateMsg) - sizeof(CARD8));
			break;
		case rfbBell:
			i = read(fd, (char *) vomsgp + sizeof(CARD8),
				sizeof(rfbBellMsg) - sizeof(CARD8));
			break;
		case rfbServerCutText:
			i = read(fd, (char *) vomsgp + sizeof(CARD8),
				sizeof(rfbServerCutTextMsg) - sizeof(CARD8));
			break;
		default:
			return -1;
	}
	switch (vomsgp->type) {
		case rfbBell:
			break;

		case rfbFramebufferUpdate:
			vomsgp->fu.nRects = ntohs(vomsgp->fu.nRects);
			for (k=0; k<vomsgp->fu.nRects; k++) {
				i = read(fd, &uprect, sizeof(uprect));
				if (i != sizeof(uprect))
					return -1;
				uprect.r.x = ntohs(uprect.r.x);
				uprect.r.y = ntohs(uprect.r.y);
				uprect.r.w = ntohs(uprect.r.w);
				uprect.r.h = ntohs(uprect.r.h);
				if (uprect.r.x >= cols)
					return -1;
				if (uprect.r.x + uprect.r.w > cols)
					return -1;
				if (uprect.r.y >= rows)
					return -1;
				if (uprect.r.y + uprect.r.h > rows)
					return -1;
				for (i=0; i < uprect.r.w * uprect.r.h;) {
					j = read(fd, updates + i,
						uprect.r.w * uprect.r.h - i);
					if (j == -1)
						return 0;
					i+=j;
				}
				update_fb(updates, uprect.r);
			}
			break;
	}
	return 0;
}

static int mr, mc;		/* mouse position */
static int cmd;			/* command mode */
static char mk[] = MOUSEKEYS;

static void handle_mouse(int fd, int c)
{
	rfbPointerEventMsg me = {rfbPointerEvent, 0, 0, 0};
	CARD8 mask = 0;
	switch (strchr(mk, c) - mk) {
	case 0:
		mc -= MOUSESPEED;
		break;
	case 1:
		mr += MOUSESPEED;
		break;
	case 2:
		mr -= MOUSESPEED;
		break;
	case 3:
		mc += MOUSESPEED;
		break;
	case 4:
		mask = rfbButton1Mask;
		break;
	case 5:
		mask = rfbButton2Mask;
		break;
	case 6:
		mask = rfbButton3Mask;
		break;
	}
	me.y = htons(MAX(0, MIN(rows - 1, mr)));
	me.x = htons(MAX(0, MIN(cols - 1, mc)));
	write(fd, &me, sizeof(me));
	if (mask) {
		me.buttonMask = mask;
		write(fd, &me, sizeof(me));
		me.buttonMask = 0;
		write(fd, &me, sizeof(me));
	}
}

static int press(int fd, int key, int down)
{
	rfbKeyEventMsg ke = {rfbKeyEvent};
	ke.key = htonl(key);
	ke.down = down;
	return write(fd, &ke, sizeof(ke));
}

int parse_kbd_in(int kbdfd, int fd)
{
	char buf[1024];
	int i, j;
	int mod = 0;

	if ((j = read(kbdfd, buf, sizeof(buf))) <= 0 )
		return -1;
	for (i = 0; i < j; i++) {
		int k = -1;
		if (!cmd) {
			switch (buf[i]) {
			case '\x08':
			case '\x7f':
				k = 0xff08;
				break;
			case '\x09':
				k = 0xff09;
				break;
			case '\x1b':
				k = 0xff1b;
				break;
			case '\x0d':
				k = 0xff0d;
				break;
			case CMDKEY:
				cmd = 1;
				break;
			default:
				k = (unsigned char) buf[i];
			}
		} else {
			if (strchr(mk, buf[i])) {
				handle_mouse(fd, buf[i]);
				continue;
			}
			switch(buf[i]) {
				case CMDKEY:
					k = (unsigned char) buf[i];
					break;
				case 'q':
				case 'i':
					cmd = 0;
					break;
				case '\x03':	/* cmd ^c */
					return -1;
				case 'r':
				case 'L':
					redraw = 1;
					break;
			}
		}
		if (isupper(k) || strchr(":\"<>?{}|+_()*&^%$#@!~", k))
			mod = 0xffe1;
		if (k >= 1 && k <= 26) {
			k = 'a' + k - 1;
			mod = 0xffe3;
		}
		if (k > 0) {
			if (mod)
				press(fd, mod, 1);
			press(fd, k, 1);
			press(fd, k, 0);
			if (mod)
				press(fd, mod, 0);
		}
	}
	return 0;
}
