/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
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

int *__libposix_errors_codes;

typedef struct PosixErrorMap PosixErrorMap;
struct PosixErrorMap
{
	PosixErrorTranslator translate;
	PosixErrorMap *next;
};

typedef struct CustomErrorMap CustomErrorMap;
struct CustomErrorMap
{
	uintptr_t caller;
	PosixErrorMap *head;
	CustomErrorMap *next;
};

static PosixErrorMap *generic_handlers;
static CustomErrorMap *custom_handlers;

void
__libposix_errors_check_conf(void)
{
	int i;
	/* check that all required configurations has been provided */
	for(i = 0; i < ERRNO_LAST-ERRNO_FIRST; ++i){
		if(__libposix_errors_codes[i] == 0)
			sysfatal("libposix: PosixError %d is undefined", i + ERRNO_FIRST);
	}
	if(generic_handlers == nil)
		sysfatal("libposix: no generic error handler");
}

int
libposix_define_errno(PosixError e, int errno)
{
	if(e < ERRNO_FIRST || e > ERRNO_LAST)
		return 0;
	__libposix_errors_codes[e - ERRNO_FIRST] = errno;
	return 1;
}

static int
register_translation(PosixErrorMap **map, PosixErrorTranslator translation)
{
	PosixErrorMap *entry = malloc(sizeof(PosixErrorMap));
	if(entry == nil)
		return 0;
	entry->translate = translation;
	entry->next = *map;
	*map = entry;
	return 1;
}

int
libposix_translate_error(PosixErrorTranslator translation, uintptr_t caller)
{
	CustomErrorMap **list = nil;
	
	if(__libposix_initialized())
		return 0;
	if(translation == nil)
		return 0;
	if(caller == 0)
		return register_translation(&generic_handlers, translation);
	list = &custom_handlers;
	while(*list != nil)
	{
		if((*list)->caller == caller)
			break;
		list = &(*list)->next;
	}
	if(*list == nil){
		*list = malloc(sizeof(CustomErrorMap));
		(*list)->next = nil;
		(*list)->caller = caller;
		(*list)->head = nil;
	}
	return register_translation(&(*list)->head, translation);
}

int
__libposix_get_errno(PosixError e)
{
	if(e == 0)
		return 0;
	if(e < 0){
		/* this way we allow a translator to return a
		 * non-posix errno as a negative number that will not
		 * collide with the indexes declared in PosixError enum
		 */
		return (int)-e;
	}
	if(e < ERRNO_FIRST || e > ERRNO_LAST)
		return PosixEINVAL;
	return __libposix_errors_codes[e - ERRNO_FIRST];
}

static PosixError
get_posix_error(PosixErrorMap *translations, char *err, uintptr_t caller)
{
	PosixError e = 0;
	while(translations != nil)
	{
		e = translations->translate(err, caller);
		if(e != 0)
			return e;
		translations = translations->next;
	}
	return PosixEINVAL;
}

PosixError
libposix_translate_kernel_errors(const char *msg)
{
	// TODO: autogenerate from /sys/src/sysconf.json
	if(nil == msg)
		return 0;
	if(strncmp("interrupted", msg, 9) == 0)
		return PosixEINTR;
	if(strncmp("no living children", msg, 18) == 0)
		return PosixECHILD;
	if(strstr(msg, "file not found") != nil)
		return PosixENOENT;
	if(strstr(msg, "does not exist") != nil)
		return PosixENOENT;
	if(strstr(msg, "file already exists") != nil)
		return PosixEEXIST;
	if(strstr(msg, "file is a directory") != nil)
		return PosixEISDIR;
	if(strncmp("fd out of range or not open", msg, 27) == 0)
		return PosixEBADF;
	if(strstr(msg, "not a directory") != nil)
		return PosixENOTDIR;
	if(strstr(msg, "permission denied") != nil)
		return PosixEPERM;
	if(strstr(msg, "name too long") != nil)
		return PosixENAMETOOLONG;
	if(strcmp("i/o error", msg) == 0)
		return PosixEIO;
	if(strcmp("i/o on hungup channel", msg) == 0)
		return PosixEIO;
	return 0;
}

int
__libposix_translate_errstr(uintptr_t caller)
{
	CustomErrorMap *handler;
	PosixError perr = 0;
	char err[ERRMAX];
	int ret;

	if(sys_errstr(err, ERRMAX) < 0)
		return __libposix_get_errno(PosixEINVAL);

	handler = custom_handlers;
	while(handler != nil)
	{
		if(handler->caller == caller)
			break;
	}
	if(handler != nil)
		perr = get_posix_error(handler->head, err, caller);
	if(perr == 0)
		perr = get_posix_error(generic_handlers, err, caller);
	if(perr == 0)
		perr = libposix_translate_kernel_errors(err);
	ret = __libposix_get_errno(perr);
	sys_errstr(err, ERRMAX);
	return ret;
}
