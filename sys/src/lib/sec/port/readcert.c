#include "os.h"
#include <libsec.h>

static char*
readfile(char *name)
{
	int fd;
	char *s;
	Dir *d;

	fd = sys_open(name, OREAD);
	if(fd < 0)
		return nil;
	if((d = jehanne_dirfstat(fd)) == nil) {
		sys_close(fd);
		return nil;
	}
	s = jehanne_malloc(d->length + 1);
	if(s == nil || jehanne_readn(fd, s, d->length) != d->length){
		jehanne_free(s);
		jehanne_free(d);
		sys_close(fd);
		return nil;
	}
	sys_close(fd);
	s[d->length] = '\0';
	jehanne_free(d);
	return s;
}

uint8_t*
readcert(char *filename, int *pcertlen)
{
	char *pem;
	uint8_t *binary;

	pem = readfile(filename);
	if(pem == nil){
		jehanne_werrstr("can't read %s: %r", filename);
		return nil;
	}
	binary = decodePEM(pem, "CERTIFICATE", pcertlen, nil);
	jehanne_free(pem);
	if(binary == nil){
		jehanne_werrstr("can't parse %s", filename);
		return nil;
	}
	return binary;
}

PEMChain *
readcertchain(char *filename)
{
	char *chfile;

	chfile = readfile(filename);
	if (chfile == nil) {
		jehanne_werrstr("can't read %s: %r", filename);
		return nil;
	}
	return decodepemchain(chfile, "CERTIFICATE");
}

