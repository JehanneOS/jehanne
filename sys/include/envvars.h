/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
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

/* This file defines the names of a few environment variables shared
 * among the userspace tools provided by Jehanne.
 *
 * It must not provide defines for variables used by commonly installed
 * softwares, just the one used by the core tools, the one required
 * to boot a minimal system.
 */

#define ENV_APID	"APID"
#define ENV_USER	"USER"
#define ENV_HOME	"HOME"
#define ENV_IFS		"IFS"
#define ENV_PATH	"PATH"
#define ENV_PID		"PID"
#define ENV_PROMPT	"PROMPT"
#define ENV_STATUS	"STATUS"
#define ENV_CDPATH	"CDPATH"
#define ENV_CPUTYPE	"CPUTYPE"
#define ENV_SERVICE	"SERVICE"
#define ENV_SYSNAME	"SYSNAME"
#define ENV_OBJTYPE	"OBJTYPE"

#define ENV_WSYS	"wsys"

extern	char*	jehanne_getenv(const char*);
extern	int	jehanne_putenv(const char*, const char*);
