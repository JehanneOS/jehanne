#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void sighup(); /* routines child will call upon sigtrap */
void sigint();
void sigquit();

int
main() { 
	int pid, p[2], w, status, sig = 0;
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
		if(read(p[0], &dummy, 1) != 1){
			printf("sync read");
			exit(EXIT_FAILURE);
		}
		close(p[1]);
		close(p[0]);
		printf("\nPARENT: sending SIGINT\n\n");
		kill(pid,SIGINT);
		do {
			w = waitpid(pid, &status, WUNTRACED);
			if (w == -1) {
				perror("waitpid");
				exit(EXIT_FAILURE);
			}

			if (WIFEXITED(status)) {
				printf("exited, status=%d\n", WEXITSTATUS(status));
				exit(EXIT_FAILURE);
			} else if (WIFSIGNALED(status)) {
				sig = WTERMSIG(status);
				if(sig == SIGINT){
					printf("killed by SIGINT\n");
				}else{
					printf("killed by signal %d\n", sig);
					exit(EXIT_FAILURE);
				}
			} else if (WIFSTOPPED(status)) {
				printf("stopped by signal %d\n", WSTOPSIG(status));
				exit(EXIT_FAILURE);
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
		exit(EXIT_SUCCESS);
	}
}
