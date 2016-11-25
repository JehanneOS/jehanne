#pragma	lib	"libflate.a"
#pragma	src	"/sys/src/lib/flate"

/*
 * errors from deflate, deflateinit, deflateblock,
 * inflate, inflateinit, inflateblock.
 * convertable to a string by flateerr
 */
enum
{
	FlateOk			= 0,
	FlateNoMem		= -1,
	FlateInputFail		= -2,
	FlateOutputFail		= -3,
	FlateCorrupted		= -4,
	FlateInternal		= -5,
};

int	deflateinit(void);
int	deflate(void *wr, int (*w)(void*, void*, int), void *rr, int (*r)(void*, void*, int), int level, int debug);

int	inflateinit(void);
int	inflate(void *wr, int (*w)(void*, void*, int), void *getr, int (*get)(void*));

int	inflateblock(uint8_t *dst, int dsize, uint8_t *src, int ssize);
int	deflateblock(uint8_t *dst, int dsize, uint8_t *src, int ssize, int level, int debug);

int	deflatezlib(void *wr, int (*w)(void*, void*, int), void *rr, int (*r)(void*, void*, int), int level, int debug);
int	inflatezlib(void *wr, int (*w)(void*, void*, int), void *getr, int (*get)(void*));

int	inflatezlibblock(uint8_t *dst, int dsize, uint8_t *src, int ssize);
int	deflatezlibblock(uint8_t *dst, int dsize, uint8_t *src, int ssize, int level, int debug);

char	*flateerr(int err);

uint32_t	*mkcrctab(uint32_t);
uint32_t	blockcrc(uint32_t *tab, uint32_t crc, void *buf, int n);

uint32_t	adler32(uint32_t adler, void *buf, int n);
