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
	Net20DCVid	= 0x0525,	/* Ajays usb debug cable */
	Net20DCDid	= 0x127a,

	HuaweiVid	= 0x12d1,
	HuaweiE220	= 0x1003,
};

Cinfo uconsinfo[] = {
	{ Net20DCVid,	Net20DCDid,	1 },
	{ HuaweiVid,	HuaweiE220,	2 },
	{ 0,		0,		0 },
};

int
uconsprobe(Serial *ser)
{
	Usbdev *ud = ser->dev->usb;
	Cinfo *ip;

	if((ip = matchid(uconsinfo, ud->vid, ud->did)) == nil)
		return -1;
	ser->nifcs = ip->cid;
	return 0;
}
