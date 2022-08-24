#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "common.h"

#define ARR_SIZE(arr)			(sizeof(arr)/sizeof(*(arr)))
int arr[1000000];

int main(int argc, char *argv[]) {
	struct write_param addr_param;
	int err = 0;
	int fd;
	int i;

	for(i = 0; i < ARR_SIZE(arr); i++) {
		arr[i] = i;
	}

	fd = open("/dev/" DEV_NAME, O_WRONLY);
	if(fd < 0) {
		err_info(-errno, "Failed to open /dev/%s\n", DEV_NAME);
		err = -errno;
		return err;
	}

	addr_param.addr = (unsigned long)arr;
	addr_param.length = ARR_SIZE(arr)*sizeof(*arr);
	err = write(fd, &addr_param, sizeof(addr_param));
	if(err < 0) {
		err_info(-errno, "write failed\n");
		err = -errno;
		close(fd);
		return err;
	}

	close(fd);

	return 0;
}
