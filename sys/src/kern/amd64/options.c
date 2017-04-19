#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
Conf conf;
char *confname[1] = {
	"console",
};
char *confval[1] = {
	"0 b115200",
};
int nconf = nelem(confname);
 */

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * There are 3584 bytes available at CONFADDR.
 */
#define	CONFADDR	PTR2UINT(KADDR(0x0001200))

#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	(4096-0x200-BOOTLINELEN)
#define	MAXCONF		64

char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;

void
confoptions(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = jehanne_getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = jehanne_strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		confname[nconf] = line[i];
		confval[nconf] = cp;
		nconf++;
	}
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(jehanne_cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
}

void
confsetenv(void)
{
	int i;

	for(i = 0; i < nconf; i++){
		if(confname[i][0] != '*')
			ksetenv(confname[i], confval[i], 0);
		ksetenv(confname[i], confval[i], 1);
	}
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[32], *p;
	int i;

	jehanne_snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	p = getconf(cc);
	if(p == nil)
		return 0;

	isa->type = "";
	isa->nopt = jehanne_tokenize(p, isa->opt, NISAOPT);
	for(i = 0; i < isa->nopt; i++){
		p = isa->opt[i];
		if(jehanne_cistrncmp(p, "type=", 5) == 0)
			isa->type = p + 5;
		else if(jehanne_cistrncmp(p, "port=", 5) == 0)
			isa->port = jehanne_strtoul(p+5, &p, 0);
		else if(jehanne_cistrncmp(p, "irq=", 4) == 0)
			isa->irq = jehanne_strtoul(p+4, &p, 0);
		else if(jehanne_cistrncmp(p, "dma=", 4) == 0)
			isa->dma = jehanne_strtoul(p+4, &p, 0);
		else if(jehanne_cistrncmp(p, "mem=", 4) == 0)
			isa->mem = jehanne_strtoul(p+4, &p, 0);
		else if(jehanne_cistrncmp(p, "size=", 5) == 0)
			isa->size = jehanne_strtoul(p+5, &p, 0);
		else if(jehanne_cistrncmp(p, "freq=", 5) == 0)
			isa->freq = jehanne_strtoul(p+5, &p, 0);
	}
	return 1;
}
