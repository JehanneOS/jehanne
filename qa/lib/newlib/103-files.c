#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

int
main(int argc, char **argv)
{
	int ret;
	char buf[256];

	ret = chdir("/tmp");
	if(ret != 0){
		printf("FAIL: chdir(/tmp) returns %d\n", ret);
		exit(1);
	}

	ret = chdir("/inexistent_folder");
	if(ret != -1 || errno != ENOENT){
		printf("FAIL: chdir(/inexistent_folder) returns %d; errno %d\n", ret, errno);
		exit(2);
	}

	if(getcwd(buf, sizeof(buf)) == NULL){
		perror("FAIL: getcwd failed");
		exit(3);
	}
	if(strcmp("/tmp", buf) != 0){
		printf("FAIL: getcwd() returns %s instead of /tmp\n", buf);
		exit(4);
	}

	ret = mkdir("qa-files", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if(ret != 0){
		printf("FAIL: mkdir(qa-files) in /tmp returns %d\n", ret);
		exit(5);
	} else {
		printf("mkdir(\"qa-files\", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0\n");
	}

	ret = access("/tmp/qa-files", F_OK);
	if(ret != 0){
		printf("FAIL: access(\"/tmp/qa-files\", F_OK) returned %d; errno %d\n", ret, errno);
		exit(6);
	} else {
		printf("access(\"/tmp/qa-files\", F_OK) == 0\n");
	}
	ret = access("/tmp/qa-files", R_OK);
	if(ret != 0){
		printf("FAIL: access(\"/tmp/qa-files\", R_OK) returned %d; errno %d\n", ret, errno);
		exit(7);
	} else {
		printf("access(\"/tmp/qa-files\", R_OK) == 0\n");
	}
	ret = access("/tmp/qa-files", W_OK);
	if(ret != 0){
		printf("FAIL: access(\"/tmp/qa-files\", W_OK) returned %d; errno %d\n", ret, errno);
		exit(7);
	} else {
		printf("access(\"/tmp/qa-files\", W_OK) == 0\n");
	}
	ret = access("/tmp/qa-files", X_OK);
	if(ret != 0){
		printf("FAIL: access(\"/tmp/qa-files\", X_OK) returned %d; errno %d\n", ret, errno);
		exit(8);
	} else {
		printf("access(\"/tmp/qa-files\", X_OK) == 0\n");
	}

	return 0;
}
