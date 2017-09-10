#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int child, cstatus;

void sighup() {
	signal(SIGHUP,sighup); /* reset signal */
	printf("CHILD: I have received a SIGHUP\n");
}

void sigint() {
	signal(SIGINT,sigint); /* reset signal */
	printf("CHILD: I have received a SIGINT\n");
}

void sigquit() {
	printf("My DADDY has Killed me!!!\n");
	exit(0);
}

void sigchld() {
	printf("PARENT: got SIGCHLD\n");
#ifdef WITH_SIGCHLD
	child = wait(&cstatus);
	if(cstatus == 0)
		exit(0);
	else
	{
		printf("PARENT: child exited with status %d\n", cstatus);
		exit(1);
	}
#endif
}

int
main() { 
	int pid, p[2];
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
		printf("\nI am the new child!\n\n");
		signal(SIGHUP,sighup); /* set function calls */
		signal(SIGINT,sigint);
		signal(SIGQUIT, sigquit);
		
		printf("\nChild going to loop...\n\n");
		write(p[1], "", 1);
		close(p[1]);
		close(p[0]);
		for(;;); /* loop for ever */
	}
	else /* parent */
	{
		signal(SIGCHLD,sigchld);
		if(read(p[0], &dummy, 1) != 1){
			printf("sync read");
			exit(EXIT_FAILURE);
		}
		close(p[1]);
		close(p[0]);
		printf("\nPARENT: sending SIGHUP\n\n");
		kill(pid,SIGHUP);
		sleep(3); /* pause for 3 secs */
		printf("\nPARENT: sending SIGINT\n\n");
		kill(pid,SIGINT);
		sleep(3); /* pause for 3 secs */
		printf("\nPARENT: sending SIGQUIT\n\n");
		kill(pid,SIGQUIT);

#ifdef WITH_SIGCHLD
		sleep(10000);
		exit(1);
#else
		child = wait(&cstatus);
		if(child == pid && cstatus == 0)
			exit(0);
		else
		{
			printf("PARENT: child exited with status %d\n", cstatus);
			exit(1);
		}
#endif
	}
}
