#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

int main (int argc, char** argv) {
	struct timeval tvalBefore, tvalAfter;
	long delta;

	gettimeofday (&tvalBefore, NULL);
	sleep(2);
	gettimeofday (&tvalAfter, NULL);

	// Changed format to long int (%ld), changed time calculation

	delta = (tvalAfter.tv_sec - tvalBefore.tv_sec)*1000000L
		   +(tvalAfter.tv_usec - tvalBefore.tv_usec);
	printf("Time in microseconds: %ld microseconds\n", delta);

	if(delta > 0)
		return 0;
	return 1;
}
