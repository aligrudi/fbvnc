#ifndef _VT52VNC_H
#define _VT52VNC_H

#include "rfbproto.h"

#define DEF_PORT 5900

/* vncproto.c */
int vncproto_init(char * addr, int port);
int request_vnc_refresh(int fd);
int parse_vnc_in(int fd);
int parse_kbd_in(int kbdfd, int fd);
int drawdelta(void); /* 0 = all_ok */

/* graphics.c */
#define	BUFLEN	1024
typedef struct	{	char outbuf[BUFLEN];
			int pos;
		} t_kanal;

typedef struct	{	int x;
			int y;
		} t_pos;
        t_kanal k;


#define VT52_XMAX	512
#define VT52_YMAX	256

void gcls( t_kanal *k );
void gputc( t_kanal *k, char c );
void gdrawto( t_kanal *k ,int x,int y );
void gflush( t_kanal *k );
void gline( t_kanal *k ,int x1,int y1, int x2, int y2 );
void gpline( t_kanal *k , t_pos *pos, int num );
void gplot( t_kanal *k ,int x,int y );
void greset( t_kanal *k );
unsigned long get_stat(void);

#define	GA_OR	1
#define	GA_XOR	2
#define	GA_DEL	4
#define	GA_PATTERN	8

void gset_attrib( t_kanal *k, int attrib );
void gset_scaling( t_kanal *k, int xfactor, int yfactor );
void gsetpos( t_kanal *k, int x, int y );

#endif
