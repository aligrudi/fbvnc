#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <string.h>

#include "fbvnc.h"
#include "draw.h"

#define MAXRESOL		(1 << 21)

/* vnc part */
static CARD16 cols = 0;
static CARD16 rows = 0;

/* and buffer for screen updates */
static CARD8 updates[MAXRESOL];

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

int request_vnc_refresh(int fd)
{
	rfbFramebufferUpdateRequestMsg updreq;
	static int incremental = 0;

	updreq.type = rfbFramebufferUpdateRequest;
	updreq.incremental = incremental; incremental=1;
	updreq.x = htons(0);
	updreq.y = htons(0);
	updreq.w = htons(cols);
	updreq.h = htons(rows);

	write(fd, &updreq, sizeof(updreq));
	return 0;
}

void update_fb(CARD8 *buffer, rfbRectangle r)
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

int parse_kbd_in(int kbdfd, int fd)
{
	static rfbKeyEventMsg ke;
	static rfbPointerEventMsg me = {rfbPointerEvent, 0, 0, 0};
	static int mouse_on = -1; static int mouse_factor = 1;
#define VT_CHAR	0
#define VT_ESC	1
	static int vt_state=VT_CHAR;
	static int ctrllock_state = 0;
	static int shiftlock_state = 0;
	char buf[1024];
	int i, j, k;

	if (mouse_on == -1) {
		me.x = htons(fb_cols() / 2);
		me.y = htons(fb_rows() / 2);
		mouse_on = 0;
		ke.type = rfbKeyEvent;
	}

	if ( (j=read(kbdfd, buf, sizeof(buf))) <= 0 )
		return -1;
	for (i=0; i<j; i++) {
		k = -1;
		switch(vt_state) {
			case VT_CHAR:
				switch(buf[i]) {
					case '\x08': k = 0xFF08; break;
					case '\x09': k = 0xFF09; break;
					case '\x0d': if (!mouse_on) {
							k = 0xFF0D; break;
							}
					case '1': case '2': case '3':
					case '4': case '5': case '6':
					case '7': case '8': case '9':
					case '0': case '.': case '-':
					case ',':
					if (!mouse_on) {
						k = (unsigned char)buf[i];
						break;
					}
					if (buf[i] == '5') {
						mouse_factor <<= 1;
						if (mouse_factor > 64)
							mouse_factor = 1;
						break;
					}
					if (buf[i] == '4' || buf[i] == '7' || buf[i] == '1') {
						if (ntohs(me.x)>mouse_factor)
							me.x = htons(ntohs(me.x)-mouse_factor);
						else
							me.x = htons(0);
					}
					if (buf[i] == '6' || buf[i] == '9' || buf[i] == '3') {
						if (ntohs(me.x)+mouse_factor < fb_cols()-1)
							me.x = htons(ntohs(me.x)+mouse_factor);
						else
							me.x = htons(fb_cols()-1);
					}
					if (buf[i] == '7' || buf[i] == '8' || buf[i] == '9') {
						if (ntohs(me.y)>mouse_factor)
							me.y = htons(ntohs(me.y)-mouse_factor);
						else
							me.y = htons(0);
					}
					if (buf[i] == '1' || buf[i] == '2' || buf[i] == '3') {
						if (ntohs(me.y)+mouse_factor < fb_rows()-1)
							me.y = htons(ntohs(me.y)+mouse_factor);
						else
							me.y = htons(fb_rows()-1);
					}

					if (buf[i]>='1' && buf[i]<='9') {
						write(fd, &me, sizeof(me));
						break;
					}
					if (buf[i]=='-') {
						me.buttonMask = me.buttonMask ^ rfbButton1Mask;
						write(fd, &me, sizeof(me));
					}
					if (buf[i]==',') {
						me.buttonMask = me.buttonMask ^ rfbButton2Mask;
						write(fd, &me, sizeof(me));
					}
					if (buf[i]=='\x0d') {
						me.buttonMask = me.buttonMask ^ rfbButton3Mask;
						write(fd, &me, sizeof(me));
					}
					if (buf[i]=='0') {
						me.buttonMask = me.buttonMask ^ rfbButton1Mask;
						write(fd, &me, sizeof(me));
						me.buttonMask = me.buttonMask ^ rfbButton1Mask;
						write(fd, &me, sizeof(me));
					}
					if (buf[i]=='.') {
						me.buttonMask = me.buttonMask ^ rfbButton2Mask;
						write(fd, &me, sizeof(me));
						me.buttonMask = me.buttonMask ^ rfbButton2Mask;
						write(fd, &me, sizeof(me));
					}
					break;
					case '\x1b': vt_state = VT_ESC; break;
					default: k = (unsigned char)buf[i];
				}
				break;
			case VT_ESC:
				switch(buf[i]) {
					case '\x1b': k = (unsigned char)buf[i];
							break;
					case '\x03': /* esc ^c */
						return -1;
					case 'D': k = 0xFF51; break;
					case 'A': k = 0xFF52; break;
					case 'C': k = 0xFF53; break;
					case 'B': k = 0xFF54; break;
					case 'P': /* mouse lock */
						mouse_on = !mouse_on;
						break;
					case 'Q': /* control lock */
						ctrllock_state ^= 1;
						ke.down = ctrllock_state;
						ke.key = 0xFFE3;
						write(fd, &ke, sizeof(ke));
						break;
					case 'R': /* shift lock */
						shiftlock_state ^= 1;
						ke.down = shiftlock_state;
						ke.key = 0xFFE1;
						write(fd, &ke, sizeof(ke));
						break;
					default: k = -1; break;
				}
				vt_state=VT_CHAR; break;
			default:
				break;
		}
		if (k>0) {
			ke.down = 1;
			ke.key = htonl(k);
			write(fd, &ke, sizeof(ke));
			ke.down = 0;
			write(fd, &ke, sizeof(ke));
		}
	}
	return 0;
}
