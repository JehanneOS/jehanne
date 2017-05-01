#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
	pid_t cpid, w;
	int status;


	cpid = fork();
	if(cpid == -1){
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (cpid == 0) {	/* Code executed by child */
		printf("Child PID is %ld\n", (long) getpid());
		_exit(10);
	}
	/* Code executed by parent */
	do {
		w = waitpid(cpid, &status, WUNTRACED);
		if (w == -1) {
			perror("waitpid");
			exit(EXIT_FAILURE);
		}

		if (WIFEXITED(status)) {
			printf("exited, status=%d\n", WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			printf("killed by signal %d\n", WTERMSIG(status));
			exit(EXIT_FAILURE);
		} else if (WIFSTOPPED(status)) {
			printf("stopped by signal %d\n", WSTOPSIG(status));
			exit(EXIT_FAILURE);
		}
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	exit(EXIT_SUCCESS);

}
