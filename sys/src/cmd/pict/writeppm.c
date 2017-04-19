#include <u.h>
#include <lib9.h>
#include <draw.h>
#include <memdraw.h>
#include <bio.h>

#define	MAXLINE	70

/* imported from libdraw/arith.c to permit an extern log2 function */
static int log2[] = {
	-1, 0, 1, -1, 2, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1, 4,
	-1, -1, -1, -1, -1, -1, -1, 4 /* BUG */, -1, -1, -1, -1, -1, -1, -1, 5
};

static int bitc = 0;
static int nbit = 0;

static
void
Bputbit(Biobufhdr *b, int c)
{
	if(c >= 0x0){
		bitc = (bitc << 1) | (c & 0x1);
		nbit++;
	}else if(nbit > 0){
		for(; nbit < 8; nbit++)
			bitc <<= 1;
	}
	if(nbit == 8){
		Bputc(b, bitc);
		bitc = nbit = 0;
	}
}

/*
 * Write data
 */
static
char*
writedata(Biobuf *fd, Image *image, Memimage *memimage, int rflag)
{
	char *err;
	unsigned char *data;
	int i, x, y, ndata, depth, col, pix, xmask, pmask;
	uint32_t chan;
	Rectangle r;

	if(memimage != nil){
		r = memimage->r;
		depth = memimage->depth;
		chan = memimage->chan;
	}else{
		r = image->r;
		depth = image->depth;
		chan = image->chan;
	}

	/*
	 * Read image data into memory
	 * potentially one extra byte on each end of each scan line
	 */
	ndata = Dy(r)*(2+Dx(r)*depth/8);
	data = malloc(ndata);
	if(data == nil)
		return "WritePPM: malloc failed";
	if(memimage != nil)
		ndata = unloadmemimage(memimage, r, data, ndata);
	else
		ndata = unloadimage(image, r, data, ndata);
	if(ndata < 0){
		err = malloc(ERRMAX);
		if(err == nil)
			return "WritePPM: malloc failed";
		snprint(err, ERRMAX, "WritePPM: %r");
		free(data);
		return err;
	}

	/* Encode and emit the data */
	col = 0;
	switch(chan){
	case GREY1:
	case GREY2:
	case GREY4:
		pmask = (1<<depth)-1;
		xmask = 7>>log2[depth];
		for(y=r.min.y; y<r.max.y; y++){
			i = (y-r.min.y)*bytesperline(r, depth);
			for(x=r.min.x; x<r.max.x; x++){
				pix = (data[i]>>depth*((xmask-x)&xmask))&pmask;
				if(((x+1)&xmask) == 0)
					i++;
				if(chan == GREY1){
					pix ^= 1;
					if(rflag){
						Bputbit(fd, pix);
						continue;
					}
				} else {
					if(rflag){
						Bputc(fd, pix);
						continue;
					}
				}
				col += Bprint(fd, "%d", pix);
				if(col >= MAXLINE-(2+1)){
					Bprint(fd, "\n");
					col = 0;
				}else if(y < r.max.y-1 || x < r.max.x-1)
					col += Bprint(fd, " ");
			}
			if(rflag)
				Bputbit(fd, -1);
		}
		break;
	case GREY8:
		for(i=0; i<ndata; i++){
			if(rflag){
				Bputc(fd, data[i]);
				continue;
			}
			col += Bprint(fd, "%d", data[i]);
			if(col >= MAXLINE-(4+1)){
				Bprint(fd, "\n");
				col = 0;
			}else if(i < ndata-1)
				col += Bprint(fd, " ");
		}
		break;
	case RGB24:
		for(i=0; i<ndata; i+=3){
			if(rflag){
				Bputc(fd, data[i+2]);
				Bputc(fd, data[i+1]);
				Bputc(fd, data[i]);
				continue;
			}
			col += Bprint(fd, "%d %d %d", data[i+2], data[i+1], data[i]);
			if(col >= MAXLINE-(4+4+4+1)){
				Bprint(fd, "\n");
				col = 0;
			}else if(i < ndata-3)
				col += Bprint(fd, " ");
		}
		break;
	default:
		return "WritePPM: can't handle channel type";
	}

	return nil;
}

static
char*
writeppm0(Biobuf *fd, Image *image, Memimage *memimage, Rectangle r, int chan, char *comment, int rflag)
{
	char *err;

	switch(chan){
	case GREY1:
		Bprint(fd, "%s\n", rflag? "P4": "P1");
		break;
	case GREY2:
	case GREY4:
	case GREY8:
		Bprint(fd, "%s\n", rflag? "P5": "P2");
		break;
	case RGB24:
		Bprint(fd, "%s\n", rflag? "P6": "P3");
		break;
	default:
		return "WritePPM: can't handle channel type";
	}

	if(comment!=nil && comment[0]!='\0'){
		Bprint(fd, "# %s", comment);
		if(comment[strlen(comment)-1] != '\n')
			Bprint(fd, "\n");
	}
	Bprint(fd, "%d %d\n", Dx(r), Dy(r));

	/* maximum pixel value */
	switch(chan){
	case GREY2:
		Bprint(fd, "%d\n", 3);
		break;
	case GREY4:
		Bprint(fd, "%d\n", 15);
		break;
	case GREY8:
	case RGB24:
		Bprint(fd, "%d\n", 255);
		break;
	}

	err = writedata(fd, image, memimage, rflag);

	if(!rflag)
		Bprint(fd, "\n");
	Bflush(fd);
	return err;
}

char*
writeppm(Biobuf *fd, Image *image, char *comment, int rflag)
{
	return writeppm0(fd, image, nil, image->r, image->chan, comment, rflag);
}

char*
memwriteppm(Biobuf *fd, Memimage *memimage, char *comment, int rflag)
{
	return writeppm0(fd, nil, memimage, memimage->r, memimage->chan, comment, rflag);
}
