#include <sys/dnv.h>
#include <sys/nv.h>
#include <sys/ioctl.h>

#include <libxo/xo.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucl.h>
#include <unistd.h>

typedef struct nvecho {
	void *buf;
	size_t len;
} nvecho_t;

#define ECHO_IOCTL _IOWR('H', 1, nvecho_t)

static enum {IOCTL_GET, IOCTL_SET, SYSCTL} action = IOCTL_GET;

char *program;

void
usage() {
	printf("Usage: %s [-ghs] [-i config file]\n", program);
}

int
main(int argc, char **argv) {
	size_t size;
	int ch, r = 0, fd, rc;
	nvlist_t *nvl;
	nvecho_t data;
	const char *param, *config;
	struct ucl_parser *parser = NULL;
	ucl_object_t *obj = NULL;
	const ucl_object_t *o = NULL;
	ucl_object_iter_t it = NULL;

	program = argv[0];
	while ((ch = getopt(argc, argv, "ghi:s")) != -1) {
		switch (ch) {
			case 'g':
				action = IOCTL_GET;
				break;
			case 'h':
				usage();
				break;
			case 'i':
				action = IOCTL_SET;
				config = optarg;
				break;
			case 's':
				action = SYSCTL;
				break;
			case '?':
			default:
				usage();
				exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (action == IOCTL_SET) {
		parser = ucl_parser_new(0);
		nvl = nvlist_create(0);
		if (!ucl_parser_add_file(parser, config)) {
			err(1, "Parsing %s", config);
		}
		if (ucl_parser_get_error(parser)) {
			err(1, "UCL parser");
		}
		if ((obj = ucl_parser_get_object(parser)) == NULL) {
			err(1, "UCL get object");
		}

		o = ucl_iterate_object(obj, &it, true);
		nvlist_add_string(nvl, ucl_object_key(o), ucl_object_tostring_forced(o));
		data.buf = nvlist_pack(nvl, &data.len);

		nvlist_destroy(nvl);
		ucl_object_unref(obj);
		ucl_parser_free(parser);

		fd = open("/dev/echo", O_RDWR);
		if (fd < 0) {
			err(1, "open(/dev/echo)");
		}
		printf("data size: %zu\n", data.len);
		rc = ioctl(fd, ECHO_IOCTL, &data);
		if (rc < 0) {
			err(1, "ioctl(/dev/echo)");
		}
		close (fd);
	} else if (action == IOCTL_GET) {
		data.buf = 0;
		data.len = 0;
		fd = open("/dev/echo", O_RDWR);
		if (fd < 0) {
			err(1, "open(/dev/echo)");
		}
		rc = ioctl(fd, ECHO_IOCTL, &data);
		if (rc < 0) {
			err(1, "ioctl(/dev/echo)");
		}
		data.buf = malloc(data.len);
		data.len = 0;
		rc = ioctl(fd, ECHO_IOCTL, &data);
		if (rc < 0) {
			err(1, "ioctl(/dev/echo)");
		}
		nvl = nvlist_unpack(data.buf, data.len, 0);
		param = dnvlist_get_string(nvl, "param", NULL);
		if (param) {
			printf("param = %s\n", param);
		} else {
			err(1, "nvlist error");
		}
		nvlist_destroy(nvl);
		close (fd);
	}
	return 0;
}
