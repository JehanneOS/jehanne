// C program to illustrate
// non I/O blocking calls
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h> // library for fcntl function
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

#define MSGSIZE 6
char* msg1 ="hello";
char* msg2 ="bye !!";
 
void parent_read(int p[])
{
	int nread;
	char buf[MSGSIZE];
 
	// write link
	close(p[1]);
 
	while (1) {
 
		// read call if return -1 then pipe is
		// empty because of fcntl
		nread = read(p[0], buf, MSGSIZE);
		switch (nread) {
		case -1:
 
			// case -1 means pipe is empty and errono
			// set EAGAIN
			if (errno == EAGAIN) {
				printf("(pipe empty)\n");
				sleep(1);
				break;
			}
 
			else {
				perror("read");
				exit(4);
			}
 
		// case 0 means all bytes are read and EOF(end of conv.)
		case 0:
			printf("End of conversation\n");
 
			// read link
			close(p[0]);
 
			exit(0);
		default:
 
			// text read
			// by default return no. of bytes
			// which read call read at that time
			printf("MSG = %s\n", buf);
		}
	}
}
void child_write(int p[])
{
	int i;
 
	// read link
	close(p[0]);
 
	// write 3 times "hello" in 3 second interval
	for (i = 0; i < 3; i++) {
		write(p[1], msg1, MSGSIZE);
		sleep(3);
	}
 
	// write "bye" one times
	write(p[1], msg2, MSGSIZE);
 
	// here after write all bytes then write end
	// doesn't close so read end block but
	// because of fcntl block doesn't happen..
	exit(0);
}
int main()
{
	int p[2];
 
	// error checking for pipe
	if (pipe(p) < 0)
		exit(1);
 
	// error checking for fcntl
	if (fcntl(p[0], F_SETFL, O_NONBLOCK) < 0)
		exit(2);
 
	// continued
	switch (fork()) {
 
	// error
	case -1:
		exit(3);
 
	// 0 for child process
	case 0:
		child_write(p);
		break;
 
	default:
		parent_read(p);
		break;
	}
	return 0;
}
