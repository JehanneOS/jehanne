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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

void
waitdebugger(void)
{
	static int c;
	while(!c) // to continue: p c=1 ; c
		;
}

void
inspect(uintptr_t arg)
{
	static int c;
	if(arg == 0)
	while(!c) // to continue: p c=1 ; c
		;		
}

int __onExecFaultBreakForPID = 0;
char __onExecFaultBreakForCMD[32] = "";
void
peekAtExecFaults(uintptr_t addr)
{
	static int brk;
	if(up == nil)
		return;
	if (__onExecFaultBreakForPID && up->pid == __onExecFaultBreakForPID)
		brk = 1;
	if (__onExecFaultBreakForCMD[0] && jehanne_strcmp(__onExecFaultBreakForCMD, up->text) == 0)
		brk = 1;
	if (brk > 10)
		brk = 1;
}

uint64_t
rdmsr(uint32_t reg)
{
	extern uint64_t _rdmsr(uint32_t reg);
	uint64_t res;
#ifdef LOGMSR
	jehanne_print("rdmsr(%#p)", reg);
#endif
	res = _rdmsr(reg);
#ifdef LOGMSR
	jehanne_print(" = %#P", res);
#endif
	return res;
}

void
wrmsr(uint32_t reg, uint64_t val)
{
	extern void _wrmsr(uint32_t reg, uint64_t val);
#ifdef LOGMSR
	jehanne_print("wrmsr(%#p, %#P)", reg, val);
#endif
	_wrmsr(reg, val);
}
