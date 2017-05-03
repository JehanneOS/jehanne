#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main()
{
	int n;
	char buf[64];
	FILE *fptr;

	fptr = fopen("/tmp/qa-fprint.txt", "w");
	if(fptr == NULL)
		exit(1);

	fprintf(fptr, "%s", "DONE");
	fclose(fptr);

	fptr = fopen("/tmp/qa-fprint.txt", "r");
	if(fptr == NULL)
		exit(2);
	n = fread (buf, 1, 64, fptr);
	buf[n] = '\0';
	fclose(fptr);

	if(strncmp(buf, "DONE", 4) != 0){
		printf("'%s'", buf);
		exit(4);
	}

	return 0;
}
