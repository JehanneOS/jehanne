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
#include <thread.h>

#include "usb.h"
#include "dat.h"

static uint8_t minit[24] = {
	2,  0, 0, 0, /* type = 2 (init) */
	24, 0, 0, 0, /* len = 24 */
	0,  0, 0, 0, /* rid = 1 */
	1,  0, 0, 0, /* vmajor = 1 */
	1,  0, 0, 0, /* vminor = 1 */
	0,  0, 0, 0, /* max xfer */
};
static uint8_t mgetmac[28] = {
	4,  0, 0, 0, /* type = 4 (query) */
	28, 0, 0, 0, /* len = 28 */
	0,  0, 0, 0, /* rid = 2 */
	1,  1, 1, 1, /* oid = get "permanent address" */
	0,  0, 0, 0, /* buflen = 0 */
	0,  0, 0, 0, /* bufoff = 0 */
	0,  0, 0, 0, /* reserved = 0 */
};
static uint8_t mfilter[32] = {
	5,  0, 0, 0, /* type = 5 (set) */
	32, 0, 0, 0, /* len = 32 */
	0,  0, 0, 0, /* rid = 3 */
	14, 1, 1, 0, /* oid = "current filter" */
	4,  0, 0, 0, /* buflen = 4 */
	20, 0, 0, 0, /* bufoff = 20 (8+20=28) */
	0,  0, 0, 0, /* reserved = 0 */
	12, 0, 0, 0, /* filter = all multicast + broadcast */
};

static int
rndisout(Dev *d, int id, uint8_t *msg, int sz)
{
	return usbcmd(d, Rh2d|Rclass|Riface, Rgetstatus, 0, id, msg, sz);
}

static int
rndisin(Dev *d, int id, uint8_t *buf, int sz)
{
	int r, status;
	for(;;){
		if((r = usbcmd(d, Rd2h|Rclass|Riface, Rclearfeature, 0, id, buf, sz)) >= 16){
			if((status = GET4(buf+12)) != 0){
				werrstr("status 0x%02x", status);
				r = -1;
			}else if(GET4(buf) == 7) /* ignore status messages */
				continue;
		}else if(r > 0){
			werrstr("short recv: %d", r);
			r = -1;
		}
		break;
	}
	return r;
}

static int
rndisreceive(Dev *ep)
{
	Block *b;
	int n, len;
	int doff, dlen;

	b = allocb(Maxpkt);
	if((n = jehanne_read(ep->dfd, b->rp, b->lim - b->base)) >= 0){
		if(n < 44)
			werrstr("short packet: %d bytes", n);
		else if(GET4(b->rp) != 1)
			werrstr("not a network packet: type 0x%08ux", GET4(b->wp));
		else{
			doff = GET4(b->rp+8);
			dlen = GET4(b->rp+12);
			if((len = GET4(b->rp+4)) != n || 8+doff+dlen > len || dlen < Ehdrsz)
				werrstr("bad packet: doff %d, dlen %d, len %d", doff, dlen, len);
			else{
				b->rp += 8 + doff;
				b->wp = b->rp + dlen;

				etheriq(b, 1);
				return 0;
			}
		}
	}

	freeb(b);
	return -1;
}

static void
rndistransmit(Dev *ep, Block *b)
{
	int n;

	n = BLEN(b);
	b->rp -= 44;
	PUT4(b->rp, 1);      /* type = 1 (packet) */
	PUT4(b->rp+4, 44+n); /* len */
	PUT4(b->rp+8, 44-8); /* data offset */
	PUT4(b->rp+12, n);   /* data length */
	memset(b->rp+16, 0, 7*4);
	jehanne_write(ep->dfd, b->rp, 44+n);
	freeb(b);
}

int
rndisinit(Dev *d)
{
	static uint32_t maxXfer = 1580;
	uint8_t res[128];
	int r, i, off, sz;
	Ep *ep;

	r = 0;
	for(i = 0; i < nelem(d->usb->ep); i++){
		if((ep = d->usb->ep[i]) == nil)
			continue;
		if(ep->iface->csp == 0x000301e0)
			r = 1;
	}
	if(!r){
		werrstr("no rndis found");
		return -1;
	}

	/* initialize */
	PUT4(minit+20, maxXfer); /* max xfer = 1580 */
	if(rndisout(d, 0, minit, sizeof(minit)) < 0)
		werrstr("init: %r");
	else if((r = rndisin(d, 0, res, sizeof(res))) < 0)
		werrstr("init: %r");
	else if(GET4(res) != 0x80000002 || r < 52)
		werrstr("not an init response: type 0x%08ux, len %d", GET4(res), r);
	/* check the type */
	else if((r = GET4(res+24)) != 1)
		werrstr("not a connectionless device: %d", r);
	else if((r = GET4(res+28)) != 0)
		werrstr("not a 802.3 device: %d", r);
	else{
		/* get mac address */
		if(rndisout(d, 0, mgetmac, sizeof(mgetmac)) < 0)
			werrstr("send getmac: %r");
		else if((r = rndisin(d, 0, res, sizeof(res))) < 0)
			werrstr("recv getmac: %r");
		else if(GET4(res) != 0x80000004 || r < 24)
			werrstr("not a query response: type 0x%08ux, len %d", GET4(res), r);
		else {
			sz = GET4(res+16);
			off = GET4(res+20);
			if(8+off+sz > r || sz != 6)
				werrstr("invalid mac: off %d, sz %d, len %d", off, sz, r);
			else{
				memcpy(macaddr, res+8+off, 6);
				/* set the filter */
				if(rndisout(d, 0, mfilter, sizeof(mfilter)) < 0)
					werrstr("send filter: %r");
				else if(rndisin(d, 0, res, sizeof(res)) < 0)
					werrstr("recv filter: %r");
				else if(GET4(res) != 0x80000005)
					werrstr("not a filter response: type 0x%08ux", GET4(res));
				else{
					epreceive = rndisreceive;
					eptransmit = rndistransmit;
					return 0;
				}
			}
		}
	}

	return -1;
}
