#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

void sighup(); /* routines child will call upon sigtrap */
void sigint();
void sigquit();

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
		
		close(p[1]);
		close(p[0]);
		printf("\nChild going to loop...\n\n");
		for(;;); /* loop for ever */
	}
	else /* parent */
	{
		close(p[1]);
		read(p[0], &dummy, 1);
		close(p[0]);
		printf("\nPARENT: sending SIGHUP\n\n");
		kill(pid,SIGHUP);
		sleep(3); /* pause for 3 secs */
		printf("\nPARENT: sending SIGINT\n\n");
		kill(pid,SIGINT);
		sleep(3); /* pause for 3 secs */
		printf("\nPARENT: sending SIGQUIT\n\n");
		kill(pid,SIGQUIT);
		sleep(3);
	}
}

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
