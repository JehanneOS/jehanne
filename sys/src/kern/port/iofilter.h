
enum {
	Niosamples	= 32,
	Lsum		= 0,
	Lmax,
	Lavg,
	Lsz,
};

typedef struct Iofilter Iofilter;
struct Iofilter {
	Lock;
	uint32_t nsamples;			/* total samples taken */
	struct {
		uint32_t b;
		uint32_t lat[Lsz];		/* latency min, max, avg for bytes in b */
	} samples[Niosamples];

	uint32_t bytes;
	uint32_t lmin;
	uint32_t lmax;
	int64_t lsum;
	uint32_t nlat;
};

#pragma	varargck	type	"Z"	Iofilter*

void		incfilter(Iofilter *, uint32_t, uint32_t);
void		delfilter(Iofilter *);
int		addfilter(Iofilter *);
void		zfilter(Iofilter *);
int		filtersum(Iofilter*, uint64_t*, int64_t*, int);
int		filterfmt(Fmt *);
