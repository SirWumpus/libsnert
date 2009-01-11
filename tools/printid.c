#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

int
main(int argc, char **argv)
{
	struct stat sb;

	printf("     Real UID=%d\nEffective UID=%d\n", getuid(), geteuid());
	printf("     Real GID=%d\nEffective GID=%d\n", getgid(), getegid());

	if (stat(argv[0], &sb) == 0)
		printf("%s UID=%d\n%s GID=%d\n", argv[0], sb.st_uid, argv[0], sb.st_gid);

	printf("\nsetuid/setgid programs siliently fail if partition is mounted\n");
	printf("``nosuid'' (see /etc/fstab and mount -u -o suid partition)\n");

	return 0;
}
