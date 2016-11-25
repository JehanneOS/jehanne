typedef struct PmcCtl PmcCtl;
typedef struct PmcCtr PmcCtr;
typedef struct PmcCtlCtrId PmcCtlCtrId;

/*
 * HW performance counters
 */
struct PmcCtl {
	uint32_t coreno;
	int enab;
	int user;
	int os;
	int nodesc;
	char descstr[KNAMELEN];
};

struct PmcCtr{
	int stale;
	Rendez r;
	uint64_t ctr;
	int ctrset;
	PmcCtl;
	int ctlset;
};

enum {
	PmcMaxCtrs = 4,
};

struct PmcCore{
	Lock;
	PmcCtr ctr[PmcMaxCtrs];
};

struct PmcCtlCtrId {
	char portdesc[KNAMELEN];
	char archdesc[KNAMELEN];
};

enum {
	PmcIgn = 0,
	PmcGet = 1,
	PmcSet = 2,
};

enum {
	PmcCtlNullval = 0xdead,
};

extern int pmcnregs(void);
extern void pmcinitctl(PmcCtl*);
extern int pmcsetctl(uint32_t, PmcCtl*, uint32_t);
extern int pmctrans(PmcCtl*);
extern int pmcgetctl(uint32_t, PmcCtl*, uint32_t);
extern int pmcdescstr(char*, int);
extern uint64_t pmcgetctr(uint32_t, uint32_t);
extern int pmcsetctr(uint32_t, uint64_t, uint32_t);

extern void pmcconfigure(void);
