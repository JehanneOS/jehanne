#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

void
fail_if_still_open(int fd)
{
	if(fd == 0){
		printf("atoi failed\n");
		exit(2);
	}
	if(fcntl(fd, F_GETFD) != -1){
		printf("fd %d is still open\n", fd);
		exit(3);
	}
}

int
main(int argc, char *argv[])
{
	int sync[2];
	int outfd;
	int nullfd;
	int tmp;
	char *eargv[7], *p;
	char buf[128];

	if(argc == 1){
		printf("cat /proc/%d/fd\n", getpid());

		pipe(sync);
		outfd = dup(1);
		nullfd = open("/dev/null", O_WRONLY);
		tmp = fcntl(sync[0], F_DUPFD_CLOEXEC, 1);
		close(sync[0]);
		sync[0] = tmp;
		tmp = fcntl(sync[1], F_DUPFD_CLOEXEC, 1);
		close(sync[1]);
		sync[1] = tmp;
		tmp = fcntl(outfd, F_DUPFD_CLOEXEC, 1);
		close(outfd);
		outfd = tmp;
		tmp = fcntl(nullfd, F_DUPFD_CLOEXEC, 1);
		close(nullfd);
		nullfd = tmp;

		eargv[0] = argv[0];
		p = buf;
		eargv[1] = p;
		p += 1+sprintf(p, "%d", sync[0]);
		eargv[2] = p;
		p += 1+sprintf(p, "%d", sync[1]);
		eargv[3] = p;
		p += 1+sprintf(p, "%d", outfd);
		eargv[4] = p;
		p += 1+sprintf(p, "%d", nullfd);
		eargv[5] = NULL;

		execvp(argv[0], eargv);
		printf("execvp returned\n");
		exit(100);

	} else if(argc != 5){
		printf("argc = %d (should be 5)\n", argc);
		exit(1);
	}

	printf("argc = %d; fds: %s %s %s %s\n", argc, argv[1], argv[2], argv[3], argv[4] );
	sync[0] = atoi(argv[1]);
	sync[1] = atoi(argv[2]);
	outfd = atoi(argv[3]);
	nullfd = atoi(argv[4]);

	fail_if_still_open(sync[0]);
	fail_if_still_open(sync[1]);
	fail_if_still_open(outfd);
	fail_if_still_open(nullfd);

	exit(0);
}
