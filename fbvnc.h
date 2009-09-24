#ifndef _VT52VNC_H
#define _VT52VNC_H

#include "rfbproto.h"

#define DEF_PORT 5900

/* vncproto.c */
int vncproto_init(char *addr, int port);
int vncproto_free(void);
int request_vnc_refresh(int fd, int inc);
int parse_vnc_in(int fd);
int parse_kbd_in(int kbdfd, int fd);
int must_redraw(void);

#endif
