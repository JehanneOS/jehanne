#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

void catcher(int sig, siginfo_t *info, void *p) {
	printf("Signal catcher called for signal %d from %d\n", sig, info->si_pid);
	exit(1);
}

void timestamp(char *str) {
	time_t t;

	time(&t);
	printf("The time %s is %s", str, ctime(&t));
}

void echoSIGUSR1(void){
	int result;
	struct sigaction sigact;
	sigset_t waitset;
	siginfo_t info;
	union sigval v;

	printf("CHILD pid %d\n", getpid());
	v.sival_int = 0;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = catcher;
	sigaction(SIGUSR1, &sigact, NULL);

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGUSR1);

	sigprocmask(SIG_BLOCK, &waitset, NULL);

	while(v.sival_int < 5){
		result = sigwaitinfo(&waitset, &info);
		v.sival_int = 1 + info.si_value.sival_int;
		printf("CHILD sigqueue %d to %d\n", v.sival_int, info.si_pid);
		sigqueue(info.si_pid, result, v);
	}
}

int main(int argc, char *argv[]) {
	int result;
	struct sigaction sigact;
	sigset_t waitset;
	siginfo_t info;
	union sigval v;
	pid_t child;

	switch(child = fork()){
	case 0:
		echoSIGUSR1();
		exit(0);
		break;
	case -1:
		exit(1);
		break;
	default:
		break;
	}

	printf("PARENT pid %d sleep(3)\n", getpid());
	sleep(3);
	v.sival_int = 0;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = catcher;
	sigaction(SIGUSR1, &sigact, NULL);

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGUSR1);

	sigprocmask(SIG_BLOCK, &waitset, NULL);

	result = SIGUSR1;
	do{
		printf("PARENT sigqueue %d\n", v.sival_int);
		sigqueue(child, result, v);
		result = sigwaitinfo(&waitset, &info);
		v.sival_int = 1 + info.si_value.sival_int;
	} while(v.sival_int < 5);
	exit(0);
}
