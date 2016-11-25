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

char screenconsolePath[] = "/boot/screenconsole";
char comconsolePath[] = "/boot/comconsole";
char usbrcPath[] = "/boot/usbrc";
char hjfsPath[] = "/boot/hjfs";
char rcPath[] = "/boot/rc";
char rcmainPath[] = "/boot/rcmain";
char factotumPath[] = "/boot/factotum";
char fdiskPath[] = "/boot/fdisk";
char prepPath[] = "/boot/prep";
char ipconfigPath[] = "/boot/ipconfig";
BootBind bootbinds[] = {
	{ nil, nil, 0 }
};
