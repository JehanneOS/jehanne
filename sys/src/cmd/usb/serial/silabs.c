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
#include <9P2000.h>
#include <9p.h>
#include "usb.h"
#include "serial.h"

enum {
	Enable		= 0x00,

	Getbaud		= 0x1D,
	Setbaud		= 0x1E,
	Setlcr		= 0x03,
	Getlcr		= 0x04,
		Bitsmask	= 0x0F00,
		Bitsshift	= 8,
		Parmask		= 0x00F0,
		Parshift	= 4,
		Stopmask	= 0x000F,
		Stop1		= 0x0000,
		Stop1_5		= 0x0001,
		Stop2		= 0x0002,
};

static Cinfo slinfo[] = {
	{ 0x10c4, 0xea60, },		/* CP210x */
	{ 0x10c4, 0xea61, },		/* CP210x */
	{ 0,	0, },
};

static Serialops slops;

int
slprobe(Serial *ser)
{
	Usbdev *ud = ser->dev->usb;

	if(matchid(slinfo, ud->vid, ud->did) == nil)
		return -1;
	ser->Serialops = slops;
	return 0;
}

static int
slwrite(Serialport *p, int req, void *buf, int len)
{
	Serial *ser;

	ser = p->s;
	return usbcmd(ser->dev, Rh2d | Rvendor | Riface, req, 0, p->interfc,
		buf, len);
}

static int
slput(Serialport *p, uint32_t op, uint32_t val)
{
	Serial *ser;

	ser = p->s;
	return usbcmd(ser->dev, Rh2d | Rvendor | Riface, op, val, p->interfc,
		nil, 0);
}

static int
slread(Serialport *p, int req, void *buf, int len)
{
	Serial *ser;

	ser = p->s;
	return usbcmd(ser->dev, Rd2h | Rvendor | Riface, req, 0, p->interfc,
		buf, len);
}

static int
slinit(Serialport *p)
{
	Serial *ser;

	ser = p->s;
	dsprint(2, "slinit\n");

	slput(p, Enable, 1);

	slops.getparam(p);

	/* p gets freed by closedev, the process has a reference */
	incref(&ser->dev->ref);
	return 0;
}

static int
slgetparam(Serialport *p)
{
	uint16_t lcr;

	slread(p, Getbaud, &p->baud, sizeof(p->baud));
	slread(p, Getlcr, &lcr, sizeof(lcr));
	p->bits = (lcr&Bitsmask)>>Bitsshift;
	p->parity = (lcr&Parmask)>>Parshift;
	p->stop = (lcr&Stopmask) == Stop1? 1 : 2;
	return 0;
}

static int
slsetparam(Serialport *p)
{
	uint16_t lcr;

	lcr = p->stop == 1? Stop1 : Stop2;
	lcr |= (p->bits<<Bitsshift) | (p->parity<<Parshift);
	slput(p, Setlcr, lcr);
	slwrite(p, Setbaud, &p->baud, sizeof(p->baud));
	return 0;
}

static int
wait4data(Serialport *p, uint8_t *data, int count)
{
	int n;

	qunlock(&p->s->ql);
	while ((n = jehanne_read(p->epin->dfd, data, count)) == 0)
		;
	qlock(&p->s->ql);
	return n;
}

static Serialops slops = {
	.init		= slinit,
	.getparam	= slgetparam,
	.setparam	= slsetparam,
	.wait4data	= wait4data,
};
