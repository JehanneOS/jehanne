#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

void catcher(int sig) {
	printf("Signal catcher called for signal %d\n", sig);
}

void timestamp(char *str) {
	time_t t;

	time(&t);
	printf("The time %s is %s", str, ctime(&t));
}

int main(int argc, char *argv[]) {

	int result = 0;
	int err = 0;

	struct sigaction sigact;
	sigset_t waitset;
	int sig;

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = catcher;
	sigaction(SIGALRM, &sigact, NULL);

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGALRM);

	sigprocmask(SIG_BLOCK, &waitset, NULL);

	alarm(3);

	timestamp("before sigwait()");

	result = sigwait(&waitset, &sig);
	err = errno;

	timestamp("after sigwait()");

	if(result == 0){
		printf("sigwait() returned for signal %d\n", sig);
		return 0;
	}

	printf("sigwait() returned %d; errno = %d\n", result, err);
	perror("sigwait() function failed");
	return 1;
}
