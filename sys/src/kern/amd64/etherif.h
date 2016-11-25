enum
{
	Eaddrlen	= 6,
	ETHERMINTU	= 60,		/* minimum transmit size */
	ETHERMAXTU	= 1514,		/* maximum transmit size */
	ETHERHDRSIZE	= 14,		/* size of an ethernet header */

	MaxEther	= 48,
	Ntypes		= 16,

	/* ethernet packet types */
	ETARP		= 0x0806,
	ETIP4		= 0x0800,
	ETIP6		= 0x86DD,
};

typedef struct Ether Ether;
struct Ether {
	ISAConf;			/* hardware info */

	int	ctlrno;
	int	minmtu;
	int 	maxmtu;
	uint8_t	ea[Eaddrlen];

	void	(*attach)(Ether*);	/* filled in by reset routine */
	void	(*detach)(Ether*);
	void	(*transmit)(Ether*);
	void	(*interrupt)(Ureg*, void*);
	long	(*ifstat)(Ether*, void*, long, uint32_t);
	long 	(*ctl)(Ether*, void*, long); /* custom ctl messages */
	void	(*power)(Ether*, int);	/* power on/off */
	void	(*shutdown)(Ether*);	/* shutdown hardware before reboot */
	void	*ctlr;
	void	*vector;

	int	scan[Ntypes];		/* base station scanning interval */
	int	nscan;			/* number of base station scanners */

	Netif	netif;
};

typedef struct Etherpkt Etherpkt;
struct Etherpkt
{
	uint8_t	d[Eaddrlen];
	uint8_t	s[Eaddrlen];
	uint8_t	type[2];
	uint8_t	data[1500];
};

extern Block* etheriq(Ether*, Block*, int);
extern void addethercard(char*, int(*)(Ether*));
extern uint32_t ethercrc(uint8_t*, int);
extern int parseether(uint8_t*, char*);
extern int ethercfgmatch(Ether*, Pcidev*, uintmem);

#define NEXT(x, l)	(((x)+1)%(l))
#define PREV(x, l)	(((x) == 0) ? (l)-1: (x)-1)
