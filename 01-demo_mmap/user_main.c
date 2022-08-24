#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "common.h"

int main(int argc, char *argv[]) {
	char *buf = NULL;
	int fd;
	struct stat sb;
	off_t offset = 0, pa_offset = 0;
	size_t length = 20;
	ssize_t size;
	int i, err = 0;

	fd = open("/dev/" DEVICE_NAME, O_RDONLY);
	if(fd < 0) {
		err_info("Open /dev/%s error, err: %d\n", DEVICE_NAME, -errno);
		err = -errno;
		return err;
	}

	buf = mmap(NULL, length + offset - pa_offset, PROT_READ,
			MAP_PRIVATE, fd, pa_offset);
	if(buf == MAP_FAILED) {
		err_info("Map failed, err: %d\n", -errno);
		err = -errno;
		goto err_mmap;
	}

	for(i = 0; i < 20; i++)
		printf("%s%d", (i)? ", ": "", buf[i]);
	printf("\n");

err_mmap:
	munmap(buf, length + offset - pa_offset);
	close(fd);
	return err;
}
