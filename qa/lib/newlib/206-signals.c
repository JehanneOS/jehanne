#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void sigusr1() {
	printf("Got SIGUSR1\n");
	exit(0);
}

int
main()
{
	sigset_t old_set,  new_set;
	sigemptyset(&old_set);
	sigemptyset(&new_set);

	signal(SIGUSR1, sigusr1);

	if(sigaddset(&new_set, SIGSEGV) == 0)
	{
		printf("sigaddset successfully added for SIGSEGV\n");
	}
	sigprocmask(SIG_BLOCK, &new_set, &old_set);

	printf("raise(SIGSEGV)\n");
	raise(SIGSEGV);

	if(sigaddset(&new_set, SIGUSR1) == 0)
	{
		printf("sigaddset successfully added for SIGUSR1\n");
	}
	if(sigprocmask(SIG_BLOCK, &new_set, &old_set) == -1)
	{
		perror("sigprocmask");
	}

	printf("raise(SIGUSR1)\n");
	raise(SIGUSR1);

	sigemptyset(&new_set);
	sigaddset(&new_set, SIGUSR1);

	printf("unblock SIGUSR1 via sigprocmask\n");
	if(sigprocmask(SIG_UNBLOCK, &new_set, &old_set) == -1)
	{
		perror("sigprocmask");
	}

	return 1;
}
