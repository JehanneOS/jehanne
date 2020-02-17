#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void sigcont() {
	printf("CHILD: Got SIGCONT\n");
	exit(0);
}

int
main() { 
	int pid, p[2], child, cstatus;
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
		signal(SIGCONT,sigcont); /* set function calls */
		printf("Child going to loop...\n");
		write(p[1], "", 1);
		close(p[1]);
		close(p[0]);
		for(;;){
			/* loop for ever */
			printf(".");
			usleep(500);
		}
	}
	else /* parent */
	{
		read(p[0], &dummy, 1);
		close(p[1]);
		close(p[0]);
		printf("PARENT: sending SIGCONT\n\n");
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
