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
 * USB joystick constants
 */
enum {

	Stack = 32 * 1024,

	/* HID class subclass protocol ids */
	JoyCSP		= 0x000003,
	
	Maxaxes = 3,

	/* Requests */
	Getreport = 0x01,
	Setreport = 0x09,
	Getproto	= 0x03,
	Setproto	= 0x0b,

	/* protocols for SET_PROTO request */
	Bootproto	= 0,
	Reportproto	= 1,

	/* protocols for SET_REPORT request */
	Reportout = 0x0200,
};

/*
 * USB HID report descriptor item tags
 */ 
enum {
	/* main items */
	Input	= 8,
	Output,
	Collection,
	Feature,

	CollectionEnd,

	/* global items */
	UsagPg = 0,
	LogiMin,
	LogiMax,
	PhysMin,
	PhysMax,
	UnitExp,
	UnitTyp,
	RepSize,
	RepId,
	RepCnt,

	Push,
	Pop,

	/* local items */
	Usage	= 0,
	UsagMin,
	UsagMax,
	DesgIdx,
	DesgMin,
	DesgMax,
	StrgIdx,
	StrgMin,
	StrgMax,

	Delim,
};

/* main item flags */
enum {
	Fdata	= 0<<0,	Fconst	= 1<<0,
	Farray	= 0<<1,	Fvar	= 1<<1,
	Fabs	= 0<<2,	Frel	= 1<<2,
	Fnowrap	= 0<<3,	Fwrap	= 1<<3,
	Flinear	= 0<<4,	Fnonlin	= 1<<4,
	Fpref	= 0<<5,	Fnopref	= 1<<5,
	Fnonull	= 0<<6,	Fnullst	= 1<<6,
};
