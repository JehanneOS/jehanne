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
typedef struct Block Block;
struct Block
{
	Ref	ref;

	Block	*next;

	uint8_t	*rp;
	uint8_t	*wp;
	uint8_t	*lim;

	uint8_t	base[];
};

#define BLEN(s)	((s)->wp - (s)->rp)

Block*	allocb(int size);
void	freeb(Block*);
Block*	copyblock(Block*, int);

typedef struct Ehdr Ehdr;
struct Ehdr
{
	uint8_t	d[6];
	uint8_t	s[6];
	uint8_t	type[2];
};

enum {
	Ehdrsz	= 6+6+2,
	Maxpkt	= 2000,
};

enum
{
	Cdcunion = 6,
	Scether = 6,
	Fnether = 15,
};

int debug;
int setmac;

/* to be filled in by *init() */
uint8_t macaddr[6];

void	etheriq(Block*, int wire);

int	(*epreceive)(Dev*);
void	(*eptransmit)(Dev*, Block*);
