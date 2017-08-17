#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>


void sighup() {
	signal(SIGHUP,sighup); /* reset signal */
	printf("CHILD: SIGHUP received\n");
}

int
main(int argc, char *argv[])
{
	pid_t cpid, w;
	int ret, status;


	cpid = fork();
	if(cpid == -1){
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (cpid == 0) {	/* Code executed by child */
		signal(SIGHUP, sighup); /* set function calls */

		printf("Child PID is %ld, calling pause()...\n", (long) getpid());
		ret = pause();

		if(ret == -1 && errno == EINTR){
			printf("Pause returned %d with errno = EINTR\n", ret);
			_exit(0);
		}
		if(ret != -1)
			printf("Pause returned %d\n", ret);
		if(errno != EINTR)
			printf("Pause returned but errno != EINTR\n");
		_exit(1);
	}

	/* Code executed by parent */
	sleep(5);
	kill(cpid, SIGHUP);

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
