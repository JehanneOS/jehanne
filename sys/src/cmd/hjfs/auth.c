#include <u.h>
#include <libc.h>
#include <9P2000.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

typedef struct User User;
typedef struct PUser PUser;

struct User {
	short uid;
	char name[USERLEN];
	short lead;
	int nmemb;
	short *memb;
};

struct PUser {
	short uid;
	char name[USERLEN];
	char lead[USERLEN];
	int nmemb;
	char (*memb)[USERLEN];
};

User udef[] = {
	{-1, "adm", -1, 0, nil},
	{0, "none", -1, 0, nil},
	{1, "tor", 1, 0, nil},
	{2, "glenda", 2, 0, nil},
	{10000, "sys", NOUID, 0, nil},
	{10001, "map", 10001, 0, nil},
	{10002, "doc", NOUID, 0, nil},
	{10003, "upas", 10003, 0, nil},
	{10004, "font", NOUID, 0, nil},
};

static int
validuser(char *n)
{
	char *p;

	if(*n == 0)
		return 0;
	for(p = n; *p != 0; p++)
		if((uint8_t) *p < ' ' || strchr("?=+-/:", *p) != nil)
			return 0;
	return n - p < USERLEN;
}

static void
usersparseline(char *l, PUser **u, int *nu)
{
	PUser v;
	char *f[5], *r, *s;
	int c;

	if(*l == 0 || *l == '#')
		return;
	c = getfields(l, f, 5, 0, ":");
	if(c < 4)
		return;
	v.uid = strtol(f[0], &r, 10);
	if(*r != 0)
		return;
	if(!validuser(f[1]) || *f[2] != 0 && !validuser(f[2]))
		return;
	strcpy(v.name, f[1]);
	strcpy(v.lead, f[2]);
	v.memb = nil;
	v.nmemb = 0;
	r = f[3];
	while(r != nil && *r != 0){
		s = strchr(r, ',');
		if(s != nil)
			*s = 0;
		if(!validuser(r)){
			free(v.memb);
			return;
		}
		v.memb = erealloc(v.memb, (v.nmemb + 1) * USERLEN);
		strcpy(v.memb[v.nmemb++], r);
		if(s == nil)
			r = nil;
		else
			r = s + 1;
	}
	*u = erealloc(*u, (*nu + 1) * sizeof(PUser));
	memcpy(&(*u)[(*nu)++], &v, sizeof(PUser));
}

static int
puserlook(PUser *u, int nu, char *name)
{
	PUser *v;

	if(*name == 0)
		return NOUID;
	for(v = u; v < u + nu; v++)
		if(strcmp(v->name, name) == 0)
			return v->uid;
	return NOUID;
}

static int
uidcomp(const void *a, const void *b)
{
	const short *aa, *bb;

	aa = a;
	bb = b;
	return *aa - *bb;
}

static int
usercomp(const void *a, const void *b)
{
	const User *aa, *bb;

	aa = a;
	bb = b;
	return aa->uid - bb->uid;
}

int
usersload(Fs *fs, Chan *ch)
{
	char *buf, *p, *q;
	int bufl, i, j, rc, nu;
	PUser *u;
	User *v;

	buf = nil;
	bufl = 0;
	u = nil;
	v = nil;
	nu = 0;
	for(;;){
		if((bufl & 1023) == 0)
			buf = erealloc(buf, bufl + 1024);
		rc = chanread(ch, buf + bufl, 1024, bufl);
		if(rc < 0)
			goto err;
		if(rc == 0)
			break;
		bufl += rc;
	}
	if(buf == nil)
		goto done;
	buf[bufl] = 0;
	for(p = buf; q = strchr(p, '\n'); p = q + 1){
		*q = 0;
		usersparseline(p, &u, &nu);
	}
	usersparseline(p, &u, &nu);
	free(buf);
	if(nu == 0)
		goto done;
	v = emalloc(sizeof(User) * nu);
	for(i = 0; i < nu; i++){
		v[i].uid = u[i].uid;
		strcpy(v[i].name, u[i].name);
		v[i].lead = puserlook(u, nu, u[i].lead);
		v[i].nmemb = u[i].nmemb;
		v[i].memb = emalloc(sizeof(short) * v[i].nmemb);
		for(j = 0; j < v[i].nmemb; j++)
			v[i].memb[j] = puserlook(u, nu, u[i].memb[j]);
		qsort(v[i].memb, v[i].nmemb, sizeof(uint16_t), uidcomp);
	}
	qsort(v, nu, sizeof(User), usercomp);
done:
	wlock(&fs->udatal);
	if(fs->udata != nil){
		for(i = 0; i < fs->nudata; i++)
			free(((User *)fs->udata)[i].memb);
		free(fs->udata);
	}
	fs->udata = v;
	fs->nudata = nu;
	wunlock(&fs->udatal);
	return 0;
err:
	free(buf);
	return -1;
}

int
userssave(Fs *fs, Chan *ch)
{
	User *u, *v;
	int nu, i;
	char buf[512], ubuf[USERLEN], *p, *e;
	uint64_t off;

	rlock(&fs->udatal);
	u = fs->udata;
	if(u == nil){
		u = udef;
		nu = nelem(udef);
	}else
		nu = fs->nudata;
	off = 0;
	for(v = u; v < u + nu; v++){
		p = buf;
		e = buf + sizeof(buf);
		p = seprint(p, e, "%d:%s:", v->uid, v->name);
		if(v->lead != NOUID)
			p = strecpy(p, e, uid2name(fs, v->lead, ubuf));
		if(p < e)
			*p++ = ':';
		for(i = 0; i < v->nmemb; i++){
			if(v->memb[i] == NOUID)
				continue;
			if(p < e && i > 0)
				*p++ = ',';
			p = strecpy(p, e, uid2name(fs, v->memb[i], ubuf));
		}
		*p++ = '\n';
		if(ch == nil)
			write(2, buf, p - buf);
		else if(chanwrite(ch, buf, p - buf, off) < p - buf)
			goto err;
		off += p - buf;
	}
	runlock(&fs->udatal);
	return 0;
err:
	runlock(&fs->udatal);
	return -1;
}

static User *
lookupuid(Fs *fs, short uid)
{
	User *u;
	int i, j, k;

	u = fs->udata;
	i = 0;
	j = fs->nudata;
	if(u == nil){
		u = udef;
		j = nelem(udef);
	}
	if(j == 0)
		return nil;
	while(i < j){
		k = (i + j) / 2;
		if(u[k].uid < uid)
			i = k + 1;
		else
			j = k;
	}
	if(u[i].uid == uid)
		return &u[i];
	return nil;
}

int
ingroup(Fs *fs, short uid, short gid, int leader)
{
	User *g;
	int i, j, k;

	if(uid == gid)
		return 1;
	rlock(&fs->udatal);
	g = lookupuid(fs, gid);
	if(g == nil)
		goto nope;
	if(g->lead == uid)
		goto yes;
	if(leader && g->lead != NOUID)
		goto nope;
	if(g->nmemb == 0)
		goto nope;
	i = 0;
	j = g->nmemb;
	while(i < j){
		k = (i + j) / 2;
		if(g->memb[k] < uid)
			i = k + 1;
		else
			j = k;
	}
	if(g->memb[i] == uid)
		goto yes;
nope:
	runlock(&fs->udatal);
	return 0;
yes:
	runlock(&fs->udatal);
	return 1;
}

int
permcheck(Fs *fs, Dentry *d, short uid, int mode)
{
	int perm;

	if((fs->flags & FSNOPERM) != 0)
		return 1;
	perm = d->mode & 0777;
	if(d->uid == uid)
		perm >>= 6;
	else if(ingroup(fs, uid, d->gid, 0))
		perm >>= 3;
	switch(mode & 3){
	case NP_OREAD:
		return (perm & 4) != 0;
	case NP_OWRITE:
		return (perm & 2) != 0;
	case NP_OEXEC:
		return (perm & 1) != 0;
	case NP_ORDWR:
		return (perm & 6) == 6;
	}
	return 0;
}

char *
uid2name(Fs *fs, short uid, char *buf)
{
	User *u;

	rlock(&fs->udatal);
	u = lookupuid(fs, uid);
	if(buf == nil)
		buf = emalloc(USERLEN);
	if(u == nil)
		snprint(buf, USERLEN, "%d", uid);
	else
		snprint(buf, USERLEN, "%s", u->name);
	runlock(&fs->udatal);
	return buf;
}

int
name2uid(Fs *fs, char *name, short *uid)
{
	char *r;
	User *u, *v;

	*uid = strtol(name, &r, 10);
	if(*r == 0)
		return 1;
	rlock(&fs->udatal);
	u = fs->udata;
	v = u + fs->nudata;
	if(u == nil){
		u = udef;
		v = udef + nelem(udef);
	}
	for(; u < v; u++)
		if(strcmp(u->name, name) == 0){
			*uid = u->uid;
			runlock(&fs->udatal);
			return 1;
		}
	runlock(&fs->udatal);
	werrstr(Einval);
	return -1;
}

static void
createuserdir(Fs *fs, char *name, short uid)
{
	Chan *ch;

	ch = chanattach(fs, CHFNOPERM);
	if(ch == nil)
		return;
	ch->uid = uid;
	if(chanwalk(ch, "usr") > 0)
		chancreat(ch, name, DMDIR | 0775, NP_OREAD);
	chanclunk(ch);
}

int
cmdnewuser(int argc, char **argv)
{
	short uid, gid;
	User *u, *v;
	Fs *fs;
	int resort, createdir, i, j;
	extern Fs *fsmain;

	if(argc < 2)
		return -9001;
	if(!validuser(argv[1])){
		werrstr(Einval);
		return -1;
	}
	fs = fsmain;
	resort = 0;
	createdir = 0;
	wlock(&fs->udatal);
	if(fs->udata == nil){
		wunlock(&fs->udatal);
		werrstr("newuser: no user database");
		return -1;
	}
	uid = 0;
	gid = 10000;
	for(u = fs->udata; u < fs->udata + fs->nudata; u++){
		if(strcmp(u->name, argv[1]) == 0)
			goto found;
		if(u->uid == uid)
			uid++;
		if(u->uid == gid)
			gid++;
	}
	resort = 1;
	fs->udata = erealloc(fs->udata, sizeof(User) * (fs->nudata + 1));
	u = fs->udata + fs->nudata++;
	strcpy(u->name, argv[1]);
	u->nmemb = 0;
	u->memb = nil;
	u->uid = gid;
	u->lead = NOUID;
	if(argc == 2 || strcmp(argv[2], ":") != 0){
		u->lead = u->uid = uid;
		createdir = 1;
	}
found:
	for(i = 2; i < argc; i++){
		if(strcmp(argv[i], ":") == 0)
			continue;
		if(*argv[i] != '+' && *argv[i] != '-' && *argv[i] != '='){
			if(!validuser(argv[i]))
				goto erropt;
			strcpy(u->name, argv[i]);
			continue;
		}
		for(v = fs->udata; v < fs->udata + fs->nudata; v++)
			if(strcmp(v->name, argv[i] + 1) == 0)
				break;
		if(v == fs->udata + fs->nudata)
			goto erropt;
		if(*argv[i] == '='){
			u->lead = v->uid;
			continue;
		}
		for(j = 0; j < u->nmemb && u->memb[j] < v->uid; j++)
			;
		if(*argv[i] == '-'){
			if(u->memb[j] != v->uid)
				goto erropt;
			memmove(&u->memb[j], &u->memb[j + 1], sizeof(short) * (u->nmemb - j - 1));
			u->memb = erealloc(u->memb, sizeof(short) * --u->nmemb);
		}else{
			u->memb = erealloc(u->memb, sizeof(short) * ++u->nmemb);
			memmove(&u->memb[j + 1], &u->memb[j], sizeof(short) * (u->nmemb - j - 1));
			u->memb[j] = v->uid;
		}
		continue;
	erropt:
		dprint("hjfs: newuser: ignoring erroneous option %s\n", argv[i]);
	}
	if(resort)
		qsort(fs->udata, fs->nudata, sizeof(User), usercomp);
	wunlock(&fs->udatal);
	writeusers(fs);
	if(createdir)
		createuserdir(fs, argv[1], uid);
	return 1;
}

