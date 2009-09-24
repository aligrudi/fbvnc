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

#endif
