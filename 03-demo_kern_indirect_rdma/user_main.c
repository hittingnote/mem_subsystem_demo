#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "common.h"

#define MAX(a, b)		((a)>(b)? (a): (b))
#define MIN(a, b)		((a)<(b)? (a): (b))

#define MAXSIZE						4096

static void usage(const char *argv0) {
	fprintf(stderr, "Usage:\n"
		"%s -d [dev_name] -p [tcp_port] -i [ibv_port] -x [sgid_index] [servername]\n"
		"\n"
		"If a servername is specified at last, "
		"this program will run as a client connecting to the specified server. "
		"Otherwise, it will start as a server\n\n", argv0);
}

static int parse_param(int argc, char *argv[],
							struct write_param *param) {
	int err = 0;
	int cur_opt;
	unsigned short tcp_port;

	if(!param || argc < 2) {
		err = -EINVAL;
		err_info(err, "param nil\n");
		return err;
	}

	memset(param, 0, sizeof(*param));
	param->s_addr.sin_family = AF_INET;
	param->s_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	while((cur_opt = getopt(argc, argv, "d:p:i:x:h")) != -1) {
		switch(cur_opt) {
		case 'd':
			strncpy(param->dev_name, optarg,
					MIN(sizeof(param->dev_name), strlen(optarg)));
			break;
		case 'p':
			tcp_port = atoi(optarg);
			param->s_addr.sin_port = htons(tcp_port);
			break;
		case 'i':
			param->rdma_port = atoi(optarg);
			break;
		case 'x':
			param->sgid_index = atoi(optarg);
			break;
		case 'h':
			usage(argv[0]);
			err = EINVAL;
			return err;
		default:
			err = -EINVAL;
			err_info(err, "Invalid options\n");
			usage(argv[0]);
			return err;
		}
	}

	if(optind == argc - 1) {
		err = inet_pton(AF_INET, argv[argc-1],
							&param->s_addr.sin_addr);
		if(err <= 0) {
			err = -EFAULT;
			err_info(err, "inet_pton error\n");
			return err;
		}
		else
			err = 0;
	}
	else if(optind < argc) {
		err = -EINVAL;
		err_info(err, "Invalid options\n");
		usage(argv[0]);
		return err;
	}

	return err;
}

static inline int is_server(const struct write_param *param) {
	return (param->s_addr.sin_addr.s_addr == htonl(INADDR_ANY));
}

int main(int argc, char *argv[]) {
	int err = 0;
	char buf[MAXSIZE];
	int fd;
	struct write_param param;

	err = parse_param(argc, argv, &param);
	if(err > 0) {
		return 0;
	}
	else if(err < 0) {
		return err;
	}

	param.virtaddr = (unsigned long)buf;
	param.length = sizeof(buf);

	if(is_server(&param)) {
		strcpy(buf, "I'm server!");
	}
	else {
		strcpy(buf, "Hello, I'm client!");
	}

	fd = open("/dev/" DEV_NAME, O_WRONLY);
	if(fd < 0) {
		err = -errno;
		err_info(err, "Failed to open /dev/" DEV_NAME "\n");
		return err;
	}

	err = write(fd, &param, sizeof(struct write_param));
	if(err < 0) {
		err = -errno;
		err_info(err, "Failed to write param\n");
		close(fd);
		return err;
	}

	PRINT("%s", (char*)buf);
	close(fd);

	return err;
}
