/* Copyright (c) 20XX 9front
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <u.h>
#include <lib9.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

#define GET8(x) {x = *p++;}
#define GET16(x) {x = *p++; x |= *p++ << 8;}
#define GET24(x) {x = *p++; x |= *p++ << 8; x |= *p++ << 16;}
#define GET32(x) {x = *p++; x |= *p++ << 8; x |= *p++ << 16; x |= *p++ << 24;}
#define GET64(x) \
		{x = (uint64_t) *p++; \
		x |= (uint64_t) *p++ << 8; \
		x |= (uint64_t) *p++ << 16; \
		x |= (uint64_t) *p++ << 24; \
		x |= (uint64_t) *p++ << 32; \
		x |= (uint64_t) *p++ << 40; \
		x |= (uint64_t) *p++ << 48; \
		x |= (uint64_t) *p++ << 56;}
#define GETS(x, n) {memcpy(x, p, n); p += n;}

#define PUT8(x) {*p++ = x;}
#define PUT16(x) {*p++ = x; *p++ = x >> 8;}
#define PUT24(x) {*p++ = x; *p++ = x >> 8; *p++ = x >> 16;}
#define PUT32(x) {*p++ = x; *p++ = x >> 8; *p++ = x >> 16; *p++ = x >> 24;}
#define PUT64(x) \
		{*p++ = x; \
		*p++ = x >> 8; \
		*p++ = x >> 16; \
		*p++ = x >> 24; \
		*p++ = x >> 32; \
		*p++ = x >> 40; \
		*p++ = x >> 48; \
		*p++ = x >> 56;}
#define PUTS(x, n) {memcpy(p, x, n); p += n;}

void
unpack(Buf *b, uint8_t *p)
{
	Dentry *d;
	int i;

	switch(b->type = *p++){
	default:
		memcpy(b->data, p, RBLOCK);
		break;
	case TSUPERBLOCK:
		GET32(b->sb.magic);
		GET64(b->sb.size);
		GET64(b->sb.fstart);
		GET64(b->sb.fend);
		GET64(b->sb.root);
		GET64(b->sb.qidpath);
		break;
	case TDENTRY:
		for(d = b->de; d < &b->de[DEPERBLK]; d++){
			GETS(d->name, NAMELEN);
			GET16(d->uid);
			GET16(d->muid);
			GET16(d->gid);
			GET16(d->mode);
			GET64(d->path);
			GET32(d->vers);
			GET8(d->type);
			GET64(d->size);
			for(i = 0; i < NDIRECT; i++)
				GET64(d->db[i]);
			for(i = 0; i < NINDIRECT; i++)
				GET64(d->ib[i]);
			GET64(d->atime);
			GET64(d->mtime);
		}
		break;
	case TINDIR:
		for(i = 0; i < OFFPERBLK; i++)
			GET64(b->offs[i]);
		break;
	case TREF:
		for(i = 0; i < REFPERBLK; i++)
			GET24(b->refs[i]);
		break;
	}
	USED(p);
}

void
pack(Buf *b, uint8_t *p)
{
	Dentry *d;
	int i;

	switch(*p++ = b->type){
	case TRAW:
		memcpy(p, b->data, RBLOCK);
		break;

	case TSUPERBLOCK:
		PUT32(b->sb.magic);
		PUT64(b->sb.size);
		PUT64(b->sb.fstart);
		PUT64(b->sb.fend);
		PUT64(b->sb.root);
		PUT64(b->sb.qidpath);
		break;
	case TDENTRY:
		for(d = b->de; d < b->de + nelem(b->de); d++){
			PUTS(d->name, NAMELEN);
			PUT16(d->uid);
			PUT16(d->muid);
			PUT16(d->gid);
			PUT16(d->mode);
			PUT64(d->path);
			PUT32(d->vers);
			PUT8(d->type);
			PUT64(d->size);
			for(i = 0; i < NDIRECT; i++)
				PUT64(d->db[i]);
			for(i = 0; i < NINDIRECT; i++)
				PUT64(d->ib[i]);
			PUT64(d->atime);
			PUT64(d->mtime);
		}
		break;
	case TINDIR:
		for(i = 0; i < OFFPERBLK; i++)
			PUT64(b->offs[i]);
		break;
	case TREF:
		for(i = 0; i < REFPERBLK; i++)
			PUT24(b->refs[i]);
		break;
	default:
		abort();
	}
	USED(p);
}

