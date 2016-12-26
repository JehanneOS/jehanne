/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>

char	errbuf[ERRMAX];
int	ignerr = 0;
int	printerr = 0;

void
err(long e, char *f)
{
	if(printerr){
		print("%s %ulld\n", f, e);
	} else if(!ignerr){
		errbuf[0] = '\0';
		errstr(errbuf, sizeof errbuf);
		fprint(2, "rm: %s: %s\n", f, errbuf);
	}
}

/*
 * f is a non-empty directory. Remove its contents and then it.
 */
void
rmdir(char *f)
{
	char *name;
	long e;
	int fd, i, j, n, ndir, nname;
	Dir *dirbuf;

	fd = open(f, OREAD);
	if(fd < 0){
		err(-1, f);
		return;
	}
	n = dirreadall(fd, &dirbuf);
	close(fd);
	if(n < 0){
		err(-1, "dirreadall");
		return;
	}

	nname = strlen(f)+1+STATMAX+1;	/* plenty! */
	name = malloc(nname);
	if(name == 0){
		err(-1, "memory allocation");
		return;
	}

	ndir = 0;
	for(i=0; i<n; i++){
		snprint(name, nname, "%s/%s", f, dirbuf[i].name);
		if((e = remove(name)) != -1)
			dirbuf[i].qid.type = QTFILE;	/* so we won't recurse */
		else{
			if(dirbuf[i].qid.type & QTDIR)
				ndir++;
			else
				err(e, name);
		}
	}
	if(ndir)
		for(j=0; j<n; j++)
			if(dirbuf[j].qid.type & QTDIR){
				snprint(name, nname, "%s/%s", f, dirbuf[j].name);
				rmdir(name);
			}
	if((e = remove(f)) == -1)
		err(e, f);
	free(name);
	free(dirbuf);
}
void
main(int argc, char *argv[])
{
	int i;
	int recurse;
	long e;
	char *f;
	Dir *db;

	ignerr = 0;
	recurse = 0;
	ARGBEGIN{
	case 'r':
		recurse = 1;
		break;
	case 'f':
		ignerr = 1;
		break;
	case 'e':
		printerr = 1;
		break;
	default:
		fprint(2, "usage: rm [-fr|-e] file ...\n");
		exits("usage");
	}ARGEND
	if(printerr)
		ignerr = 0;
	for(i=0; i<argc; i++){
		f = argv[i];
		e = remove(f);
		if(e != -1 && (!printerr || e == 0))
			continue;
		db = nil;
		if(recurse && (db=dirstat(f))!=nil && (db->qid.type&QTDIR))
			rmdir(f);
		else
			err(e, f);
		free(db);
	}
	exits(errbuf);
}
