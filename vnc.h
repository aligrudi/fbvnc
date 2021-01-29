#define VNC_CONN_FAILED		0
#define VNC_CONN_NOAUTH		1
#define VNC_CONN_AUTH		2

#define VNC_AUTH_OK		0
#define VNC_AUTH_FAILED		1
#define VNC_AUTH_TOOMANY	2

#define VNC_UPDATE		0
#define VNC_SERVERCOLORMAP	1
#define VNC_BELL		2
#define VNC_SERVERCUTTEXT	3

#define VNC_SETPIXELFORMAT	0
#define VNC_SETCOLORMAPENTRIES	1
#define VNC_SETENCODING		2
#define VNC_UPDATEREQUEST	3
#define VNC_KEYEVENT		4
#define VNC_POINTEREVENT	5
#define VNC_CLIENTCUTTEXT	6

#define VNC_ENC_RAW		0
#define VNC_ENC_COPYRECT	1
#define VNC_ENC_RRE		2
#define VNC_ENC_CORRE		4
#define VNC_ENC_HEXTILE		5
#define VNC_ENC_ZLIB		6
#define VNC_ENC_TIGHT		7
#define VNC_ENC_ZLIBHEX		8
#define VNC_ENC_ZRLE		16

#define VNC_BUTTON1_MASK	0x01
#define VNC_BUTTON2_MASK	0x02
#define VNC_BUTTON3_MASK	0x04
#define VNC_BUTTON4_MASK	0x10
#define VNC_BUTTON5_MASK	0x08

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct vnc_pixelformat {
	u8 bpp;
	u8 depth;
	u8 bigendian;
	u8 truecolor;
	u16 rmax;
	u16 gmax;
	u16 bmax;
	u8 rshl;
	u8 gshl;
	u8 bshl;
	u8 pad1;
	u16 pad2;
};

struct vnc_clientinit {
	u8 shared;
};

struct vnc_serverinit {
	u16 w;
	u16 h;
	struct vnc_pixelformat fmt;
	u32 len;
	/* char name[len]; */
};

struct vnc_rect {
	u16 x, y;
	u16 w, h;
	u32 enc;
	/* rect bytes */
};

struct vnc_update {
	u8 type;
	u8 pad;
	u16 n;
	/* struct vnc_rect rects[n]; */
};

struct vnc_servercuttext {
	u8 type;
	u8 pad1;
	u16 pad2;
	u32 len;
	/* char text[length] */
};

struct vnc_setcolormapentries {
	u8 type;
	u8 pad;
	u16 first;
	u16 n;
	/* u8 colors[n * 3 * 2]; */
};

struct vnc_setpixelformat {
	u8 type;
	u8 pad1;
	u16 pad2;
	struct vnc_pixelformat format;
};

struct vnc_setencoding {
	u8 type;
	u8 pad;
	u16 n;
	/* s32[n] */
};

struct vnc_updaterequest {
	u8 type;
	u8 inc;
	u16 x;
	u16 y;
	u16 w;
	u16 h;
};

struct vnc_keyevent {
	u8 type;
	u8 down;
	u16 pad;
	u32 key;
};

struct vnc_pointerevent {
	u8 type;
	u8 mask;
	u16 x;
	u16 y;
};
