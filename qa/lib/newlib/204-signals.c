#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int child, cstatus;

void sigquit() {
	printf("CHILD: got SIGQUIT\n");
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
		printf("%d is the new child!\n", getpid());
		signal(SIGHUP, SIG_IGN);
		signal(SIGQUIT, sigquit);

		printf("Child going to loop...\n");
		write(p[1], "", 1);
		close(p[1]);
		close(p[0]);
		for(;;); /* loop for ever */
	}
	else /* parent */
	{
		signal(SIGCHLD,sigchld);
		read(p[0], &dummy, 1);
		close(p[1]);
		close(p[0]);
		printf("PARENT: sending SIGHUP\n");
		kill(pid,SIGHUP);
		sleep(3); /* pause for 3 secs */
		printf("PARENT: sending SIGQUIT\n");
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
