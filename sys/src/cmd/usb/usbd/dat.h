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
typedef struct Hub Hub;
typedef struct DHub DHub;
typedef struct Port Port;

enum
{
	Stack	= 32*1024,

	Dhub	= 0x29,		/* hub descriptor type */
	Dhublen = 9,		/* hub descriptor length */

	/* hub class feature selectors */
	Fhublocalpower	= 0,
	Fhubovercurrent	= 1,

	Fportconnection	= 0,
	Fportenable	= 1,
	Fportsuspend	= 2,
	Fportovercurrent = 3,
	Fportreset	= 4,
	Fportpower	= 8,
	Fportlowspeed	= 9,
	Fcportconnection	= 16,
	Fcportenable	= 17,
	Fcportsuspend	= 18,
	Fcportovercurrent= 19,
	Fcportreset	= 20,
	Fportindicator	= 22,

	/* Port status and status change bits
	 * Constants at /sys/src/9/pc/usb.h starting with HP-
	 * must have the same values or root hubs won't work.
	 */
	PSpresent	= 0x0001,
	PSenable	= 0x0002,
	PSsuspend	= 0x0004,
	PSovercurrent	= 0x0008,
	PSreset		= 0x0010,
	PSpower		= 0x0100,
	PSslow		= 0x0200,
	PShigh		= 0x0400,

	PSstatuschg	= 0x10000,	/* PSpresent changed */
	PSchange	= 0x20000,	/* PSenable changed */


	/* port/device state */
	Pdisabled = 0,		/* must be 0 */
	Pattached,
	Pconfiged,

	/* Delays, timeouts (ms) */
//	Spawndelay	= 1000,		/* how often may we re-spawn a driver */
	Spawndelay	= 250,		/* how often may we re-spawn a driver */
//	Connectdelay	= 1000,		/* how much to wait after a connect */
	Connectdelay	= 500,		/* how much to wait after a connect */
	Resetdelay	= 20,		/* how much to wait after a reset */
	Enabledelay	= 20,		/* how much to wait after an enable */
	Powerdelay	= 100,		/* after powering up ports */
	Pollms		= 250, 		/* port poll interval */
	Chgdelay	= 100,		/* waiting for port become stable */
	Chgtmout	= 1000,		/* ...but at most this much */

	/*
	 * device tab for embedded usb drivers.
	 */
	DCL = 0x01000000,		/* csp identifies just class */
	DSC = 0x02000000,		/* csp identifies just subclass */
	DPT = 0x04000000,		/* csp identifies just proto */

};

struct Hub
{
	uint8_t	pwrmode;
	uint8_t	compound;
	uint8_t	pwrms;		/* time to wait in ms */
	uint8_t	maxcurrent;	/*    after powering port*/
	int	leds;		/* has port indicators? */
	int	maxpkt;
	uint8_t	nport;
	Port	*port;
	int	failed;		/* I/O error while enumerating */
	int	isroot;		/* set if root hub */
	Dev	*dev;		/* for this hub */
	Hub	*next;		/* in list of hubs */
};

struct Port
{
	int	state;		/* state of the device */
	int	sts;		/* old port status */
	uint8_t	removable;
	uint8_t	pwrctl;
	Dev	*dev;		/* attached device (if non-nil) */
	Hub	*hub;		/* non-nil if hub attached */
	int	devnb;		/* device number */
	uint64_t	*devmaskp;	/* ptr to dev mask */
};

/* USB HUB descriptor */
struct DHub
{
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint8_t	bNbrPorts;
	uint8_t	wHubCharacteristics[2];
	uint8_t	bPwrOn2PwrGood;
	uint8_t	bHubContrCurrent;
	uint8_t	DeviceRemovable[1];	/* variable length */
};
