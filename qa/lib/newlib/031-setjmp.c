#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

/* declare variable of type jmp_buf */
jmp_buf resume_here;

void hello(void);

int main(void)
{
	int ret_val;
	printf("sizeof(jmp_buf) = %lu\n", sizeof(jmp_buf));

	/* Initialize 'resume_here' by calling setjmp() */
	if (ret_val = setjmp(resume_here)) {
		printf("After \'longjump()\', back in \'main()\'\n");
		printf("\'jump buffer variable \'resume_here\'\' becomes "
			  "INVALID!\n");
		return 0;
	} else {
		printf("\'setjmp()\' returns first time\n");
		hello();
		return 1;
	}
}

void hello(void)
{
	printf("Hey, I'm in \'hello()\'\n");
	longjmp(resume_here, 1);

	/* other code */
	printf("can't be reached here because I did longjmp!\n");
}
