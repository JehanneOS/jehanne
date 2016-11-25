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
#include "boot.h"

char screenconsolePath[] = "/cmd/hmi/screenconsole";
char comconsolePath[] = "/cmd/hmi/comconsole";
char usbrcPath[] = "/cmd/usbrc";
char hjfsPath[] = "/cmd/hjfs";
char rcPath[] = "/cmd/rc";
char rcmainPath[] = "/arch/rc/lib/rcmain";
char factotumPath[] = "/cmd/auth/factotum";
char fdiskPath[] = "/cmd/disk/fdisk";
char prepPath[] = "/cmd/disk/prep";
char ipconfigPath[] = "/cmd/ip/ipconfig";
BootBind bootbinds[] = {
	{ "/boot", "/", MAFTER },
	{ "/boot/cmd", "/cmd", MAFTER },
};
