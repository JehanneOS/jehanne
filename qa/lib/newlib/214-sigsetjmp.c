#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

sigset_t sigset;
sigjmp_buf mark;
int catcherWasCalled;

void catcher(int);
void p(void);

int main(int argc, char *argv[]) {

	int result = 0;
	int returnCode = 0;

	/*
	 * Block the SIGUSR2 signal.  This signal set will be
	 * saved as part of the environment by the sigsetjmp()
	 * function and subsequently restored by the siglongjmp()
	 * function.
	 */

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR2);
	sigprocmask(SIG_SETMASK, &sigset, NULL);

	/* Save the stack environment and the current signal mask */

	returnCode = sigsetjmp(mark, 1);

	/* Handle the sigsetjmp return code */

	switch(returnCode) {
	case  0:
		printf("sigsetjmp() has been called\n");

		/*
		 * Call function p() which will call the siglongjmp()
		 * function
		 */
		p();

		printf("control returning here is an error\n");
		result=-1;
		break;
	case -1:
		printf("siglongjmp() function was called\n");

		/* Retrieve the current signal mask */

		sigprocmask(SIG_SETMASK, NULL, &sigset);

		/* Verify SIGUSR2 is in sigset */
		if(sigismember(&sigset, SIGUSR2)) {
			  printf("signal mask was restored after siglongjmp()\n");
			  result=0;
		} else {
			  printf("signal mask was not restored after siglongjmp()\n");
			  result=-1;
		}
		break;
	default:
		printf("this unexpected return code is an error\n");
		result=-1;
		break;
	}

	printf("return from main with result %d\n", result);

	return result;
}

void p(void) {

	struct sigaction sigact;
	int error=0;

	printf("performing function p()\n");

	/* Setup signal handler in case error condition is detected */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = catcher;
	sigaction(SIGUSR2, &sigact, NULL);

	/*
	 * Delete SIGUSR2 from the signal set that was initialized
	 * by the main() function. This allows us to demonstrate
	 * that the original signal set saved by the sigsetjmp() function
	 * is restored by the siglongjmp() function.
	 */
	sigdelset(&sigset, SIGUSR2);
	sigprocmask(SIG_SETMASK, &sigset, NULL);

	/* After some processing an error condition is detected */
	error=-1;

	/* Call catcher() function if error is detected */
	if(error != 0) {
		catcherWasCalled = 0;

		/* Send SIGUSR2 to handle the error condition */

		printf("error condition detected, send SIGUSR2 signal\n");
		kill(getpid(), SIGUSR2);

		if(catcherWasCalled == 1) {
			printf("catcher() function handled the error condition\n");

			/*
			 * Perform a nonlocal "goto" and specify -1 for the
			 * return value
			 */

			siglongjmp(mark, -1);

			printf("control getting here is an error\n");
			exit(3);
		}
	}
}

void catcher(int signo) {
	/*
	 * Indicate the catcher() function is handling the
	 * SIGUSR2 signal.
	 */
	catcherWasCalled = 1;
}
