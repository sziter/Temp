#include <stdio.h>
#include <linux/ioctl.h>

#include <fcntl.h>

#define SCULL_IOC_MAGIC '%'
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)
#define SCULL_IOCTEST _IO(SCULL_IOC_MAGIC, 1)

int main(void)
{
	char filename[] = "/dev/mycdev";
	long fd = open(filename, O_RDONLY);
	printf("fd = %ld", fd);

	ioctl(fd, SCULL_IOCTEST, &fd);

	return 1;
}
