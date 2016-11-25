typedef enum PTevent {
	SAdmit = 0,	/* Edf admit */
	SRelease,	/* Edf release, waiting to be scheduled */
	SEdf,		/* running under EDF */
	SRun,		/* running best effort */

	SReady,		/* runnable but not running  */
	SSleep,		/* blocked; arg is PSstate| pc<<8  */
	SYield,		/* blocked waiting for release */
	SSlice,		/* slice exhausted */

	SDeadline,	/* proc's deadline */
	SExpel,		/* Edf expel */
	SDead,		/* proc dies */
	SInts,		/* Interrupt start */

	SInte,		/* Interrupt end */
	STrap,		/* fault */
	SUser,		/* user event */
	SName,		/* used to report names for pids */
	Nevent,
} Tevent;

enum {
	PTsize = 4 + 4 + 4 + 8 + 8,

	/* STrap arg flags */
	STrapRPF = 0x1000000000000000ULL,	/* page fault (read) STrap arg */
	STrapWPF = 0x1000000000000000ULL,	/* page fault (write) STrap arg */
	STrapSC  = 0x2000000000000000ULL,	/* sys call STrap arg */
	STrapMask = 0x0FFFFFFFFFFFFFFFULL,	/* bits available in arg */

	/* Sleep states; keep in sync with the kernel schedstate
	 * BUG: generate automatically.
	 */
	PSDead = 0,		/* not used */
	PSMoribund,		/* not used */
	PSReady,		/* not used */
	PSScheding,		/* not used */
	PSRunning,		/* not used */
	PSQueueing,
	PSQueueingR,
	PSQueueingW,
	PSWakeme,
	PSBroken,		/* not used */
	PSStopped,		/* not used */
	PSRendezvous,
	PSWaitrelease,

};

typedef struct PTraceevent	PTraceevent;
struct PTraceevent {
	uint32_t	pid;	/* for the process */
	uint32_t	etype;	/* Event type */
	uint32_t	machno;	/* where the event happen */
	int64_t		time;	/* time stamp  */
	uint64_t	arg;	/* for this event type */
};
