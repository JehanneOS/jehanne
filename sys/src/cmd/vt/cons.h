/*  console state (for consctl) */
typedef struct Consstate	Consstate;
struct Consstate{
	int raw;
	int hold;
	int winch;
};
extern Consstate cs[];

typedef struct Buf	Buf;
struct Buf
{
	int	n;
	char	*s;
	char	b[];
};

#define	INSET	2
#define	BUFS	32
#define	HISTSIZ	(64*1024)	/* number of history characters */
#define BSIZE	(8*1024)

#define	SCROLL	2
#define NEWLINE	1
#define OTHER	0

#define COOKED	0
#define RAW	1

/* text attributes */
enum {
	THighIntensity = 1<<0,
	TUnderline = 1<<1,
	TBlink = 1<<2,
	TReverse = 1<<3,
	TInvisible = 1<<4,
};

struct ttystate {
	int	crnl;
	int	nlcr;
};
extern struct ttystate ttystate[];

struct funckey {
	char	*name;
	char	*sequence;
};
extern struct funckey *fk, *appfk;
extern struct funckey ansifk[], ansiappfk[], vt220fk[];

extern int	x, y, xmax, ymax, olines;
extern int	peekc, attribute;
extern char*	term;

extern void	emulate(void);
extern int	host_avail(void);
extern void	clear(int,int,int,int);
extern void	newline(void);
extern int	get_next_char(void);
extern void	ringbell(void);
extern int	number(Rune *, int *);
extern void	shift(int,int,int,int);
extern void	scroll(int,int,int,int);
extern void	backup(int);
extern void	sendnchars(int, char *);
extern Point	pt(int, int);
extern Point	pos(Point);
extern void	funckey(int);
extern void	drawstring(Rune*, int);

extern int	yscrmin, yscrmax;
extern int	attr;
extern int	defattr;

extern Image *fgcolor;
extern Image *bgcolor;
extern Image *colors[];
extern Image *hicolors[];

extern int cursoron;
extern int nocolor;

extern void setdim(int, int);
extern void mountcons(void);
