#include <u.h>
#include <lib9.h>

int done;
int waited;

void
handler(void *v, char *s)
{
	int i;
	if(strcmp(s, "stop") == 0){
		done = 1;
		noted(NCONT);
	}
	print("waiting after %s", s);
	for(i = 0; i < 1000*1000; ++i)
		if(i % 4999 == 0)
			print(".");
	print("\n");
	print("wait after %s terminated\n", s);
	waited++;
	noted(NCONT);
}

void
main(int argc, char**argv)
{
	int fd;
	if(argc > 1){
		fd = ocreate(argv[1], OWRITE, 0666);
		dup(fd, 1);
		close(fd);
	}

	if (notify(handler)){
		fprint(2, "%r\n");
		exits("notify fails");
	}

	print("note handler installed\n");

	while(!done)
		sleep(100);
	if(waited == 2){
		print("PASS\n");
		exits(0);
	}
}
