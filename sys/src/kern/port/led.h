typedef struct Ledport Ledport;

struct Ledport {
	uint8_t	nled;
	uint8_t	led;
	uint16_t	ledbits;		/* implementation dependent */
};

/* http://en.wikipedia.org/wiki/IBPI */
enum {
	Ibpinone,
	Ibpinormal,
	Ibpilocate,
	Ibpifail,
	Ibpirebuild,
	Ibpipfa,
	Ibpispare,
	Ibpicritarray,
	Ibpifailarray,
	Ibpilast,
};

char	*ledname(int);
int	name2led(char*);
int32_t	ledr(Ledport*, Chan*, void*, int32_t, int64_t);
int32_t	ledw(Ledport*, Chan*, void*, int32_t, int64_t);
