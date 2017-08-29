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

	dirp = opendir("/usr/glenda");
	if(dirp != NULL)
		printf("opendir(/usr/glenda): done\n");
	else
		printf("opendir(/usr/glenda): failed\n");

	while (dirp) {
		errno = 0;
		if ((dp = readdir(dirp)) != NULL) {
			if (strcmp(dp->d_name, "readme.acme") == 0) {
				if(dp->d_type == DT_REG){
					++i;
					printf("FOUND! ");
				}
			}
			if (strcmp(dp->d_name, "tmp") == 0) {
				if(dp->d_type == DT_DIR){
					++i;
					printf("FOUND! ");
				}
			}
			printf("%s: %s\n", dp->d_name, dp->d_type == DT_DIR ? "directory" : "regular file");
			if(i == 2){
				closedir(dirp);
				return 0;
			}
		} else {
			printf("readdir returned NULL before finding both readme.acme and tmp/\n");
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
