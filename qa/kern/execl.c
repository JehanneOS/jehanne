#include <u.h>
#include <lib9.h>

void
main(int argc, char *argv[])
{
	Waitmsg *w;
	int i;

	for(i = 0; i < argc; i++){
		switch(fork()){
		case -1:
			fprint(2, "fork fail\n");
			exits("FAIL");
		case 0:
			execl("/cmd/echo", "echo", argv[i], nil);
			fprint(2, "execl fail: %r\n");
			exits("FAIL");
		default:
			w = wait();
			if(w->msg[0]){
				print("FAIL\n");
				exits("FAIL");
			}
			break;
		}
	}
	print("PASS\n");
	exits("PASS");
}
