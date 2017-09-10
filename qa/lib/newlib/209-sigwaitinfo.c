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
	siginfo_t info;

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = catcher;
	sigaction(SIGALRM, &sigact, NULL);

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGALRM);

	sigprocmask(SIG_BLOCK, &waitset, NULL);

	alarm(3);

	timestamp("before sigwaitinfo()");

	result = sigwaitinfo(&waitset, &info);
	err = errno;

	timestamp("after sigwaitinfo()");

	if(result > 0){
		printf("sigwaitinfo() returned for signal %d\n", info.si_signo);
		return 0;
	}

	printf("sigwaitinfo() returned %d; errno = %d\n", result, err);
	perror("sigwaitinfo() function failed");
	return 1;
}
