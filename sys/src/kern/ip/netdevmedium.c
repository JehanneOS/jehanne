/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"

static void	netdevbind(Ipifc *ifc, int argc, char **argv);
static void	netdevunbind(Ipifc *ifc);
static void	netdevbwrite(Ipifc *ifc, Block *bp, int version, uint8_t *ip);
static void	netdevread(void *a);

typedef struct	Netdevrock Netdevrock;
struct Netdevrock
{
	Fs	*f;		/* file system we belong to */
	Proc	*readp;		/* reading process */
	Chan	*mchan;		/* Data channel */
};

Medium netdevmedium =
{
.name=		"netdev",
.hsize=		0,
.mintu=	0,
.maxtu=	64000,
.maclen=	0,
.bind=		netdevbind,
.unbind=	netdevunbind,
.bwrite=	netdevbwrite,
.unbindonclose=	0,
};

/*
 *  called to bind an IP ifc to a generic network device
 *  called with ifc qlock'd
 */
static void
netdevbind(Ipifc *ifc, int argc, char **argv)
{
	Chan *mchan;
	Netdevrock *er;

	if(argc < 2)
		error(Ebadarg);

	mchan = namec(argv[2], Aopen, ORDWR, 0);

	er = smalloc(sizeof(*er));
	er->mchan = mchan;
	er->f = ifc->conv->p->f;

	ifc->arg = er;

	kproc("netdevread", netdevread, ifc);
}

/*
 *  called with ifc wlock'd
 */
static void
netdevunbind(Ipifc *ifc)
{
	Netdevrock *er = ifc->arg;

	if(er->readp != nil)
		postnote(er->readp, 1, "unbind", 0);

	/* wait for readers to die */
	while(er->readp != nil)
		tsleep(&up->sleep, return0, 0, 300);

	if(er->mchan != nil)
		cclose(er->mchan);

	jehanne_free(er);
}

/*
 *  called by ipoput with a single block to write
 */
static void
netdevbwrite(Ipifc *ifc, Block *bp, int _1, uint8_t* _2)
{
	Netdevrock *er = ifc->arg;

	if(bp->next)
		bp = concatblock(bp);
	if(BLEN(bp) < ifc->mintu)
		bp = adjustblock(bp, ifc->mintu);

	er->mchan->dev->bwrite(er->mchan, bp, 0);
	ifc->out++;
}

/*
 *  process to read from the device
 */
static void
netdevread(void *a)
{
	Ipifc *ifc;
	Block *bp;
	Netdevrock *er;
	char *argv[1];

	ifc = a;
	er = ifc->arg;
	er->readp = up;	/* hide identity under a rock for unbind */
	if(waserror()){
		er->readp = nil;
		pexit("hangup", 1);
	}
	for(;;){
		bp = er->mchan->dev->bread(er->mchan, ifc->maxtu, 0);
		if(bp == nil){
			/*
			 * get here if mchan is a pipe and other side hangs up
			 * clean up this interface & get out
ZZZ is this a good idea?
			 */
			poperror();
			er->readp = nil;
			argv[0] = "unbind";
			if(!waserror())
				ifc->conv->p->ctl(ifc->conv, argv, 1);
			pexit("hangup", 1);
		}
		if(!canrlock(ifc)){
			freeb(bp);
			continue;
		}
		if(waserror()){
			runlock(ifc);
			nexterror();
		}
		ifc->in++;
		if(ifc->lifc == nil)
			freeb(bp);
		else
			ipiput4(er->f, ifc, bp);
		runlock(ifc);
		poperror();
	}
}

void
netdevmediumlink(void)
{
	addipmedium(&netdevmedium);
}
