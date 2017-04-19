#include "os.h"
#include <bio.h>
#include <libsec.h>

enum{ ThumbTab = 1<<10 };

static Thumbprint*
tablehead(uint8_t *sum, Thumbprint *table)
{
	return &table[((sum[0]<<8) + sum[1]) & (ThumbTab-1)];
}

void
freeThumbprints(Thumbprint *table)
{
	Thumbprint *hd, *p, *q;

	if(table == nil)
		return;
	for(hd = table; hd < table+ThumbTab; hd++){
		for(p = hd->next; p && p != hd; p = q){
			q = p->next;
			jehanne_free(p);
		}
	}
	jehanne_free(table);
}

int
okThumbprint(uint8_t *sum, Thumbprint *table)
{
	Thumbprint *hd, *p;

	if(table == nil)
		return 0;
	hd = tablehead(sum, table);
	for(p = hd->next; p; p = p->next){
		if(jehanne_memcmp(sum, p->sha1, SHA1dlen) == 0)
			return 1;
		if(p == hd)
			break;
	}
	return 0;
}

static int
loadThumbprints(char *file, Thumbprint *table, Thumbprint *crltab)
{
	Thumbprint *hd, *entry;
	char *line, *field[50];
	uint8_t sum[SHA1dlen];
	Biobuf *bin;

	if(jehanne_access(file, AEXIST) < 0)
		return 0;	/* not an error */
	if((bin = Bopen(file, OREAD)) == nil)
		return -1;
	for(; (line = Brdstr(bin, '\n', 1)) != nil; jehanne_free(line)){
		if(jehanne_tokenize(line, field, nelem(field)) < 2)
			continue;
		if(jehanne_strcmp(field[0], "#include") == 0){
			if(loadThumbprints(field[1], table, crltab) < 0)
				goto err;
			continue;
		}
		if(jehanne_strcmp(field[0], "x509") != 0 || jehanne_strncmp(field[1], "sha1=", 5) != 0)
			continue;
		field[1] += 5;
		if(jehanne_dec16(sum, SHA1dlen, field[1], jehanne_strlen(field[1])) != SHA1dlen){
			jehanne_werrstr("malformed x509 entry in %s: %s", file, field[1]);
			goto err;
		}
		if(crltab && okThumbprint(sum, crltab))
			continue;
		hd = tablehead(sum, table);
		if(hd->next == nil)
			entry = hd;
		else {
			if((entry = jehanne_malloc(sizeof(*entry))) == nil)
				goto err;
			entry->next = hd->next;
		}
		hd->next = entry;
		jehanne_memcpy(entry->sha1, sum, SHA1dlen);
	}
	Bterm(bin);
	return 0;
err:
	jehanne_free(line);
	Bterm(bin);
	return -1;
}

Thumbprint *
initThumbprints(char *ok, char *crl)
{
	Thumbprint *table, *crltab;

	table = crltab = nil;
	if(crl){
		if((crltab = jehanne_malloc(ThumbTab * sizeof(*crltab))) == nil)
			goto err;
		jehanne_memset(crltab, 0, ThumbTab * sizeof(*crltab));
		if(loadThumbprints(crl, crltab, nil) < 0)
			goto err;
	}
	if((table = jehanne_malloc(ThumbTab * sizeof(*table))) == nil)
		goto err;
	jehanne_memset(table, 0, ThumbTab * sizeof(*table));
	if(loadThumbprints(ok, table, crltab) < 0){
		freeThumbprints(table);
		table = nil;
	}
err:
	freeThumbprints(crltab);
	return table;
}
