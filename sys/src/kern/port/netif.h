typedef struct Netaddr	Netaddr;
typedef struct Netfile	Netfile;
typedef struct Netif	Netif;

enum
{
	Nmaxaddr=	64,
	Nmhash=		31,
	Niqlim=		128,
	Noqlim=		4096,

	Ncloneqid=	1,
	Naddrqid,
	N2ndqid,
	N3rdqid,
	Ndataqid,
	Nctlqid,
	Nstatqid,
	N3statqid,
	Ntypeqid,
	Nifstatqid,
	Nmtuqid,
	Nmaxmtuqid,
};

/*
 *  Macros to manage Qid's used for multiplexed devices
 */
#define NETTYPE(x)	(((uint32_t)x)&0x1f)
#define NETID(x)	((((uint32_t)x))>>5)
#define NETQID(i,t)	((((uint32_t)i)<<5)|(t))

/*
 *  one per multiplexed connection
 */
struct Netfile
{
	QLock;

	int	inuse;
	uint32_t	mode;
	char	owner[KNAMELEN];

	int	type;			/* multiplexor type */
	int	prom;			/* promiscuous mode */
	int	scan;			/* base station scanning interval */
	int	bridge;			/* bridge mode */
	int	vlan;			/* treat 802.1Q frames as inner frame type*/
	int	headersonly;		/* headers only - no data */
	uint8_t	maddr[8];		/* bitmask of multicast addresses requested */
	int	nmaddr;			/* number of multicast addresses */
	int	fat;			/* frame at a time */

	uint32_t	inoverflows;		/* software input overflows */

	Queue*	iq;			/* input */
};

/*
 *  a network address
 */
struct Netaddr
{
	Netaddr	*next;			/* allocation chain */
	Netaddr	*hnext;
	uint8_t	addr[Nmaxaddr];
	int	ref;
};

/*
 *  a network interface
 */
struct Netif
{
	QLock;

	int	inited;

	/* multiplexing */
	char	name[KNAMELEN];		/* for top level directory */
	int	nfile;			/* max number of Netfiles */
	Netfile	**f;

	/* about net */
	int	limit;			/* flow control */
	int	alen;			/* address length */
	int	mbps;			/* megabits per sec */
	int	link;			/* link status */
	int	minmtu;
	int 	maxmtu;
	int	mtu;
	uint8_t	addr[Nmaxaddr];
	uint8_t	bcast[Nmaxaddr];
	Netaddr	*maddr;			/* known multicast addresses */
	int	nmaddr;			/* number of known multicast addresses */
	Netaddr *mhash[Nmhash];		/* hash table of multicast addresses */
	int	prom;			/* number of promiscuous opens */
	int	scan;			/* number of base station scanners */
	int	all;			/* number of -1 multiplexors */

	Queue*	oq;			/* output */

	/* statistics */
	uint32_t	misses;
	uint64_t	inpackets;
	uint64_t	outpackets;
	uint32_t	crcs;			/* input crc errors */
	uint32_t	oerrs;			/* output errors */
	uint32_t	frames;			/* framing errors */
	uint32_t	overflows;		/* packet overflows */
	uint32_t	buffs;			/* buffering errors */
	uint32_t	inoverflows;	/* software overflow on input */
	uint32_t	outoverflows;	/* software overflow on output */
	uint32_t	loopbacks;	/* loopback packets processed */

	/* routines for touching the hardware */
	void	*arg;
	void	(*promiscuous)(void*, int);
	void	(*multicast)(void*, uint8_t*, int);
	int	(*hwmtu)(void*, int);	/* get/set mtu */
	void	(*scanbs)(void*, uint32_t);	/* scan for base stations */
};

void	netifinit(Netif*, char*, int, uint32_t);
Walkqid*	netifwalk(Netif*, Chan*, Chan*, char **, int);
Chan*	netifopen(Netif*, Chan*, int);
void	netifclose(Netif*, Chan*);
long	netifread(Netif*, Chan*, void*, long, int64_t);
Block*	netifbread(Netif*, Chan*, long, int64_t);
long	netifwrite(Netif*, Chan*, void*, long);
long	netifwstat(Netif*, Chan*, uint8_t*, long);
long	netifstat(Netif*, Chan*, uint8_t*, long);
int	activemulti(Netif*, uint8_t*, int);
