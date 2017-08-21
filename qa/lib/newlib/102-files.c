#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

int
main(int argc, char **argv)
{
	int i = 0;
	DIR *dirp;
	struct dirent *dp;

	dirp = opendir("/");
	if(dirp != NULL)
		printf("opendir(/): done\n");
	else
		printf("opendir(/): failed\n");

	while (dirp) {
		errno = 0;
		if ((dp = readdir(dirp)) != NULL) {
			if (strcmp(dp->d_name, "README.md") == 0) {
				if(dp->d_type == DT_REG){
					++i;
					printf("FOUND! ");
				}
			}
			if (strcmp(dp->d_name, "sys") == 0) {
				if(dp->d_type == DT_DIR){
					++i;
					printf("FOUND! ");
				}
			}
			printf("%s: %s\n", dp->d_name, dp->d_type == DT_DIR ? "directory" : "regular");
			if(i == 2){
				closedir(dirp);
				return 0;
			}
		} else {
			printf("readdir returned NULL\n");
			if (errno == 0) {
				closedir(dirp);
				return 1;
			}
			closedir(dirp);
			return 2;
		}
	}
	return 3;
}
