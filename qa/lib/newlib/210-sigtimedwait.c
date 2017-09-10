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
	printf("The time %s is %s\n", str, ctime(&t));
}

int main(int argc, char *argv[]) {

	int result = 0;
	int err = 0;

	struct sigaction sigact;
	sigset_t waitset;
	siginfo_t info;
	struct timespec timeout;

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = catcher;
	sigaction(SIGALRM, &sigact, NULL);

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGALRM);

	sigprocmask(SIG_BLOCK, &waitset, NULL);

	timeout.tv_sec = 4;	 /* Number of seconds to wait */
	timeout.tv_nsec = 1000;  /* Number of nanoseconds to wait */

	alarm(2);

	timestamp("before sigtimedwait()");

	result = sigtimedwait(&waitset, &info, &timeout);
	err = errno;

	timestamp("after sigtimedwait()");

	if(result > 0){
		printf("sigtimedwait() returned for signal %d\n", info.si_signo);
		return 0;
	}

	printf("sigtimedwait() returned %d; errno = %d\n", result, err);
	perror("sigtimedwait() function failed");
	return 1;
}
