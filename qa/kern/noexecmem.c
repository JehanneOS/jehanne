#include <u.h>
#include <lib9.h>
#define RET 0xc3

int success;
int cases;

void
handler(void *v, char *s)
{
	success++;
	exits("PASS");
}

void
callinsn(char *name, char *buf)
{
	void (*f)(void);
	if (sys_notify(handler)){
		fprint(2, "%r\n");
		exits("sys_notify fails");
	}


	f = (void *)buf;
	f();
	print("FAIL %s\n", name);
	exits("FAIL");
}

void
writeptr(char *name, void *ptr)
{
	if (sys_notify(handler)){
		fprint(2, "%r\n");
		exits("sys_notify fails");
	}

	*(uintptr_t*)ptr = 0xdeadbeef;
	print("FAIL %s\n", name);
	exits("FAIL");
}

void
main(void)
{
	char *str = "hello world";
	char stk[128];
	char *mem;

	switch(sys_rfork(RFMEM|RFPROC)){
	case -1:
		sysfatal("sys_rfork");
	case 0:
		stk[0] = RET;
		callinsn("exec stack", stk);
	default:
		cases++;
		waitpid();
	}

	switch(sys_rfork(RFMEM|RFPROC)){
	case -1:
		sysfatal("sys_rfork");
	case 0:
		mem = malloc(128);
		mem[0] = RET;
		callinsn("exec heap", mem);
	default:
		cases++;
		waitpid();
	}

	switch(sys_rfork(RFMEM|RFPROC)){
	case -1:
		sysfatal("sys_rfork");
	case 0:
		writeptr("write code", (void*)&main);
	default:
		cases++;
		waitpid();
	}

	switch(sys_rfork(RFMEM|RFPROC)){
	case -1:
		sysfatal("sys_rfork");
	case 0:
		writeptr("write rodata", (void*)str);
	default:
		cases++;
		waitpid();
	}

	if(success == cases){
		print("PASS\n");
		exits("PASS");
	}
	print("FAIL\n");
	exits("FAIL");
}
