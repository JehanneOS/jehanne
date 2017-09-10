#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

sigset_t parent_mask;

int
main()
{
	int status;
	sigset_t mask;

	sigprocmask(SIG_BLOCK, NULL, &parent_mask);
	sigaddset(&parent_mask, SIGSEGV);
	sigprocmask(SIG_BLOCK, &parent_mask, NULL);
	printf("SIGSEGV blocked in parent. Forking...\n");

	switch(fork()){
	case -1:
		exit(1);
	case 0:
		sigprocmask(SIG_BLOCK, NULL, &mask);
		printf("Is SIGSEGV (%llx) a member of %llx?\n", 1ULL<<(SIGSEGV-1), (long long unsigned int)mask);
		if(sigismember(&mask, SIGSEGV) != 1){
			printf("FAIL: SIGSEGV is not present in child's mask after fork\n");
			exit(2);
		}
		printf("PASS\n");
		exit(0);
	default:
		wait(&status);
		status = WEXITSTATUS(status);
		exit(status);
	}
	
}
