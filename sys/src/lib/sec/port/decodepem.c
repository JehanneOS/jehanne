#include "os.h"
#include <libsec.h>

#define STRLEN(s)	(sizeof(s)-1)

uint8_t*
decodePEM(char *s, char *type, int *len, char **new_s)
{
	uint8_t *d;
	char *t, *e, *tt;
	int n;

	*len = 0;

	/*
	 * find the correct section of the file, stripping garbage at the beginning and end.
	 * the data is delimited by -----BEGIN <type>-----\n and -----END <type>-----\n
	 */
	n = jehanne_strlen(type);
	e = jehanne_strchr(s, '\0');
	for(t = s; t != nil && t < e; ){
		tt = t;
		t = jehanne_strchr(tt, '\n');
		if(t != nil)
			t++;
		if(jehanne_strncmp(tt, "-----BEGIN ", STRLEN("-----BEGIN ")) == 0
		&& jehanne_strncmp(&tt[STRLEN("-----BEGIN ")], type, n) == 0
		&& jehanne_strncmp(&tt[STRLEN("-----BEGIN ")+n], "-----\n", STRLEN("-----\n")) == 0)
			break;
	}
	for(tt = t; tt != nil && tt < e; tt++){
		if(jehanne_strncmp(tt, "-----END ", STRLEN("-----END ")) == 0
		&& jehanne_strncmp(&tt[STRLEN("-----END ")], type, n) == 0
		&& jehanne_strncmp(&tt[STRLEN("-----END ")+n], "-----\n", STRLEN("-----\n")) == 0)
			break;
		tt = jehanne_strchr(tt, '\n');
		if(tt == nil)
			break;
	}
	if(tt == nil || tt == e){
		jehanne_werrstr("incorrect .pem file format: bad header or trailer");
		return nil;
	}

	if(new_s)
		*new_s = tt+1;
	n = ((tt - t) * 6 + 7) / 8;
	d = jehanne_malloc(n);
	if(d == nil){
		jehanne_werrstr("out of memory");
		return nil;
	}
	n = jehanne_dec64(d, n, t, tt - t);
	if(n < 0){
		jehanne_free(d);
		jehanne_werrstr("incorrect .pem file format: bad base64 encoded data");
		return nil;
	}
	*len = n;
	return d;
}

PEMChain*
decodepemchain(char *s, char *type)
{
	PEMChain *first = nil, *last = nil, *chp;
	uint8_t *d;
	char *e;
	int n;

	e = jehanne_strchr(s, '\0');
	while (s < e) {
		d = decodePEM(s, type, &n, &s);
		if(d == nil)
			break;
		chp = jehanne_malloc(sizeof(PEMChain));
		chp->next = nil;
		chp->pem = d;
		chp->pemlen = n;
		if (first == nil)
			first = chp;
		else
			last->next = chp;
		last = chp;
	}
	return first;
}
