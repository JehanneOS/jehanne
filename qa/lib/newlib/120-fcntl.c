#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

// see http://www.informit.com/articles/article.aspx?p=99706&seqNum=13
int
main(int argc, char *argv[])
{

	int fd, accmode, val;

	if (argc != 2)
		fd = 0;
	else
		fd = atoi(argv[1]);

	if ( (val = fcntl(fd, F_GETFL, 0)) < 0){
		perror("fcntl error for fd");
		exit(1);
	}

	printf("fcntl(%d) returns %d\n", fd, val);

	accmode = val & O_ACCMODE;
	if (accmode == O_RDONLY)
		printf("read only");
	else if (accmode == O_WRONLY)
		printf("write only");
	else if (accmode == O_RDWR)
		printf("read write");
	else {
		perror("unknown access mode");
		exit(1);
	}

	if (val & O_APPEND)
		printf(", append");
	if (val & O_NONBLOCK)
		printf(", nonblocking");
#if !defined(_POSIX_SOURCE) && defined(O_SYNC)
	if (val & O_SYNC)
		printf(", synchronous writes");
#endif
	putchar('\n');
	exit(0);
}
