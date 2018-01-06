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
void*	emalloc(int);
void*	erealloc(void*,int);
char*	estrdup(char*);
void	bufinit(int);
Buf*	getbuf(Dev *, uint64_t, int, int);
void	putbuf(Buf *);
void	sync(int);
void	pack(Buf *, uint8_t *);
void	unpack(Buf *, uint8_t *);
Dev*	newdev(char *);
ThrData*	getthrdata(void);
Fs*	initfs(Dev *, int, int);
Dentry*	getdent(FLoc *, Buf *);
int	getfree(Fs *, uint64_t *);
int	putfree(Fs *, uint64_t);
Chan*	chanattach(Fs *, int);
Chan*	chanclone(Chan *);
int	chanwalk(Chan *, char *);
int	chancreat(Chan *, char *, int, int);
int	chanopen(Chan *, int mode);
int	chanwrite(Chan *, void *, uint32_t, uint64_t);
int	chanread(Chan *, void *, uint32_t, uint64_t);
int	chanstat(Chan *, Dir *);
int	chanwstat(Chan *, Dir *);
int	permcheck(Fs *, Dentry *, short, int);
char *	uid2name(Fs *, short, char *);
int	name2uid(Fs *, char *, short *);
void	start9p(char *, char **, int);
int	chanclunk(Chan *);
int	chanremove(Chan *);
int	getblk(Fs *, FLoc *, Buf *, uint64_t, uint64_t *, int);
void	initcons(char *);
void	shutdown(void);
int	fsdump(Fs *);
int	willmodify(Fs *, Loc *, int);
void	chbegin(Chan *);
void	chend(Chan *);
int	newqid(Fs *, uint64_t *);
Loc *	getloc(Fs *, FLoc, Loc *);
int	haveloc(Fs *, uint64_t, int, Loc *);
Loc *	cloneloc(Fs *, Loc *);
void	putloc(Fs *, Loc *, int);
int	findentry(Fs *, FLoc *, Buf *, char *, FLoc *, int);
void	modified(Chan *, Dentry *);
int	trunc(Fs *, FLoc *, Buf *, uint64_t);
int	dprint(char *fmt, ...);
int	delete(Fs *, FLoc *, Buf *);
int	chref(Fs *, uint64_t, int);
int	newentry(Fs *, Loc *, Buf *, char *, FLoc *, int);
int	namevalid(char *);
int	usersload(Fs *, Chan *);
int	userssave(Fs *, Chan *);
int	ingroup(Fs *, short, short, int);
void	workerinit(void);
void	writeusers(Fs *);
void	readusers(Fs *);
