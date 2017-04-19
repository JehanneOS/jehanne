#include <u.h>
#include <lib9.h>
#include <auth.h>
#include <9P2000.h>
#include <thread.h>
#include <9p.h>

enum {
	NHASH = 128
};

typedef struct Intlist	Intlist;
struct Intlist
{
	uint32_t	id;
	void*	aux;
	Intlist*	link;
};

struct Intmap
{
	RWLock	rwl;
	Intlist*	hash[NHASH];
	void (*inc)(void*);
};

static uint32_t
hashid(uint32_t id)
{
	return id%NHASH;
}

static void
nop(void* _)
{
}

Intmap*
allocmap(void (*inc)(void*))
{
	Intmap *m;

	m = emalloc9p(sizeof(*m));
	if(inc == nil)
		inc = nop;
	m->inc = inc;
	return m;
}

void
freemap(Intmap *map, void (*destroy)(void*))
{
	int i;
	Intlist *p, *nlink;

	if(destroy == nil)
		destroy = nop;
	for(i=0; i<NHASH; i++){
		for(p=map->hash[i]; p; p=nlink){
			nlink = p->link;
			destroy(p->aux);
			free(p);
		}
	}
			
	free(map);
}

static Intlist**
llookup(Intmap *map, uint32_t id)
{
	Intlist **lf;

	for(lf=&map->hash[hashid(id)]; *lf; lf=&(*lf)->link)
		if((*lf)->id == id)
			break;
	return lf;	
}

/*
 * The RWlock is used as expected except that we allow
 * inc() to be called while holding it.  This is because we're
 * locking changes to the tree structure, not to the references.
 * Inc() is expected to have its own locking.
 */
void*
lookupkey(Intmap *map, uint32_t id)
{
	Intlist *f;
	void *v;

	rlock(&map->rwl);
	if(f = *llookup(map, id)){
		v = f->aux;
		map->inc(v);
	}else
		v = nil;
	runlock(&map->rwl);
	return v;
}

void*
insertkey(Intmap *map, uint32_t id, void *v)
{
	Intlist *f;
	void *ov;
	uint32_t h;

	wlock(&map->rwl);
	if(f = *llookup(map, id)){
		/* no decrement for ov because we're returning it */
		ov = f->aux;
		f->aux = v;
	}else{
		f = emalloc9p(sizeof(*f));
		f->id = id;
		f->aux = v;
		h = hashid(id);
		f->link = map->hash[h];
		map->hash[h] = f;
		ov = nil;
	}
	wunlock(&map->rwl);
	return ov;	
}

int
caninsertkey(Intmap *map, uint32_t id, void *v)
{
	Intlist *f;
	int rv;
	uint32_t h;

	wlock(&map->rwl);
	if(*llookup(map, id))
		rv = 0;
	else{
		f = emalloc9p(sizeof *f);
		f->id = id;
		f->aux = v;
		h = hashid(id);
		f->link = map->hash[h];
		map->hash[h] = f;
		rv = 1;
	}
	wunlock(&map->rwl);
	return rv;	
}

void*
deletekey(Intmap *map, uint32_t id)
{
	Intlist **lf, *f;
	void *ov;

	wlock(&map->rwl);
	if(f = *(lf = llookup(map, id))){
		ov = f->aux;
		*lf = f->link;
		free(f);
	}else
		ov = nil;
	wunlock(&map->rwl);
	return ov;
}
