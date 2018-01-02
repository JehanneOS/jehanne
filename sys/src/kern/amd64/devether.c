/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
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

#include "io.h"

#include "../port/netif.h"

#include "etherif.h"

static Ether *etherxx[MaxEther];

Chan*
etherattach(Chan *c, Chan *ac, char *spec, int flags)
{
	int ctlrno;
	char *p;
	Chan *chan;

	ctlrno = 0;
	if(spec && *spec){
		ctlrno = jehanne_strtoul(spec, &p, 0);
		if((ctlrno == 0 && p == spec) || *p != 0)
			error(Ebadarg);
		if(ctlrno < 0 || ctlrno >= MaxEther)
			error(Ebadarg);
	}
	if(etherxx[ctlrno] == 0)
		error(Enodev);

	chan = devattach('l', spec);
	if(waserror()){
		chanfree(chan);
		nexterror();
	}
	chan->devno = ctlrno;
	if(etherxx[ctlrno]->attach)
		etherxx[ctlrno]->attach(etherxx[ctlrno]);
	poperror();
	return chan;
}

static Walkqid*
etherwalk(Chan* chan, Chan* nchan, char** name, int nname)
{
	return netifwalk(&etherxx[chan->devno]->netif, chan, nchan, name, nname);
}

static long
etherstat(Chan* chan, uint8_t* dp, long n)
{
	return netifstat(&etherxx[chan->devno]->netif, chan, dp, n);
}

static Chan*
etheropen(Chan* chan, unsigned long omode)
{
	return netifopen(&etherxx[chan->devno]->netif, chan, omode);
}

static Chan*
ethercreate(Chan* _1, char* _2, unsigned long _3, unsigned long _4)
{
	error(Eperm);
	return nil;
}

static void
etherclose(Chan* chan)
{
	netifclose(&etherxx[chan->devno]->netif, chan);
}

static long
etherread(Chan* chan, void* buf, long n, int64_t off)
{
	Ether *ether;
	uint32_t offset = off;

	ether = etherxx[chan->devno];
	if((chan->qid.type & QTDIR) == 0 && ether->ifstat){
		/*
		 * With some controllers it is necessary to reach
		 * into the chip to extract statistics.
		 */
		if(NETTYPE(chan->qid.path) == Nifstatqid)
			return ether->ifstat(ether, buf, n, offset);
		else if(NETTYPE(chan->qid.path) == Nstatqid)
			ether->ifstat(ether, buf, 0, offset);
	}

	return netifread(&ether->netif, chan, buf, n, offset);
}

static Block*
etherbread(Chan* chan, long n, int64_t offset)
{
	return netifbread(&etherxx[chan->devno]->netif, chan, n, offset);
}

static long
etherwstat(Chan* chan, uint8_t* dp, long n)
{
	return netifwstat(&etherxx[chan->devno]->netif, chan, dp, n);
}

static void
etherrtrace(Netfile* f, Etherpkt* pkt, int len)
{
	int i, n;
	Block *bp;

	if(qwindow(f->iq) <= 0)
		return;
	if(len > 58)
		n = 58;
	else
		n = len;
	bp = iallocb(64);
	if(bp == nil)
		return;
	jehanne_memmove(bp->wp, pkt->d, n);
	i = TK2MS(sys->ticks);
	bp->wp[58] = len>>8;
	bp->wp[59] = len;
	bp->wp[60] = i>>24;
	bp->wp[61] = i>>16;
	bp->wp[62] = i>>8;
	bp->wp[63] = i;
	bp->wp += 64;
	qpass(f->iq, bp);
}

Block*
etheriq(Ether* ether, Block* bp, int fromwire)
{
	Etherpkt *pkt;
	uint16_t type;
	int len, multi, tome, fromme;
	Netfile **ep, *f, **fp, *fx;
	Block *xbp;

	ether->netif.inpackets++;

	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	type = (pkt->type[0]<<8)|pkt->type[1];
	fx = 0;
	ep = &ether->netif.f[Ntypes];

	multi = pkt->d[0] & 1;
	/* check for valid multcast addresses */
	if(multi && jehanne_memcmp(pkt->d, ether->netif.bcast, sizeof(pkt->d)) != 0 && ether->netif.prom == 0){
		if(!activemulti(&ether->netif, pkt->d, sizeof(pkt->d))){
			if(fromwire){
				freeb(bp);
				bp = 0;
			}
			return bp;
		}
	}

	/* is it for me? */
	tome = jehanne_memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	fromme = jehanne_memcmp(pkt->s, ether->ea, sizeof(pkt->s)) == 0;

	/*
	 * Multiplex the packet to all the connections which want it.
	 * If the packet is not to be used subsequently (fromwire != 0),
	 * attempt to simply pass it into one of the connections, thereby
	 * saving a copy of the data (usual case hopefully).
	 */
	for(fp = ether->netif.f; fp < ep; fp++){
		if(f = *fp)
		if(f->type == type || f->type < 0)
		if(tome || multi || f->prom){
			/* Don't want to hear bridged packets */
			if(f->bridge && !fromwire && !fromme)
				continue;
			if(!f->headersonly){
				if(fromwire && fx == 0)
					fx = f;
				else if(xbp = iallocb(len)){
					jehanne_memmove(xbp->wp, pkt, len);
					xbp->wp += len;
					xbp->flag = bp->flag;
					if(qpass(f->iq, xbp) < 0)
						ether->netif.inoverflows++;
				}
				else
					ether->netif.inoverflows++;
			}
			else
				etherrtrace(f, pkt, len);
		}
	}

	if(fx){
		if(qpass(fx->iq, bp) < 0)
			ether->netif.inoverflows++;
		return 0;
	}
	if(fromwire){
		freeb(bp);
		return 0;
	}

	return bp;
}

static int
etheroq(Ether* ether, Block* bp)
{
	int len, loopback;
	Etherpkt *pkt;

	ether->netif.outpackets++;

	/*
	 * Check if the packet has to be placed back onto the input queue,
	 * i.e. if it's a loopback or broadcast packet or the interface is
	 * in promiscuous mode.
	 * If it's a loopback packet indicate to etheriq that the data isn't
	 * needed and return, etheriq will pass-on or free the block.
	 * To enable bridging to work, only packets that were originated
	 * by this interface are fed back.
	 */
	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	loopback = jehanne_memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	if(loopback || jehanne_memcmp(pkt->d, ether->netif.bcast, sizeof(pkt->d)) == 0 || ether->netif.prom)
		if(etheriq(ether, bp, loopback) == 0)
			return len;

	qbwrite(ether->netif.oq, bp);
	if(ether->transmit != nil)
		ether->transmit(ether);
	return len;
}

static long
etherwrite(Chan* chan, void* buf, long n, int64_t _1)
{
	Ether *ether;
	Block *bp;
	int nn, onoff;
	Cmdbuf *cb;

	ether = etherxx[chan->devno];
	if(NETTYPE(chan->qid.path) != Ndataqid) {
		nn = netifwrite(&ether->netif, chan, buf, n);
		if(nn >= 0)
			return nn;
		cb = parsecmd(buf, n);
		if(cb->f[0] && jehanne_strcmp(cb->f[0], "nonblocking") == 0){
			if(cb->nf <= 1)
				onoff = 1;
			else
				onoff = jehanne_atoi(cb->f[1]);
			qnoblock(ether->netif.oq, onoff);
			jehanne_free(cb);
			return n;
		}
		jehanne_free(cb);
		if(ether->ctl!=nil)
			return ether->ctl(ether,buf,n);

		error(Ebadctl);
	}

	if(n > ether->maxmtu)
		error(Etoobig);
	if(n < ether->minmtu)
		error(Etoosmall);

	bp = allocb(n);
	if(waserror()){
		freeb(bp);
		nexterror();
	}
	jehanne_memmove(bp->rp, buf, n);
	jehanne_memmove(bp->rp+Eaddrlen, ether->ea, Eaddrlen);
	poperror();
	bp->wp += n;

	return etheroq(ether, bp);
}

static long
etherbwrite(Chan* chan, Block* bp, int64_t _1)
{
	Ether *ether;
	long n;

	n = BLEN(bp);
	if(NETTYPE(chan->qid.path) != Ndataqid){
		if(waserror()) {
			freeb(bp);
			nexterror();
		}
		n = etherwrite(chan, bp->rp, n, 0);
		poperror();
		freeb(bp);
		return n;
	}
	ether = etherxx[chan->devno];

	if(n > ether->maxmtu){
		freeb(bp);
		error(Etoobig);
	}
	if(n < ether->minmtu){
		freeb(bp);
		error(Etoosmall);
	}

	return etheroq(ether, bp);
}

static struct {
	char*	type;
	int	(*reset)(Ether*);
} cards[MaxEther+1];

void
addethercard(char* t, int (*r)(Ether*))
{
	static int ncard;

	if(ncard == MaxEther)
		panic("too many ether cards");
	cards[ncard].type = t;
	cards[ncard].reset = r;
	ncard++;
}

int
parseether(uint8_t *to, char *from)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	for(i = 0; i < Eaddrlen; i++){
		if(*p == 0)
			return -1;
		nip[0] = *p++;
		if(*p == 0)
			return -1;
		nip[1] = *p++;
		nip[2] = 0;
		to[i] = jehanne_strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return 0;
}

static Ether*
etherprobe(int cardno, int ctlrno)
{
	int i, lg;
	unsigned long mb, bsz;
	Ether *ether;
	char buf[128], name[32];

	ether = jehanne_malloc(sizeof(Ether));
	if(ether == nil){
		jehanne_print("etherprobe: no memory for Ether\n");
		return nil;
	}
	jehanne_memset(ether, 0, sizeof(Ether));
	ether->ctlrno = ctlrno;
	ether->tbdf = BUSUNKNOWN;
	ether->netif.mbps = 10;
	ether->minmtu = ETHERMINTU;
	ether->maxmtu = ETHERMAXTU;

	if(cardno < 0){
		if(isaconfig("ether", ctlrno, ether) == 0){
			jehanne_free(ether);
			return nil;
		}
		for(cardno = 0; cards[cardno].type; cardno++){
			if(jehanne_cistrcmp(cards[cardno].type, ether->type))
				continue;
			for(i = 0; i < ether->nopt; i++){
				if(jehanne_strncmp(ether->opt[i], "ea=", 3))
					continue;
				if(parseether(ether->ea, &ether->opt[i][3]))
					jehanne_memset(ether->ea, 0, Eaddrlen);
			}
			break;
		}
	}

	if(cardno >= MaxEther || cards[cardno].type == nil){
		jehanne_free(ether);
		return nil;
	}
	if(cards[cardno].reset(ether) < 0){
		jehanne_free(ether);
		return nil;
	}

	/*
	 * IRQ2 doesn't really exist, it's used to gang the interrupt
	 * controllers together. A device set to IRQ2 will appear on
	 * the second interrupt controller as IRQ9.
	 */
	if(ether->irq == 2)
		ether->irq = 9;
	jehanne_snprint(name, sizeof(name), "ether%d", ctlrno);

	/*
	 * If ether->irq is <0, it is a hack to indicate no interrupt
	 * used by ethersink.
	 * Or perhaps the driver has some other way to configure
	 * interrupts for itself, e.g. HyperTransport MSI.
	 */
	if(ether->irq >= 0)
		ether->vector = intrenable(ether->irq, ether->interrupt, ether, ether->tbdf, name);

	i = jehanne_sprint(buf, "#l%d: %s: %dMbps port %#p irq %d",
		ctlrno, cards[cardno].type, ether->netif.mbps, ether->port, ether->irq);
	if(ether->mem)
		i += jehanne_sprint(buf+i, " addr %#p", ether->mem);
	if(ether->size)
		i += jehanne_sprint(buf+i, " size %ld", ether->size);
	i += jehanne_sprint(buf+i, ": %2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux",
		ether->ea[0], ether->ea[1], ether->ea[2],
		ether->ea[3], ether->ea[4], ether->ea[5]);
	jehanne_sprint(buf+i, "\n");
	jehanne_print(buf);

	/* compute jehanne_log10(ether->mbps) into lg */
	for(lg = 0, mb = ether->netif.mbps; mb >= 10; lg++)
		mb /= 10;
	if (lg > 0)
		lg--;
	if (lg > 14)			/* 2^(14+17) = 2^31 */
		lg = 14;
	/* allocate larger output queues for higher-speed interfaces */
	bsz = 1UL << (lg + 17);		/* 2^17 = 128K, bsz = 2^n × 128K */
	while (/* bsz > mainmem->maxsize / 8 && */bsz > 128*1024)
		bsz /= 2;

	netifinit(&ether->netif, name, Ntypes, bsz);
	if(ether->netif.oq == nil) {
		ether->netif.oq = qopen(bsz, Qmsg, 0, 0);
		ether->netif.limit = bsz;
	}
	if(ether->netif.oq == 0)
		panic("etherreset %s", name);
	ether->netif.alen = Eaddrlen;
	jehanne_memmove(ether->netif.addr, ether->ea, Eaddrlen);
	jehanne_memset(ether->netif.bcast, 0xFF, Eaddrlen);

	return ether;
}

static void
etherreset(void)
{
	Ether *ether;
	int cardno, ctlrno;

	for(ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if((ether = etherprobe(-1, ctlrno)) == nil)
			continue;
		etherxx[ctlrno] = ether;
	}

	if(getconf("*noetherprobe"))
		return;

	cardno = ctlrno = 0;
	while(cards[cardno].type != nil && ctlrno < MaxEther){
		if(etherxx[ctlrno] != nil){
			ctlrno++;
			continue;
		}
		if((ether = etherprobe(cardno, ctlrno)) == nil){
			cardno++;
			continue;
		}
		etherxx[ctlrno] = ether;
		ctlrno++;
	}
}

static void
ethershutdown(void)
{
	Ether *ether;
	int i;

	for(i = 0; i < MaxEther; i++){
		ether = etherxx[i];
		if(ether == nil)
			continue;
		if(ether->shutdown == nil) {
			jehanne_print("#l%d: no shutdown function\n", i);
			continue;
		}
		(*ether->shutdown)(ether);
	}
}

int
ethercfgmatch(Ether *e, Pcidev *p, uintmem port)
{
	if((e->port == 0 || e->port == port) &&
	   (e->tbdf == BUSUNKNOWN || p == nil || e->tbdf == p->tbdf))
		return 0;
	return -1;
}


#define POLY 0xedb88320

/* really slow 32 bit crc for ethers */
uint32_t
ethercrc(uint8_t *p, int len)
{
	int i, j;
	uint32_t crc, b;

	crc = 0xffffffff;
	for(i = 0; i < len; i++){
		b = *p++;
		for(j = 0; j < 8; j++){
			crc = (crc>>1) ^ (((crc^b) & 1) ? POLY : 0);
			b >>= 1;
		}
	}
	return crc;
}

Dev etherdevtab = {
	'l',
	"ether",

	etherreset,
	devinit,
	ethershutdown,
	etherattach,
	etherwalk,
	etherstat,
	etheropen,
	ethercreate,
	etherclose,
	etherread,
	etherbread,
	etherwrite,
	etherbwrite,
	devremove,
	etherwstat,
};
