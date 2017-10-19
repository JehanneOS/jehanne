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

#define ENV_APID	"apid"
#define ENV_USER	"user"
#define ENV_HOME	"home"
#define ENV_IFS		"ifs"
#define ENV_PATH	"path"
#define ENV_PID		"pid"
#define ENV_PROMPT	"prompt"
#define ENV_STATUS	"status"
#define ENV_CDPATH	"cdpath"
#define ENV_CPUTYPE	"cputype"
#define ENV_SERVICE	"service"
#define ENV_SYSNAME	"sysname"
#define ENV_OBJTYPE	"objtype"

#define ENV_WSYS	"wsys"

extern	char*	jehanne_getenv(const char*);
extern	int	jehanne_putenv(const char*, const char*);
