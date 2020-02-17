#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int p[2];

void sigcont() {
	signal(SIGCONT,sigcont); /* reset signal */
	printf("CHILD: Got SIGCONT\n");
	exit(0);
}

void sigstop() {
	signal(SIGSTOP,sigstop); /* reset signal */
	printf("CHILD: Got SIGSTOP\n");
	exit(1);
}

void childloop(void)
{
	signal(SIGCONT,sigcont); /* set function calls */
	signal(SIGSTOP,sigstop); /* set function calls */
	printf("Child %d going to loop...\n", getpid());
	write(p[1], "", 1);
	close(p[1]);
	close(p[0]);
	for(;;){
		/* loop for ever */
		printf(".");
		usleep(500);
	}
	exit(0);
}

int
main() { 
	int pid, child, cstatus;
	char dummy[1];

	/* get child process */

	if(pipe(p)){
		perror("pipe");
		exit(1);
	}
	if ((pid = fork()) < 0) {
		perror("fork");
		exit(2);
	}

	if (pid == 0) {
		childloop();
	}
	else /* parent */
	{
		read(p[0], &dummy, 1);
		close(p[1]);
		close(p[0]);
		sleep(2);
		printf("PARENT: sending SIGSTOP\n");
		kill(pid,SIGSTOP);
		sleep(4);
		
		printf("PARENT: sending SIGCONT\n");
		kill(pid,SIGCONT);
		
		child = wait(&cstatus);
		if(child == pid && cstatus == 0)
			exit(0);
		else
		{
			printf("PARENT: child exited with status %d\n", cstatus);
			exit(1);
		}
	}
}
