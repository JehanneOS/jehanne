#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void catcher(int sig) {
	puts("Got SIGUSR1");
}

int check_pending(int sig, char *signame) {

	sigset_t sigset;

	if(sigpending(&sigset) != 0){
		perror("sigpending() error\n");
		exit(1);
	}
	if(sigismember(&sigset, sig)){
		printf("a %s (%d) signal is pending\n", signame, sig);
		return 1;
	} else {
		printf("no %s (%d) signals are pending\n", signame, sig);
		return 0;
	}
}

int main(int argc, char *argv[]) {

	struct sigaction sigact;
	sigset_t sigset;

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = catcher;

	if(sigaction(SIGUSR1, &sigact, NULL) != 0){
		perror("sigaction() error\n");
		return 2;
	}

	printf("Calling sigprocmask to block SIGUSR1...\n");
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	if (sigprocmask(SIG_SETMASK, &sigset, NULL) != 0){
		perror("sigprocmask() error\n");
		return 3;
	}
	printf("SIGUSR1 signals are now blocked\n");

	kill(getpid(), SIGUSR1);
	printf("kill(getpid(), SIGUSR1) DONE\n");

	if(!check_pending(SIGUSR1, "SIGUSR1")){
		printf("FAIL: SIGUSR1 is not pending despite the mask\n");
		return 4;
	}

	printf("Calling sigprocmask to unblock SIGUSR1...\n");
	sigemptyset(&sigset);
	sigprocmask(SIG_SETMASK, &sigset, NULL);
	printf("SIGUSR1 signals are no longer blocked\n");

	if(check_pending(SIGUSR1, "SIGUSR1")){
		printf("FAIL: SIGUSR1 is still pending despite the mask\n");
		return 4;
	}

	return 0;
}
