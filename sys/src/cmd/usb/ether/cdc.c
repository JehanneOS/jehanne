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
/*
 * generic CDC
 */

#include <u.h>
#include <lib9.h>
#include <thread.h>

#include "usb.h"
#include "dat.h"

#include <ip.h>

static int
cdcreceive(Dev *ep)
{
	Block *b;
	int n;

	b = allocb(Maxpkt);
	if((n = jehanne_read(ep->dfd, b->wp, b->lim - b->base)) < 0){
		freeb(b);
		return -1;
	}
	b->wp += n;
	etheriq(b, 1);
	return 0;
}

static void
cdctransmit(Dev *ep, Block *b)
{
	int n;

	n = BLEN(b);
	if(jehanne_write(ep->dfd, b->rp, n) < 0){
		freeb(b);
		return;
	}
	freeb(b);

	/*
	 * this may not work with all CDC devices. the
	 * linux driver sends one more random byte
	 * instead of a zero byte transaction. maybe we
	 * should do the same?
	 */
	if((n % ep->maxpkt) == 0)
		jehanne_write(ep->dfd, "", 0);
}

int
cdcinit(Dev *d)
{
	int i;
	Usbdev *ud;
	uint8_t *b;
	Desc *dd;
	char *mac;

	ud = d->usb;
	for(i = 0; i < nelem(ud->ddesc); i++)
		if((dd = ud->ddesc[i]) != nil){
			b = (uint8_t*)&dd->data;
			if(b[1] == Dfunction && b[2] == Fnether){
				mac = loaddevstr(d, b[3]);
				if(mac != nil && strlen(mac) != 12){
					free(mac);
					mac = nil;
				}
				if(mac != nil){
					parseether(macaddr, mac);
					free(mac);

					epreceive = cdcreceive;
					eptransmit = cdctransmit;
					return 0;
				}
			}
		}
	return -1;
}
