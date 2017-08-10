typedef struct Cursor Cursor;
typedef struct Cursorinfo Cursorinfo;
struct Cursorinfo {
	Cursor;
	Lock;
};

/* devmouse.c */
extern void mousetrack(int, int, int, uint32_t);
extern void absmousetrack(int, int, int, uint32_t);
extern Point mousexy(void);

extern void mouseaccelerate(int);
extern int m3mouseputc(Queue*, int);
extern int m5mouseputc(Queue*, int);
extern int mouseputc(Queue*, int);

extern Cursorinfo cursor;
extern Cursor arrow;

/*
 * Generic VGA registers.
 */
enum {
	MiscW		= 0x03C2,	/* Miscellaneous Output (W) */
	MiscR		= 0x03CC,	/* Miscellaneous Output (R) */
	Status0		= 0x03C2,	/* Input status 0 (R) */
	Status1		= 0x03DA,	/* Input Status 1 (R) */
	FeatureR	= 0x03CA,	/* Feature Control (R) */
	FeatureW	= 0x03DA,	/* Feature Control (W) */

	Seqx		= 0x03C4,	/* Sequencer Index, Data at Seqx+1 */
	Crtx		= 0x03D4,	/* CRT Controller Index, Data at Crtx+1 */
	Grx		= 0x03CE,	/* Graphics Controller Index, Data at Grx+1 */
	Attrx		= 0x03C0,	/* Attribute Controller Index and Data */

	PaddrW		= 0x03C8,	/* Palette Address Register, write */
	Pdata		= 0x03C9,	/* Palette Data Register */
	Pixmask		= 0x03C6,	/* Pixel Mask Register */
	PaddrR		= 0x03C7,	/* Palette Address Register, read */
	Pstatus		= 0x03C7,	/* DAC Status (RO) */

	Pcolours	= 256,		/* Palette */
	Pred		= 0,
	Pgreen		= 1,
	Pblue		= 2,

	Pblack		= 0x00,
	Pwhite		= 0xFF,
};

#define VGAMEM()	0xA0000
#define vgai(port)		inb(port)
#define vgao(port, data)	outb(port, data)

extern int vgaxi(int32_t port, uint8_t index);
extern int vgaxo(int32_t port, uint8_t index, uint8_t data);

/*
 */
typedef struct VGAdev VGAdev;
typedef struct VGAcur VGAcur;
typedef struct VGAscr VGAscr;

struct VGAdev {
	char*	name;

	void	(*enable)(VGAscr*);
	void	(*disable)(VGAscr*);
	void	(*page)(VGAscr*, int);
	void	(*linear)(VGAscr*, int, int);
	void	(*drawinit)(VGAscr*);
	int	(*fill)(VGAscr*, Rectangle, uint32_t);
	void	(*ovlctl)(VGAscr*, Chan*, void*, int);
	int	(*ovlwrite)(VGAscr*, void*, int, int64_t);
	void (*flush)(VGAscr*, Rectangle);
};

struct VGAcur {
	char*	name;

	void	(*enable)(VGAscr*);
	void	(*disable)(VGAscr*);
	void	(*load)(VGAscr*, Cursor*);
	int	(*move)(VGAscr*, Point);

	int	doespanning;
};

/*
 */
struct VGAscr {
	Lock	devlock;
	VGAdev*	dev;
	Pcidev*	pci;

	VGAcur*	cur;
	uintptr_t	storage;
	Cursor;

	int	useflush;

	uintptr_t	paddr;		/* frame buffer */
	void*	vaddr;
	int	apsize;

	uint32_t	io;		/* device specific registers */
	uint32_t	*mmio;
	
	uint32_t	colormap[Pcolours][3];
	int	palettedepth;

	Memimage* gscreen;
	Memdata* gscreendata;
	Memsubfont* memdefont;

	int	(*fill)(VGAscr*, Rectangle, uint32_t);
	int	(*scroll)(VGAscr*, Rectangle, Rectangle);
	void	(*blank)(VGAscr*, int);
	uint32_t	id;	/* internal identifier for driver use */
	int	overlayinit;
	int	softscreen;
};

extern VGAscr vgascreen[];

enum {
	Backgnd		= 0,	/* black */
};

/* mouse.c */
extern void	mousectl(Cmdbuf*);
extern void	mouseresize(void);
extern void	mouseredraw(void);

/* screen.c */
extern int		hwaccel;	/* use hw acceleration */
extern int		hwblank;	/* use hw blanking */
extern int		panning;	/* use virtual screen panning */
extern void addvgaseg(char*, uint32_t, uint32_t);
extern uint8_t* attachscreen(Rectangle*, uint32_t*, int*, int*, int*);
extern void	flushmemscreen(Rectangle);
extern void	cursoron(void);
extern void	cursoroff(void);
extern void	setcursor(Cursor*);
extern int	screensize(int, int, int, uint32_t);
extern int	screenaperture(int, int);
extern Rectangle physgscreenr;	/* actual monitor size */
extern void	blankscreen(int);
extern char*	rgbmask2chan(char *buf, int depth, uint32_t rm, uint32_t gm, uint32_t bm);

extern void	bootscreeninit(void);
extern void	bootscreenconf(VGAscr*);

extern VGAcur swcursor;
extern void swcursorinit(void);
extern void swcursorhide(void);
extern void swcursoravoid(Rectangle);
extern void swcursorunhide(void);

/* devdraw.c */
extern void	deletescreenimage(void);
extern void	resetscreenimage(void);
extern int		drawhasclients(void);
extern void	setscreenimageclipr(Rectangle);
extern void	drawflush(void);
extern QLock	drawlock;

/* vga.c */
extern void	vgascreenwin(VGAscr*);
extern void	vgaimageinit(uint32_t);
extern void	vgalinearpci(VGAscr*);
extern void	vgalinearaddr(VGAscr*, uint32_t, int);

extern void	vgablank(VGAscr*, int);

extern Lock	vgascreenlock;

#define ishwimage(i)	(vgascreen[0].gscreendata && (i)->data->bdata == vgascreen[0].gscreendata->bdata)

/* swcursor.c */
void		swcursorhide(void);
void		swcursoravoid(Rectangle);
void		swcursordraw(Point);
void		swcursorload(Cursor *);
void		swcursorinit(void);
