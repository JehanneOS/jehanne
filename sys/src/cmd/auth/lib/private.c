#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

static char *pmsg = "Warning! %s can't protect itself from debugging: %r\n";
static char *smsg = "Warning! %s can't turn off swapping: %r\n";

/* don't allow other processes to debug us and steal keys */
void
private(void)
{
	int fd;
	char buf[64];

	snprint(buf, sizeof(buf), "#p/%d/ctl", getpid());
	fd = open(buf, OWRITE);
	if(fd < 0){
		fprint(2, pmsg, argv0);
		return;
	}
	if(fprint(fd, "private") < 0)
		fprint(2, pmsg, argv0);
	if(fprint(fd, "noswap") < 0)
		fprint(2, smsg, argv0);
	close(fd);
}
