/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017-2019 Giacomo Tesio <giacomo@tesio.it>
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
#include <envvars.h>
#include <posix.h>
#include "internal.h"

static char* __env[1] = { nil };
char **environ = &__env[0]; /* how to map this to #e? */

/* In POSIX, getenv returns strings that must not be freed by the caller.
 * In Jehanne (and Plan9) getenv returns strings obtained with malloc().
 *
 * Thus we need a more complex managememnt to avoid that calling
 * getenv("PATH") several times would leak memory.
 *
 * We use a sorted linked list of outputs returned by Jehanne's getenv
 * and search it before actually calling it.
 */

#define MAX_ENVNAME_LEN	(127+5+1)

typedef struct EnvVar EnvVar;
struct EnvVar
{
	char*	name;
	char*	value;
	EnvVar*	next;
};

static RWLock list_lock;
static EnvVar* list_start;

static void
free_env(EnvVar* e)
{
	free(e->name);
	free(e->value);
	free(e);
}

static void
put_in_list(EnvVar* newenv)
{
	int cmp;
	EnvVar *e, **o;

	wlock(&list_lock);
	
	e = list_start;
	o = &list_start;
	while(e != nil){
		cmp = strcmp(e->name, newenv->name);
		if(cmp < 0){
			// check next (alphabetically sorted)
			o = &e->next;
			e = e->next;
		} else {
			if(cmp > 0){
				// reached the next variable
				newenv->next = e;
			} else {
				// found the variable to replace
				newenv->next = e->next;
				free_env(e);
			}
			e = nil; // quit the lookup
		}
	}

	*o = newenv;

	wunlock(&list_lock);
}

static int
write_env_file(const char *name, const char *value, int size)
{
	int f;
	char buf[MAX_ENVNAME_LEN];

	snprint(buf, sizeof(buf), "/env/%s", name);
	f = ocreate(buf, OWRITE, 664);
	if(f < 0)
		return 0;
	if(jehanne_write(f, value, size) < 0){
		sys_close(f);
		return 0;
	}
	sys_close(f);
	return 1;
}

int
POSIX_setenv(int *errnop, const char *name, const char *value, int overwrite)
{
	EnvVar* e;
	if(name == nil || name[0] == 0 || strchr(name, '=') != nil){
		*errnop = PosixEINVAL;
		return -1;
	}
	if(!overwrite && POSIX_getenv(errnop, name) != nil)
		return 0;
	if(strlen(name) > 127){
		*errnop = PosixENOMEM;
		return -1;
	}
	e = malloc(sizeof(EnvVar));
	e->next = nil;
	e->value = strdup(value);	// see free_env
	e->name = strdup(name);
	if(!write_env_file(name, e->value, strlen(value) + 1)){
		free_env(e);
		*errnop = PosixENOMEM;
		return -1;
	}
	put_in_list(e);
	return 0;
}

int
POSIX_unsetenv(int *errnop, const char *name)
{
	EnvVar* e, **o;
	char buf[MAX_ENVNAME_LEN];
	if(name == nil || name[0] == 0 || strchr(name, '=') != nil){
		*errnop = PosixEINVAL;
		return -1;
	}
	if(POSIX_getenv(errnop, name) == nil)
		return 0;
	e = malloc(sizeof(EnvVar));

	wlock(&list_lock);

	e = list_start;
	o = &list_start;
	while(e != nil){
		if(strcmp(e->name, name) == 0){
			*o = e->next;
			free(e);
			e = nil; // done
		} else {
			o = &e->next;
			e = *o;
		}
	}

	wunlock(&list_lock);

	snprint(buf, sizeof(buf), "/env/%s", name);
	sys_remove(buf);
	return 0;
}

char *
POSIX_getenv(int *errnop, const char *name)
{
	EnvVar* e;
	if(name == nil || name[0] == 0 || strchr(name, '=') != nil){
		*errnop = PosixEINVAL;
		return nil;
	}
	if(strlen(name) > 127){
		*errnop = PosixEINVAL;
		return nil;
	}
	
	rlock(&list_lock);
	e = list_start;
	while(e != nil){
		if(strcmp(name, e->name) == 0)
			break;
		e = e->next;
	}
	runlock(&list_lock);
	if(e != nil)
		return e->value;

	e = malloc(sizeof(EnvVar));
	if(e == nil){
		*errnop = PosixEINVAL;
		return nil;
	}
	e->next = nil;
	e->value = nil;	// see free_env
	e->name = strdup(name);
	if(e->name == nil){
		free_env(e);
		*errnop = PosixEINVAL;
		return nil;
	}
	e->value = getenv(name);
	if(e->value == nil){
		free_env(e);
		return nil;
	}

	put_in_list(e);
	return e->value;
}

void
__libposix_setup_exec_environment(char * const *env)
{
	int fd, len;
	char **e, *start, *end;
	char ename[MAX_ENVNAME_LEN];

	if(env == nil || env[0] == nil)
		return;
	strcpy(ename, "#e/");
	for(e = (char **)env; (start = *e); e++) {
		end = strchr(start, '=');
		if(!end || start==end)
			continue;	/* not in "var=val" format */
		len = end-start;
		if(len > 127)
			len = 127;
		memcpy(ename+3, start, len);
		ename[3+len] = 0;
		fd = ocreate(ename, OWRITE, 0666);
		if(fd < 0)
			continue;
		end++; /* after '=' */
		len = strlen(end);
		sys_pwrite(fd, end, len, -1);
		sys_close(fd);
	}
}
