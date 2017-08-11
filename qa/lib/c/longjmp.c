
#include <u.h>
#include <lib9.h>

// from kernel's mem.h
#define MiB		(1024*1024)
#define TSTKTOP		(0x00007ffffffff000ull)
#define USTKSIZE	(16*MiB)			/* size of user stack */
#define USTKTOP		(TSTKTOP-USTKSIZE)		/* end of new stack in sysexec */

enum {
	Njmps = 10000
};

void foo(void);

void
main(void)
{
	int i, njmp;
	int fail = 0;
	jmp_buf label;

	njmp = 0;
	while((njmp = setjmp(label)) < Njmps)
		longjmp(label, njmp+1);

	for(i = 0; i < nelem(label); i++)
		fprint(2, "label[%d] = %p\n", i, label[i]);
	fprint(2, "main: %p foo: %p\n", main, foo);

	if(njmp != Njmps){
		print("error: njmp = %d\n", njmp);
		fail++;
	}
	if(label[JMPBUFPC] < (uintptr_t)main){
		print("error: label[JMPBUFPC] = %#p\n", label[JMPBUFPC]);
		fail++;
	}
	if(label[JMPBUFPC] > (uintptr_t)foo){
		print("error: label[JMPBUFPC] = %#p\n", label[JMPBUFPC]);
		fail++;
	}
	if(label[JMPBUFSP] > (uintptr_t)&label[nelem(label)]){
		print("error: label[JMPBUFSP] (%#p) is greater then &label[nelem(label)] (%#p) \n", label[JMPBUFPC], &label[nelem(label)]);
		fail++;
	}
	if(label[JMPBUFSP] < USTKTOP-USTKSIZE){
		print("error: label[JMPBUFSP] (%#p) is lower then USTKTOP-USTKSIZE \n", label[JMPBUFSP]);
		fail++;
	}

	if(fail == 0){
		print("PASS\n");
		exits("PASS");
	}
	print("FAIL\n");
	exits("FAIL");
}

void
foo(void)
{
}
