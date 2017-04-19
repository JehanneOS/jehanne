/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <lib9.h>
#include <ip.h>

/* from the kernel. Sorry. */
enum {
	Nipht = 521,				/* convenient prime */
};

/* Jehanne iphash() from ip/ipaux.c */
uint32_t
iphash(uint8_t *sa, uint16_t sp, uint8_t *da, uint16_t dp)
{
	return (((uint32_t)(sa[IPaddrlen-1])<<24) ^ (sp << 16) ^ (((uint32_t)da[IPaddrlen-1])<<8) ^ dp ) % Nipht;
}

/* old Nix iphash (worked with kenc) */
uint32_t oldiphash(uint8_t * sa, uint16_t sp, uint8_t * da, uint16_t dp)
{
	return ((sa[IPaddrlen - 1] << 24) ^ (sp << 16) ^ (da[IPaddrlen - 1] << 8) ^
			dp) % Nipht;
}

/* conventions.
 * informational messages go on fd 2.
 * PASS/FAIL go on fd 1 and there is only ever one of each.
 * The first four characters of passing tests are PASS
 * The first four characters of failing tests are FAIL
 * It is an error to print both PASS and FAIL
 * FAIL tests should exits() with a message
 * We may consider not printing PASS/FAIL and using exits instead.
 */
void
main()
{
	static uint8_t sa[IPaddrlen] = { 0x80, };
	static uint8_t da[IPaddrlen];
	uint16_t sp = 4;
	uint16_t dp = 5;
	uint32_t ohash, nhash;
	sa[IPaddrlen - 1] = 0x80;
	ohash = oldiphash(sa, sp, da, dp);
	if (ohash > Nipht)
		fprint(2, "oldiphash returns bad value: 0x%ulx, should be < 0x%ulx\n",
			   ohash, Nipht);
	nhash = iphash(sa, sp, da, dp);
	if (nhash > Nipht)
		fprint(2, "iphash returns bad value: 0x%ulx, should be < 0x%ulx\n",
			   ohash, Nipht);
	fprint(2, "ohash is 0x%ulx, nhash is 0x%ulx\n", ohash, nhash);
	if (ohash == nhash) {
		/* ohash and nhash should differs on gcc due to type promotion rules */
		fprint(2, "FAIL: iphash equals\n");
		exits("FAIL");
	}
	/* Always print PASS at the end. */
	print("PASS\n");
	exits("PASS");
}
