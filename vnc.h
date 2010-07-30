#define VNC_CONN_FAILED		0
#define VNC_CONN_NOAUTH		1
#define VNC_CONN_AUTH		2

#define VNC_AUTH_OK		0
#define VNC_AUTH_FAILED		1
#define VNC_AUTH_TOOMANY	2

#define VNC_SERVER_FBUP		0
#define VNC_SERVER_COLORMAP	1
#define VNC_SERVER_BELL		2
#define VNC_SERVER_CUTTEXT	3

#define VNC_CLIENT_PIXFMT	0
#define VNC_CLIENT_COLORMAP	1
#define VNC_CLIENT_SETENC	2
#define VNC_CLIENT_FBUP		3
#define VNC_CLIENT_KEYEVENT	4
#define VNC_CLIENT_RATEVENT	5
#define VNC_CLIENT_CUTTEXT	6

#define VNC_ENC_RAW		0
#define VNC_ENC_COPYRECT	1
#define VNC_ENC_RRE		2
#define VNC_ENC_CORRE		4
#define VNC_ENC_HEXTILE		5

#define VNC_BUTTON1_MASK	0x1
#define VNC_BUTTON2_MASK	0x2
#define VNC_BUTTON3_MASK	0x4

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct vnc_pixelfmt {
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

struct vnc_client_init {
	u8 shared;
};

struct vnc_server_init {
    u16 w;
    u16 h;
    struct vnc_pixelfmt fmt;
    u32 len;
    /* char name[len]; */
};

struct vnc_rect {
	u16 x, y;
	u16 w, h;
	u32 enc;
	/* rect bytes */
};

struct vnc_server_fbup {
    u8 type;
    u8 pad;
    u16 n;
    /* struct vnc_rect rects[n]; */
};

struct vnc_server_cuttext {
	u8 type;
	u8 pad1;
	u16 pad2;
	u32 len;
	/* char text[length] */
};

struct vnc_server_colormap {
	u8 type;
	u8 pad;
	u16 first;
	u16 n;
	/* u8 colors[n * 3 * 2]; */
};

struct vnc_client_pixelfmt {
	u8 type;
	u8 pad1;
	u16 pad2;
	struct vnc_pixelfmt format;
};

struct vnc_client_fbup {
	u8 type;
	u8 inc;
	u16 x;
	u16 y;
	u16 w;
	u16 h;
};

struct vnc_client_keyevent {
	u8 type;
	u8 down;
	u16 pad;
	u32 key;
};

struct vnc_client_ratevent {
	u8 type;
	u8 mask;
	u16 x;
	u16 y;
};
