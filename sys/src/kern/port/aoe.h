/*
 * Copyright © 2011 Coraid, Inc.
 * All rights reserved.
 */
/* Portions of this file are Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

typedef struct Aoehdr Aoehdr;
typedef struct Aoeata Aoeata;
typedef struct Aoeqc Aoeqc;
typedef struct Mdir Mdir;
typedef struct Aoemask Aoemask;
typedef struct Aoesrr Aoesrr;
typedef struct Aoekrr Aoekrr;
typedef struct Kresp Kresp;
typedef struct Kreg Kreg;
typedef struct Kset Kset;
typedef struct Kreplace Kreplace;

enum
{
	ACata, ACconfig, ACmask, ACresrel, ACkresrel,

	AQCread= 0, AQCtest, AQCprefix, AQCset, AQCfset, AQCtar,

	ETAOE= 0x88a2,
	Aoever= 1,

	AEcmd= 1, AEarg, AEdev, AEcfg, AEver, AEres,

	AFerr= 1<<2,
	AFrsp= 1<<3,

	AAFwrite= 1,
	AAFext= 1<<6,

	AKstat = 0, AKreg, AKset, AKreplace, AKreset,

	Aoesectsz = 512,
	Szaoeata	= 24+12,
	Szaoeqc	= 24+8,

	/* mask commands */
	Mread= 0,	
	Medit,

	/* mask directives */
	MDnop= 0,
	MDadd,
	MDdel,

	/* mask errors */
	MEunspec= 1,
	MEbaddir,
	MEfull,

	/* Keyed-RR Rflags */
	KRnopreempt = 1<<0,
};

struct Aoehdr
{
	uint8_t dst[6];
	uint8_t src[6];
	uint8_t type[2];
	uint8_t verflags;
	uint8_t error;
	uint8_t major[2];
	uint8_t minor;
	uint8_t cmd;
	uint8_t tag[4];
};

struct Aoeata
{
	Aoehdr;
	uint8_t aflags;
	uint8_t errfeat;
	uint8_t scnt;
	uint8_t cmdstat;
	uint8_t lba[6];
	uint8_t res[2];
};

struct Aoeqc
{
	Aoehdr;
	uint8_t bufcnt[2];
	uint8_t fwver[2];
	uint8_t scnt;
	uint8_t verccmd;
	uint8_t cslen[2];
};

// mask directive
struct Mdir {
	uint8_t res;
	uint8_t cmd;
	uint8_t mac[6];
};

struct Aoemask {
	Aoehdr;
	uint8_t rid;
	uint8_t cmd;
	uint8_t merror;
	uint8_t nmacs;
//	struct Mdir m[0];
};

struct Aoesrr {
	Aoehdr;
	uint8_t rcmd;
	uint8_t nmacs;
//	uint8_t mac[6][nmacs];
};

struct Aoekrr {
	Aoehdr; 
	uint8_t rcmd;
};

struct Kresp {
	Aoehdr;
	uint8_t rcmd;
	uint8_t rtype;
	uint8_t nkeys[2];
	uint8_t res[4];
	uint8_t gencnt[4];
	uint8_t owner[8];
	uint8_t keys[1];
};

struct Kreg {
	Aoehdr;
	uint8_t rcmd;
	uint8_t nmacs;
	uint8_t res[2];
	uint8_t key[8];
	uint8_t macs[1];
};

struct Kset {
	Aoehdr;
	uint8_t rcmd;
	uint8_t rtype;
	uint8_t res[2];
	uint8_t key[8];
};

struct Kreplace {
	Aoehdr;
	uint8_t rcmd;
	uint8_t rtype;
	uint8_t rflags;
	uint8_t res;
	uint8_t targkey[8];
	uint8_t replkey[8];
};
