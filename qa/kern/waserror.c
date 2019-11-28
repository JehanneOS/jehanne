// Ensure that many calls to waserror:
// -- return an error
// -- complete
// Since we're going to improve waserror
#include <u.h>
#include <lib9.h>

void
main(void)
{
	char buf[1];
	int i;
	// Just to be sure.
	if (sys_close(3) >= 0) {
		print("waserror: sys_close of 3 did not get an error\n");
		exits("FAIL");
	}

	for(i = 0; i < 100000; i++) {
		if (jehanne_read(3, buf, 1) >= 0){
			print("waserror: read of 3 did not get an error\n");
			exits("FAIL");
		}
	}
	print("PASS\n");
	exits("PASS");
}
