/* Copyright (c) 20XX 9front
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <u.h>
#include <lib9.h>
#include <9P2000.h>
#include <thread.h>
#include <9p.h>
#include "usb.h"

typedef struct Audio Audio;
struct Audio
{
	Audio	*next;

	int	minfreq;
	int	maxfreq;
};

int audiodelay = 1764;	/* 40 ms */
int audiofreq = 44100;
int audiochan = 2;
int audiores = 16;

char user[] = "audio";

Dev *audiodev = nil;

Ep *audioepin = nil;
Ep *audioepout = nil;

void
parsedescr(Desc *dd)
{
	Audio *a, **ap;
	uint8_t *b;
	int i;

	if(dd == nil || dd->iface == nil || dd->altc == nil)
		return;
	b = (uint8_t*)&dd->data;
	if(Subclass(dd->iface->csp) != 2 || b[1] != 0x24 || b[2] != 0x02)
		return;
	if(b[4] != audiochan)
		return;
	if(b[6] != audiores)
		return;

	ap = (Audio**)&dd->altc->aux;
	if(b[7] == 0){
		a = mallocz(sizeof(*a), 1);
		a->minfreq = b[8] | b[9]<<8 | b[10]<<16;
		a->maxfreq = b[11] | b[12]<<8 | b[13]<<16;
		a->next = *ap;
		*ap = a;
	} else {
		for(i=0; i<b[7]; i++){
			a = mallocz(sizeof(*a), 1);
			a->minfreq = b[8+3*i] | b[9+3*i]<<8 | b[10+3*i]<<16;
			a->maxfreq = a->minfreq;
			a->next = *ap;
			*ap = a;
		}
	}
}

Dev*
setupep(Dev *d, Ep *e, int speed)
{
	uint8_t b[4];
	Audio *x;
	Altc *a;
	int i;

	for(i = 0; i < nelem(e->iface->altc); i++)
		if(a = e->iface->altc[i])
			for(x = a->aux; x; x = x->next)
				if(speed >= x->minfreq && speed <= x->maxfreq)
					goto Foundaltc;

	werrstr("no altc found");
	return nil;

Foundaltc:
	if(usbcmd(d, Rh2d|Rstd|Riface, Rsetiface, i, e->iface->id, nil, 0) < 0){
		werrstr("set altc: %r");
		return nil;
	}

	b[0] = speed;
	b[1] = speed >> 8;
	b[2] = speed >> 16;
	if(usbcmd(d, Rh2d|Rclass|Rep, Rsetcur, 0x100, e->addr, b, 3) < 0)
		fprint(2, "warning: set freq: %r\n");

	if((d = openep(d, e->id)) == nil){
		werrstr("openep: %r");
		return nil;
	}
	devctl(d, "pollival %d", a->interval);
	devctl(d, "samplesz %d", audiochan*audiores/8);
	devctl(d, "sampledelay %d", audiodelay);
	devctl(d, "hz %d", speed);
	if(e->dir == Ein)
		devctl(d, "name audioin");
	else
		devctl(d, "name audio");
	return d;
}

void
fsread(Req *r)
{
	char *msg;

	msg = smprint("master 100 100\nspeed %d\ndelay %d\n", audiofreq, audiodelay);
	readstr(r, msg);
	respond(r, nil);
	free(msg);
}

void
fswrite(Req *r)
{
	char msg[256], *f[4];
	int nf, speed;

	snprint(msg, sizeof(msg), "%.*s", r->ifcall.count, r->ifcall.data);
	nf = tokenize(msg, f, nelem(f));
	if(nf < 2){
		respond(r, "invalid ctl message");
		return;
	}
	if(strcmp(f[0], "speed") == 0){
		Dev *d;

		speed = atoi(f[1]);
Setup:
		if((d = setupep(audiodev, audioepout, speed)) == nil){
			responderror(r);
			return;
		}
		closedev(d);
		if(audioepin != nil && audioepin != audioepout){
			if(d = setupep(audiodev, audioepin, speed))
				closedev(d);
		}
		audiofreq = speed;
	} else if(strcmp(f[0], "delay") == 0){
		audiodelay = atoi(f[1]);
		speed = audiofreq;
		goto Setup;
	}
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

Srv fs = {
	.read = fsread,
	.write = fswrite,
};

void
usage(void)
{
	fprint(2, "%s devid\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char buf[32];
	Dev *d, *ed;
	Ep *e;
	int i;

	ARGBEGIN {
	case 'D':
		chatty9p++;
		break;
	case 'd':
		usbdebug++;
		break;
	} ARGEND;

	if(argc == 0)
		usage();

	if((d = getdev(*argv)) == nil)
		sysfatal("getdev: %r");
	audiodev = d;

	/* parse descriptors, mark valid altc */
	for(i = 0; i < nelem(d->usb->ddesc); i++)
		parsedescr(d->usb->ddesc[i]);
	for(i = 0; i < nelem(d->usb->ep); i++){
		e = d->usb->ep[i];
		if(e != nil && e->type == Eiso && e->iface != nil && e->iface->csp == CSP(Claudio, 2, 0)){
			switch(e->dir){
			case Ein:
				if(audioepin != nil)
					continue;
				audioepin = e;
				break;
			case Eout:
				if(audioepout != nil)
					continue;
				audioepout = e;
				break;
			case Eboth:
				if(audioepin != nil && audioepout != nil)
					continue;
				if(audioepin == nil)
					audioepin = e;
				if(audioepout == nil)
					audioepout = e;
				break;
			}
			if((ed = setupep(audiodev, e, audiofreq)) == nil){
				fprint(2, "setupep: %r\n");

				if(e == audioepin)
					audioepin = nil;
				if(e == audioepout)
					audioepout = nil;
				continue;
			}
			closedev(ed);
		}
	}
	if(audioepout == nil)
		sysfatal("no endpoints found");

	fs.tree = alloctree(user, "usb", DMDIR|0555, nil);
	createfile(fs.tree->root, "volume", user, 0666, nil);

	snprint(buf, sizeof buf, "%d.audio", audiodev->id);
	postsharesrv(&fs, nil, "usb", buf);

	exits(0);
}
