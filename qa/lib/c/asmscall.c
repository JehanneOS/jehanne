/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <libc.h>

int verbose = 1;

#define asm_nsec() ({ \
	register long __ret asm ("rax"); \
	__asm__ __volatile__ ( \
		"syscall" \
		: "=r" (__ret) \
		: "0"(19) \
		: "cc", "rcx", "r11", "memory" \
	); \
	__ret; })

#define asm_mount(/* int */ fd, /* int */ afd, /* char* */ old, /* int */ flag, /* char* */ aname, /* int */ mdev) ({ \
	register long r10 asm("r10") = flag; \
	register long r8 asm("r8") = (uintptr_t)aname; \
	register long r9 asm("r9") = mdev; \
	register int __ret asm ("eax"); \
	__asm__ __volatile__ ( \
		"syscall" \
		: "=r" (__ret) \
		: "0"(16), "D"(fd), "S"(afd), "d"(old), "r"(r10), "r"(r8), "r"(r9) \
		: "cc", "rcx", "r11", "memory" \
	); \
	__ret; })

/*
long
asm_nsec(void)
{
	register int *p1 asm ("r0");
	register int *p2 asm ("r1");
	register int *result asm ("r0");
	asm ("sysint" : "=r" (result) : "0" (p1), "r" (p2));
	return result ;
}
*/

void
main(void)
{
	int ret = 0; // success
	uint64_t start, end;
	char *msg;

	start = asm_nsec();
	sleep(1);
	end = asm_nsec();

	if (end <= start)
		ret = 1;

	if (verbose)
		print("nsec: start %llx, end %llx\n", start, end);
	if(ret){
		msg = smprint("nsec: FAIL: start %llx end %llx",
			start, end);
		print("%s\n", msg);
		exits(msg);
	}

	int fd;
	fd = open("#|", ORDWR);
	asm_mount(fd, -1, "/tmp", MREPL, "", 'M');

	print("PASS\n");
	exits("PASS");
}
