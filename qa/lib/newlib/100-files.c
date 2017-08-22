#include <stdio.h>

int
main(int argc, char** argv) {
	FILE *fp;
	int i;
	char * output = "/tmp/io-test.txt";

	if(argc == 2){
		output = argv[1];
	}else if(argc > 2){
		printf("usage: %s [output]", argv[0]);
		return 100;
	}

	fp = fopen(output, "w+");
	if(fp == NULL){
		printf("Can't open %s", output);
		return 1;
	}
	i = fprintf(fp, "%s works.\n", "fprintf");
	if(i <= 0){
		fclose(fp);
		printf("Can't write 'fprintf works.' to %s (fprintf returned %d)", output, i);
		return 2;
	}
	i = fputs("fputs works.\n", fp);
	if(i == EOF){
		fclose(fp);
		printf("Can't write 'fputs works.' to %s (fputs returned EOF)", output);
		return 3;
	}
	fclose(fp);
	return 0;
}
