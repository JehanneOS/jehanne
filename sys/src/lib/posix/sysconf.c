/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2019 Giacomo Tesio <giacomo@tesio.it>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <lib9.h>
#include <posix.h>
#include "internal.h"

long
POSIX_sysconf(int *errnop, PosixSysConfNames name)
{
	/* "arbitrary" means that no actual limit exists */

	switch(name){
	case PosixSCNArgMax:		// _SC_ARG_MAX
		/* arbitrary */
		return 4096;
	case PosixSCNChildMax:		// _SC_CHILD_MAX
		/* arbitrary */
		return 128;
	case PosixSCNHostNameMax:	// _SC_HOST_NAME_MAX
	case PosixSCNLoginNameMax:	// _SC_LOGIN_NAME_MAX
		/* See Proc.genbuf in /sys/src/kern/port/portdat.h */
		return 128;
	case PosixSCNClockTicks:	// _SC_CLK_TCK
		/* See HZ in /sys/src/kern/amd64/dat.h */
		return 100;
	case PosixSCNOpenMax:		// _SC_OPEN_MAX
		/* arbitrary */
		return 256;
	case PosixSCNPageSize:		// _SC_PAGESIZE
		return 4096;
	case PosixSCNPosixVersion:	// _SC_VERSION
		return 200101;
	case PosixSCNLineMax:		// _SC_LINE_MAX
		/* arbitrary */
		return 4096;

	default:
		*errnop = __libposix_get_errno(PosixENOSYS);
		return -1;
	}
}
