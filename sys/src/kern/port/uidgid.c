/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

typedef struct User User;
struct User
{
	char*	name;
	char*	leader;
	User*	next;
	int	n;
	char*	mem[];
};

enum{
	Ulog=	6,
	Usize=	1<<Ulog,
	Umask=	Usize-1,
};

static struct{
	Lock;
	User*	hash[Usize];
} users;

static void
checkname(char *s)
{
	Rune r;
	static char invalid[] = "#:,()";

	if(*s == 0 || *s == '-' || *s == '+')
		error("illegal name");
	while((r = *s) != 0){
		if(r < Runeself){
			s++;
			if(r < 0x20 || r >= 0x7F && r < 0xA0 || jehanne_strchr(invalid, r) != nil)
				r = Runeerror;
		}else
			s += jehanne_chartorune(&r, s);
		if(r == Runeerror)
			error("invalid character in name");
	}
}

static uint32_t
hashpjw(char *s)
{
	uint32_t h, g;

	h = 0;
	for(; *s != 0; s++){
		h = (h << 4) + (*s&0xFF);
		g = h & 0xf0000000;
		if(g != 0)
			h ^= ((g >> 24) & 0xff) | g;
	}
	return h & 0x7FFFFFFF;
}

static User**
lookuser(char *name)
{
	uint32_t h;
	User **l, *u;

	h = hashpjw(name) & Umask;
	for(l = &users.hash[h]; (u = *l) != nil; l = &u->next)
		if(jehanne_strcmp(u->name, name) == 0)
			break;
	return l;
}

static char*
tack(char **p, char *s)
{
	char *o;

	o = *p;
	jehanne_strcpy(o, s);
	*p += jehanne_strlen(o)+1;
	return o;
}

void
adduser(char *uid, char *leader, int nm, char **mem)
{
	User **l, *u, *v;
	char *o;
	int i, nc;

	if(leader != nil){
		if(*leader == '\0')
			leader = nil;
		else if(jehanne_strcmp(leader, uid) == 0)
			leader = uid;
	}
	checkname(uid);
	nc = jehanne_strlen(uid)+1;
	if(leader != nil && leader != uid){
		checkname(leader);
		nc += jehanne_strlen(leader)+1;
	}
	for(i = 0; i < nm; i++){
		checkname(mem[i]);
		nc += jehanne_strlen(mem[i])+1;
	}
	v = jehanne_mallocz(sizeof(User)+nm*sizeof(v->mem[0])+nc, 1);
	if(v == nil)
		error(Enomem);
	o = (char*)(v+1)+nm*sizeof(v->mem[0]);
	v->name = tack(&o, uid);
	if(leader == nil)
		v->leader = nil;
	else if(jehanne_strcmp(v->name, leader) != 0)
		v->leader = tack(&o, leader);
	else
		v->leader = v->name;
	v->n = nm;
	for(i = 0; i < nm; i++)
		v->mem[i] = tack(&o, mem[i]);
	lock(&users);
	l = lookuser(uid);
	u = *l;
	if(u != nil){
		/* replace */
		v->next = u->next;
		jehanne_free(u);
	}
	*l = v;
	unlock(&users);
}

int
deluser(char *name)
{
	User **l, *u;

	lock(&users);
	l = lookuser(name);
	u = *l;
	if(u == nil){
		unlock(&users);
		return 0;
	}
	*l = u->next;
	unlock(&users);
	jehanne_free(u);
	return 1;
}

static int
ismember(char *s, int n, char **mem)
{
	int i;

	for(i = 0; i < n; i++)
		if(jehanne_strcmp(s, mem[i]) == 0)
			return 1;
	return 0;
}

int
ingroup(char *uid, char *gid)
{
	User *g;

	if(jehanne_strcmp(uid, gid) == 0)
		return 1;
	lock(&users);
	g = *lookuser(gid);
	if(g != nil && ismember(uid, g->n, g->mem)){
		unlock(&users);
		return 1;
	}
	unlock(&users);
	return 0;
}

int
leadsgroup(char *uid, char *gid)
{
	User *g;

	lock(&users);
	g = *lookuser(gid);
	if(g != nil){
		if(g->leader != nil && jehanne_strcmp(uid, g->leader) == 0 ||
		   g->leader == nil && ismember(uid, g->n, g->mem)){
			unlock(&users);
			return 1;
		}
	}
	unlock(&users);
	return g == nil && jehanne_strcmp(uid, gid) == 0;
}

char*
usersread(void)
{
	int i, m;
	User *u;
	Fmt fmt;

	jehanne_fmtstrinit(&fmt);
	for(i = 0; i < nelem(users.hash); i++){
		lock(&users);
		for(u = users.hash[i]; u != nil; u = u->next){
			jehanne_fmtprint(&fmt, "%q", u->name);
			if(u->leader != nil || u->n != 0){
				jehanne_fmtprint(&fmt, " %q", u->leader != nil? u->leader: "");
				for(m = 0; m < u->n; m++)
					jehanne_fmtprint(&fmt, " %q", u->mem[m]);
			}
			jehanne_fmtprint(&fmt, "\n");
		}
		unlock(&users);
	}
	return jehanne_fmtstrflush(&fmt);
}

long
userswrite(void *buf, long n)
{
	int i, nf;
	char *p, *s, *e, *flds[100];

	if(n <= 0)
		return n;
	if(n > 16*1024)
		error(Etoobig);
	p = jehanne_malloc(n+1);
	if(p == nil)
		error(Enomem);
	if(waserror()){
		jehanne_free(p);
		nexterror();
	}
	jehanne_memmove(p, buf, n);
	p[n] = '\0';
	if(p[n-1] != '\n')
		error("incomplete line");
	for(s = p; (e = jehanne_strchr(s, '\n')) != nil; s = e){
		*e++ = '\0';
		if(*s == '#')
			continue;
		nf = jehanne_tokenize(s, flds, nelem(flds));
		if(nf == nelem(flds))
			error("too many group members");
		if(jehanne_strcmp(flds[0], "-") == 0){
			for(i = 1; i < nf; i++)
				deluser(flds[i]);
		}else if(nf > 1)
			adduser(flds[0], flds[1], nf-2, flds+2);
		else if(nf != 0)
			adduser(flds[0], nil, 0, nil);
	}
	poperror();
	jehanne_free(p);
	return n;
}
