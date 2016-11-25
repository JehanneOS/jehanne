typedef struct Block Block;
struct Block
{
	Ref	ref;

	Block	*next;

	uint8_t	*rp;
	uint8_t	*wp;
	uint8_t	*lim;

	uint8_t	base[];
};

#define BLEN(s)	((s)->wp - (s)->rp)

Block*	allocb(int size);
void	freeb(Block*);
Block*	copyblock(Block*, int);

typedef struct Ehdr Ehdr;
struct Ehdr
{
	uint8_t	d[6];
	uint8_t	s[6];
	uint8_t	type[2];
};

enum {
	Ehdrsz	= 6+6+2,
	Maxpkt	= 2000,
};

enum
{
	Cdcunion = 6,
	Scether = 6,
	Fnether = 15,
};

int debug;
int setmac;

/* to be filled in by *init() */
uint8_t macaddr[6];

void	etheriq(Block*, int wire);

int	(*epreceive)(Dev*);
void	(*eptransmit)(Dev*, Block*);
