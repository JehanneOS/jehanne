/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <u.h>
#include <lib9.h>
#include <draw.h>

Image*
namedimage(Display *d, char *name)
{
	uint8_t *a;
	char *err, buf[12*12+1];
	Image *i;
	int id, n;
	uint32_t chan;

	err = 0;
	i = 0;

	n = strlen(name);
	if(n >= 256){
		err = "name too long";
    Error:
		if(err)
			werrstr("namedimage: %s", err);
		else
			werrstr("namedimage: %r");
		if(i)
			free(i);
		return 0;
	}
	/* flush pending data so we don't get error allocating the image */
	flushimage(d, 0);
	a = bufimage(d, 1+4+1+n);
	if(a == 0)
		goto Error;
	d->imageid++;
	id = d->imageid;
	a[0] = 'n';
	BPLONG(a+1, id);
	a[5] = n;
	memmove(a+6, name, n);
	if(flushimage(d, 0) < 0)
		goto Error;

	if(sys_pread(d->ctlfd, buf, sizeof buf, 0) < 12*12)
		goto Error;
	buf[12*12] = '\0';

	i = malloc(sizeof(Image));
	if(i == nil){
	Error1:
		a = bufimage(d, 1+4);
		if(a){
			a[0] = 'f';
			BPLONG(a+1, id);
			flushimage(d, 0);
		}
		goto Error;
	}
	i->display = d;
	i->id = id;
	if((chan=strtochan(buf+2*12))==0){
		werrstr("bad channel '%.12s' from devdraw", buf+2*12);
		goto Error1;
	}
	i->chan = chan;
	i->depth = chantodepth(chan);
	i->repl = atoi(buf+3*12);
	i->r.min.x = atoi(buf+4*12);
	i->r.min.y = atoi(buf+5*12);
	i->r.max.x = atoi(buf+6*12);
	i->r.max.y = atoi(buf+7*12);
	i->clipr.min.x = atoi(buf+8*12);
	i->clipr.min.y = atoi(buf+9*12);
	i->clipr.max.x = atoi(buf+10*12);
	i->clipr.max.y = atoi(buf+11*12);
	i->screen = 0;
	i->next = 0;
	return i;
}

