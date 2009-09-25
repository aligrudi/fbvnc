/* fbvnc config file */

/* escape key: inserts vnc mode */
#define ESCKEY		'\x1b'
/* mouse keys: left, down, up, right, button1, button2, button3 */
#define MOUSEKEYS	"hjkl\r \t"
/* mouse movements in pixels */
#define MOUSESPEED	4

/* framebuffer color depth */
typedef unsigned short fbval_t;
/* framebuffer device path */
#define FBDEV_PATH	"/dev/fb0"
