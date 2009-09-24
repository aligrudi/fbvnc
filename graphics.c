/* Graficke funkcie pre vdt52s */
/* */

#include "parita.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vt52vnc.h"

#define	ESC	0x1b

#define	PAR(x)	((x) | (128-parity_table[(x) & 0x7f]))

#define	HIGH_Y(n)	(PAR(0x20|(n)))
#define	LOW_Y(n)	(PAR(0x60|(n)))
#define	HIGH_X(n)	(PAR(0x20|(n)))
#define	LOW_X(n)	(PAR(0x40|(n)))


unsigned long bytes_sent = 0;

void gflush( t_kanal *k )
{
	write(1,k->outbuf,k->pos);
	bytes_sent+=k->pos;
	k->pos=0;
}
void gputc( t_kanal *k, char c )
{
	k->outbuf[(k->pos)++]=c;
	if( k->pos >=BUFLEN )
		gflush(k);
}
#define	gputcesc(k,c)	{gputc((k),ESC);gputc((k),(c));}

void greset( t_kanal *k )
{
	gputcesc(k,'0');
}

void gcls( t_kanal *k )
{
	gputc(k,13);
	gputc(k,10);
	gputcesc(k,'E');
}

void gset_scaling( t_kanal *k, int xfactor, int yfactor )
{
	gputc(k,0x1d);
	gputc(k,ESC);
	gputc(k,'s');
	gputc(k,PAR(0x40|xfactor));
	gputc(k,PAR(0x40|yfactor));
}

void gset_attrib( t_kanal *k, int attrib )
{
	if( attrib&GA_OR )
		gputcesc(k,0x12)
	else if( attrib&GA_XOR )
		gputcesc(k,0x13)
	if( attrib&GA_DEL )
		gputcesc(k,0x11)
	if( attrib&GA_PATTERN )
		gputcesc(k,0x14)
}

void gsetpos( t_kanal *k, int x, int y )
{
	gputc(k,0x1d);
	gputc(k,HIGH_Y((y>>5)));
	gputc(k,LOW_Y((y&0x1f)));
	gputc(k,HIGH_X((x>>5)));
	gputc(k,LOW_X((x&0x1f)));
}

void gline( t_kanal *k ,int x1,int y1, int x2, int y2 )
{
	gputc(k,0x1d);
	gputc(k,HIGH_Y((y1>>5)));
	gputc(k,LOW_Y((y1&0x1f)));
	gputc(k,HIGH_X((x1>>5)));
	gputc(k,LOW_X((x1&0x1f)));
	gputc(k,HIGH_Y((y2>>5)));
	gputc(k,LOW_Y((y2&0x1f)));
	gputc(k,HIGH_X((x2>>5)));
	gputc(k,LOW_X((x2&0x1f)));
}

void gdrawto( t_kanal *k ,int x,int y )
{
	gputc(k,0x1d);
	gputc(k,7);
	gputc(k,HIGH_Y((y>>5)));
	gputc(k,LOW_Y((y&0x1f)));
	gputc(k,HIGH_X((x>>5)));
	gputc(k,LOW_X((x&0x1f)));
}

void gplot( t_kanal *k ,int x,int y )
{
	gputc(k,0x1d);
	gputc(k,HIGH_Y((y>>5)));
	gputc(k,LOW_Y((y&0x1f)));
	gputc(k,HIGH_X((x>>5)));
	gputc(k,LOW_X((x&0x1f)));
	gputc(k,LOW_X((x&0x1f)));
}

void gpline( t_kanal *k , t_pos *pos, int num )
{ int i;
	gputc(k,0x1d);
	for(i=0;i<num;i++)
	{	gputc(k,HIGH_Y(((pos[i].y)>>5)));
		gputc(k,LOW_Y(((pos[i].y)&0x1f)));
		gputc(k,HIGH_X(((pos[i].x)>>5)));
		gputc(k,LOW_X(((pos[i].x)&0x1f)));
	}
}

unsigned long get_stat(void)
{
	unsigned long tmp = bytes_sent;
	bytes_sent = 0;
	return tmp;
}
