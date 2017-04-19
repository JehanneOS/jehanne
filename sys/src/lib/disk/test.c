#include <u.h>
#include <lib9.h>
#include <disk.h>

char *src[] = { "part", "disk", "guess" };
void
main(int argc, char **argv)
{
	Disk *disk;

	assert(argc == 2);
	disk = opendisk(argv[1], 0, 0);
	print("%d %d %d from %s\n", disk->c, disk->h, disk->s, src[disk->chssrc]);
}
