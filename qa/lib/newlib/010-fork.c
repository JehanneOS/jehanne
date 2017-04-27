#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
	int counter = 0;
	int status;

	printf("parent: pid = %d; ppid = %d\n", getpid(), getppid());

	pid_t pid = fork();

	if (pid == 0)
	{
		// child process
		printf("child: pid = %d; ppid = %d\n", getpid(), getppid());
		int i = 0;
		for (; i < 5; ++i)
		{
			printf("child: counter=%d\n", ++counter);
		}
	}
	else if (pid > 0)
	{
		// parent process
		int j = 0;
		for (; j < 5; ++j)
		{
			printf("parent: counter=%d\n", ++counter);
		}
		wait(&status);
		if(status != 0){
			printf("parent: child exited with status %d\n", status);
			return 2;
		}
	}
	else
	{
		// fork failed
		printf("parent: fork() failed!\n");
		return 1;
	}

	return 0;
}

