#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int
main ()
{
	int child, status;
	printf("Parent $PATH = %s\n", getenv("PATH"));
	printf("Parent $HOME = %s\n", getenv("HOME"));
	printf("Parent $USER = %s\n", getenv("USER"));
	printf("Parent $IFS  = %s\n", getenv("IFS"));
	printf("Parent $ROOT = %s\n", getenv("ROOT"));
	printf("Parent $TEST = %s\n", getenv("TEST"));
	putenv("TEST=value");
	printf("Parent putenv(TEST=value); now $TEST = %s\n", getenv("TEST"));
	fflush(stdout);

	switch(child = fork()){
	case 0:
		printf("Child $PATH = %s\n", getenv("PATH"));
		printf("Child $HOME = %s\n", getenv("HOME"));
		printf("Child $USER = %s\n", getenv("USER"));
		printf("Child $IFS  = %s\n", getenv("IFS"));
		printf("Child $ROOT = %s\n", getenv("ROOT"));
		printf("Child $TEST = %s\n", getenv("TEST"));
		exit(0);
	case -1:
		printf("FAIL: fork\n");
		return 1;
	default:
		wait(&status);
		break;
	}

	unsetenv("TEST");
	printf("Parent unsetenv(TEST); now $TEST = %s\n", getenv("TEST"));

	if(status)
		printf("FAIL: child returned %d\n", status);
	exit(status);
}
