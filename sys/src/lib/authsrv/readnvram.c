#include <u.h>
#include <lib9.h>
#include <envvars.h>
#include <authsrv.h>

static int32_t	finddosfile(int, char*);

static int
check(void *x, int len, uint8_t sum, char *msg)
{
	if(nvcsum(x, len) == sum)
		return 0;
	memset(x, 0, len);
	fprint(2, "%s\n", msg);
	return 1;
}

/*
 *  get key info out of nvram.  since there isn't room in the PC's nvram use
 *  a disk partition there.
 */
static struct {
	char *cputype;
	char *file;
	int off;
	int len;
} nvtab[] = {
	"pc", "#S/sdC0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdC0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdC1/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdC1/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdD0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdD0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdE0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdE0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdE1/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdE1/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdF0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdF0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sd00/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sd00/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sd01/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sd01/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sd10/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sd10/9fat", -1, sizeof(Nvrsafe),
	"pc", "#f/fd0disk", -1, 512,	/* 512: #f requires whole sector reads */
	"pc", "#f/fd1disk", -1, 512,
};

static char*
readcons(char *prompt, char *def, int raw, char *buf, int nbuf)
{
	int fdin, fdout, ctl, n, m;
	char line[10];

	fdin = sys_open("/dev/cons", OREAD);
	if(fdin < 0)
		fdin = 0;
	fdout = sys_open("/dev/cons", OWRITE);
	if(fdout < 0)
		fdout = 1;
	if(def != nil)
		fprint(fdout, "%s[%s]: ", prompt, def);
	else
		fprint(fdout, "%s: ", prompt);
	if(raw){
		ctl = sys_open("/dev/consctl", OWRITE);
		if(ctl >= 0)
			jehanne_write(ctl, "rawon", 5);
	} else
		ctl = -1;

	m = 0;
	for(;;){
		n = jehanne_read(fdin, line, 1);
		if(n == 0){
			sys_close(ctl);
			werrstr("readcons: EOF");
			return nil;
		}
		if(n < 0){
			sys_close(ctl);
			werrstr("can't read cons");
			return nil;
		}
		if(line[0] == 0x7f)
			exits(0);
		if(n == 0 || line[0] == '\n' || line[0] == '\r'){
			if(raw){
				jehanne_write(ctl, "rawoff", 6);
				jehanne_write(fdout, "\n", 1);
				sys_close(ctl);
			}
			buf[m] = '\0';
			if(buf[0]=='\0' && def)
				strcpy(buf, def);
			return buf;
		}
		if(line[0] == '\b'){
			if(m > 0)
				m--;
		}else if(line[0] == 0x15){	/* ^U: line kill */
			m = 0;
			if(def != nil)
				fprint(fdout, "%s[%s]: ", prompt, def);
			else
				fprint(fdout, "%s: ", prompt);
		}else{
			if(m >= nbuf-1){
				fprint(fdout, "line too int32_t\n");
				m = 0;
				if(def != nil)
					fprint(fdout, "%s[%s]: ", prompt, def);
				else
					fprint(fdout, "%s: ", prompt);
			}else
				buf[m++] = line[0];
		}
	}
}

typedef struct {
	int	fd;
	int	safelen;
	int64_t	safeoff;
} Nvrwhere;

static char *nvrfile = nil, *cputype = nil;

/* returns with *locp filled in and locp->fd open, if possible */
static void
findnvram(Nvrwhere *locp)
{
	char *nvrlen, *nvroff, *v[2];
	int fd, i, safelen;
	int64_t safeoff;

	if (nvrfile == nil)
		nvrfile = getenv("nvram");
	if (cputype == nil)
		cputype = getenv(ENV_CPUTYPE);
	if(cputype == nil)
		cputype = strdup("mips");
	if(strcmp(cputype, "386")==0 || strcmp(cputype, "amd64")==0 || strcmp(cputype, "alpha")==0) {
		free(cputype);
		cputype = strdup("pc");
	}

	fd = -1;
	safeoff = -1;
	safelen = -1;
	if(nvrfile != nil && *nvrfile != '\0'){
		/* accept device and device!file */
		i = gettokens(nvrfile, v, nelem(v), "!");
		if (i < 1) {
			i = 1;
			v[0] = "";
			v[1] = nil;
		}
		fd = sys_open(v[0], ORDWR);
		if (fd < 0)
			fd = sys_open(v[0], OREAD);
		safelen = sizeof(Nvrsafe);
		if(strstr(v[0], "/9fat") == nil)
			safeoff = 0;
		nvrlen = getenv("nvrlen");
		if(nvrlen != nil)
			safelen = strtol(nvrlen, 0, 0);
		nvroff = getenv("nvroff");
		if(nvroff != nil)
			if(strcmp(nvroff, "dos") == 0)
				safeoff = -1;
			else
				safeoff = strtoll(nvroff, 0, 0);
		if(safeoff < 0 && fd >= 0){
			safelen = 512;
			safeoff = finddosfile(fd, i == 2? v[1]: "plan9.nvr");
			if(safeoff < 0){	/* didn't find plan9.nvr? */
				sys_close(fd);
				fd = -1;
			}
		}
		free(nvroff);
		free(nvrlen);
	}else
		for(i=0; i<nelem(nvtab); i++){
			if(strcmp(cputype, nvtab[i].cputype) != 0)
				continue;
			if((fd = sys_open(nvtab[i].file, ORDWR)) < 0)
				continue;
			safeoff = nvtab[i].off;
			safelen = nvtab[i].len;
			if(safeoff == -1){
				safeoff = finddosfile(fd, "plan9.nvr");
				if(safeoff < 0){  /* didn't find plan9.nvr? */
					sys_close(fd);
					fd = -1;
					continue;
				}
			}
			break;
		}
	locp->fd = fd;
	locp->safelen = safelen;
	locp->safeoff = safeoff;
}

/*
 *  get key info out of nvram.  since there isn't room in the PC's nvram use
 *  a disk partition there.
 */
int
readnvram(Nvrsafe *safep, int flag)
{
	int err;
	char buf[512], in[128];		/* 512 for floppy i/o */
	Nvrsafe *safe;
	Nvrwhere loc;

	err = 0;
	safe = (Nvrsafe*)buf;
	memset(&loc, 0, sizeof loc);
	findnvram(&loc);
	if (loc.safelen < 0)
		loc.safelen = sizeof *safe;
	else if (loc.safelen > sizeof buf)
		loc.safelen = sizeof buf;
	if (loc.safeoff < 0) {
		fprint(2, "readnvram: couldn't find nvram\n");
		if(!(flag&NVwritemem))
			memset(safep, 0, sizeof(*safep));
		safe = safep;
		/*
		 * allow user to type the data for authentication,
		 * even if there's no nvram to store it in.
		 */
	}

	if(flag&NVwritemem)
		safe = safep;
	else {
		memset(safep, 0, sizeof(*safep));
		if(loc.fd < 0
		|| sys_seek(loc.fd, loc.safeoff, 0) < 0
		|| jehanne_read(loc.fd, buf, loc.safelen) != loc.safelen){
			err = 1;
			if(flag&(NVwrite|NVwriteonerr))
				if(loc.fd < 0)
					fprint(2, "can't open %s: %r\n", nvrfile);
				else if (sys_seek(loc.fd, loc.safeoff, 0) < 0)
					fprint(2, "can't seek %s to %lld: %r\n",
						nvrfile, loc.safeoff);
				else
					fprint(2, "can't read %d bytes from %s: %r\n",
						loc.safelen, nvrfile);
			/* start from scratch */
			memset(safep, 0, sizeof(*safep));
			safe = safep;
		}else{
			*safep = *safe;	/* overwrite arg with data read */
			safe = safep;

			/* verify data read */
			err |= check(safe->machkey, DESKEYLEN, safe->machsum,
						"bad nvram des key");
			err |= check(safe->authid, ANAMELEN, safe->authidsum,
						"bad authentication id");
			err |= check(safe->authdom, DOMLEN, safe->authdomsum,
						"bad authentication domain");
			if(0){
				err |= check(safe->config, CONFIGLEN, safe->configsum,
						"bad secstore key");
				err |= check(safe->aesmachkey, AESKEYLEN, safe->aesmachsum,
						"bad nvram aes key");
			} else {
				if(nvcsum(safe->config, CONFIGLEN) != safe->configsum)
					memset(safe->config, 0, CONFIGLEN);
				if(nvcsum(safe->aesmachkey, AESKEYLEN) != safe->aesmachsum)
					memset(safe->aesmachkey, 0, AESKEYLEN);
			}
			if(err == 0)
				if(safe->authid[0]==0 || safe->authdom[0]==0){
					fprint(2, "empty nvram authid or authdom\n");
					err = 1;
				}
		}
	}

	if((flag&(NVwrite|NVwritemem)) || (err && (flag&NVwriteonerr))){
		if (!(flag&NVwritemem)) {
			readcons("authid", nil, 0, safe->authid,
					sizeof safe->authid);
			readcons("authdom", nil, 0, safe->authdom,
					sizeof safe->authdom);
			readcons("secstore key", nil, 1, safe->config,
					sizeof safe->config);
			for(;;){
				Authkey k;

				if(readcons("password", nil, 1, in, sizeof in) == nil)
					goto Out;
				passtokey(&k, in);
				memmove(safe->machkey, k.des, DESKEYLEN);
				memmove(safe->aesmachkey, k.aes, AESKEYLEN);
				break;
			}
		}

		safe->machsum = nvcsum(safe->machkey, DESKEYLEN);
		// safe->authsum = nvcsum(safe->authkey, DESKEYLEN);
		safe->configsum = nvcsum(safe->config, CONFIGLEN);
		safe->authidsum = nvcsum(safe->authid, sizeof safe->authid);
		safe->authdomsum = nvcsum(safe->authdom, sizeof safe->authdom);
		safe->aesmachsum = nvcsum(safe->aesmachkey, AESKEYLEN);

		*(Nvrsafe*)buf = *safe;
		if(loc.fd < 0
		|| sys_seek(loc.fd, loc.safeoff, 0) < 0
		|| jehanne_write(loc.fd, buf, loc.safelen) != loc.safelen){
			fprint(2, "can't write key to nvram: %r\n");
			err = 1;
		}else
			err = 0;
	}
Out:
	if (loc.fd >= 0)
		sys_close(loc.fd);
	return err? -1: 0;
}

typedef struct Dosboot	Dosboot;
struct Dosboot{
	uint8_t	magic[3];	/* really an xx86 JMP instruction */
	uint8_t	version[8];
	uint8_t	sectsize[2];
	uint8_t	clustsize;
	uint8_t	nresrv[2];
	uint8_t	nfats;
	uint8_t	rootsize[2];
	uint8_t	volsize[2];
	uint8_t	mediadesc;
	uint8_t	fatsize[2];
	uint8_t	trksize[2];
	uint8_t	nheads[2];
	uint8_t	nhidden[4];
	uint8_t	bigvolsize[4];
	uint8_t	driveno;
	uint8_t	reserved0;
	uint8_t	bootsig;
	uint8_t	volid[4];
	uint8_t	label[11];
	uint8_t	type[8];
};
#define	GETSHORT(p) (((p)[1]<<8) | (p)[0])
#define	GETLONG(p) ((GETSHORT((p)+2) << 16) | GETSHORT((p)))

typedef struct Dosdir	Dosdir;
struct Dosdir
{
	char	name[8];
	char	ext[3];
	uint8_t	attr;
	uint8_t	reserved[10];
	uint8_t	time[2];
	uint8_t	date[2];
	uint8_t	start[2];
	uint8_t	length[4];
};

static char*
dosparse(char *from, char *to, int len)
{
	char c;

	memset(to, ' ', len);
	if(from == 0)
		return 0;
	while(len-- > 0){
		c = *from++;
		if(c == '.')
			return from;
		if(c == 0)
			break;
		if(c >= 'a' && c <= 'z')
			*to++ = c + 'A' - 'a';
		else
			*to++ = c;
	}
	return 0;
}

/*
 *  return offset of first file block
 *
 *  This is a very simplistic dos file system.  It only
 *  works on floppies, only looks in the root, and only
 *  returns a pointer to the first block of a file.
 *
 *  This exists for cpu servers that have no hard disk
 *  or nvram to store the key on.
 *
 *  Please don't make this any smarter: it stays resident
 *  and I'ld prefer not to waste the space on something that
 *  runs only at boottime -- presotto.
 */
static int32_t
finddosfile(int fd, char *file)
{
	uint8_t secbuf[512];
	char name[8];
	char ext[3];
	Dosboot	*b;
	Dosdir *root, *dp;
	int nroot, sectsize, rootoff, rootsects, n;

	/* dos'ize file name */
	file = dosparse(file, name, 8);
	dosparse(file, ext, 3);

	/* read boot block, check for sanity */
	b = (Dosboot*)secbuf;
	if(jehanne_read(fd, secbuf, sizeof(secbuf)) != sizeof(secbuf))
		return -1;
	if(b->magic[0] != 0xEB || b->magic[1] != 0x3C || b->magic[2] != 0x90)
		return -1;
	sectsize = GETSHORT(b->sectsize);
	if(sectsize != 512)
		return -1;
	rootoff = (GETSHORT(b->nresrv) + b->nfats*GETSHORT(b->fatsize)) * sectsize;
	if(sys_seek(fd, rootoff, 0) < 0)
		return -1;
	nroot = GETSHORT(b->rootsize);
	rootsects = (nroot*sizeof(Dosdir)+sectsize-1)/sectsize;
	if(rootsects <= 0 || rootsects > 64)
		return -1;

	/*
	 *  read root. it is contiguous to make stuff like
	 *  this easier
	 */
	root = malloc(rootsects*sectsize);
	if(jehanne_read(fd, root, rootsects*sectsize) != rootsects*sectsize)
		return -1;
	n = -1;
	for(dp = root; dp < &root[nroot]; dp++)
		if(memcmp(name, dp->name, 8) == 0 && memcmp(ext, dp->ext, 3) == 0){
			n = GETSHORT(dp->start);
			break;
		}
	free(root);

	if(n < 0)
		return -1;

	/*
	 *  dp->start is in cluster units, not sectors.  The first
	 *  cluster is cluster 2 which starts immediately after the
	 *  root directory
	 */
	return rootoff + rootsects*sectsize + (n-2)*sectsize*b->clustsize;
}

