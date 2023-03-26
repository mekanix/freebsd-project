#include <sys/dnv.h>
#include <sys/nv.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>

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

static char *program;
static enum {IOCTL_GET, IOCTL_SET, SYSCTL_GET, SYSCTL_SET} action = IOCTL_GET;
#define ECHO_IOCTL _IOWR('H', 1, nvecho_t)

static void
usage() {
	printf("Usage: %s [-ghs] [-i config file]\n", program);
}

static void
uclobj2nv(nvlist_t *nvl, const ucl_object_t *top) {
	nvlist_t *nested = NULL;
	const char *key = NULL, *svalue = NULL, *curkey = NULL;
	const ucl_object_t *obj = NULL, *cur = NULL, *tmp = NULL;
	ucl_object_iter_t it = NULL, itobj = NULL;

	if (nvl == NULL || top == NULL) {
		err(1, "NVList or UCL object is NULL in uclobj2nv");
	}

	while ((obj = ucl_iterate_object(top, &it, false))) {
		key = ucl_object_key(obj);
		switch(obj->type) {
			case UCL_OBJECT:
				printf("%s {\n", key);
				while ((cur = ucl_iterate_object(obj, &itobj, true))) {
					uclobj2nv(nvl, cur);
				}
				printf("}\n");
				break;
			case UCL_ARRAY:
				printf("found array\n");
				break;
			default:
				printf("%s: %s\n", key, ucl_object_tostring_forced(obj));
				break;
		}
	}
}

static nvlist_t *
ucl2nv(struct ucl_parser *parser) {
	nvlist_t *nvl;
	ucl_object_t *top;
	const ucl_object_t *obj;
	ucl_object_iter_t it = NULL;

	top = ucl_parser_get_object(parser);
	if (top == NULL) {
		err(1, "UCL get object");
	}
	nvl = nvlist_create(0);
	if (nvl == NULL) {
		err(1, "nvlist_create");
	}
	while ((obj = ucl_iterate_object(top, &it, true))) {
		uclobj2nv(nvl, obj);
	}
	ucl_object_unref(top);
	
	return nvl;
}

static void
print_nvlist(const nvlist_t *nvl) {
	const char *name = NULL;
	void *cookie = NULL;
	int type;

	if (nvl == NULL) {
		return;
	}
	do {
		while ((name = nvlist_next(nvl, &type, &cookie)) != NULL) {
			if (type == NV_TYPE_NVLIST) {
				nvl = nvlist_get_nvlist(nvl, name);
				cookie = NULL;
			} else if (type == NV_TYPE_NVLIST_ARRAY) {
				nvl = nvlist_get_nvlist_array(nvl, name, NULL)[0];
				cookie = NULL;
			}
		}
	} while ((nvl = nvlist_get_pararr(nvl, &cookie)) != NULL);
}

int
main(int argc, char **argv) {
	size_t size;
	int ch, r = 0, fd, rc;
	nvecho_t data = {0};
	const char *config;

	program = argv[0];
	while ((ch = getopt(argc, argv, "ghi:s:q")) != -1) {
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
				action = SYSCTL_SET;
				config = optarg;
				break;
			case 'q':
				action = SYSCTL_GET;
				break;
			case '?':
			default:
				usage();
				exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (action == IOCTL_SET || action == SYSCTL_SET) {
		nvlist_t *nvl = NULL;
		struct ucl_parser *parser = ucl_parser_new(0);

		if (!ucl_parser_add_file(parser, config)) {
			err(1, "Parsing %s", config);
		}
		if (ucl_parser_get_error(parser)) {
			err(1, "UCL parser");
		}
		nvl = ucl2nv(parser);
		ucl_parser_free(parser);
		if (nvl == NULL) {
			err(1, "empty config nvlist");
		}
		print_nvlist(nvl);
		data.buf = nvlist_pack(nvl, &data.len);
		nvlist_destroy(nvl);

		if (action == IOCTL_SET) {
			fd = open("/dev/echo", O_RDWR);
			if (fd < 0) {
				err(1, "open(/dev/echo)");
			}
			rc = ioctl(fd, ECHO_IOCTL, &data);
			if (rc < 0) {
				err(1, "ioctl(/dev/echo)");
			}
			close (fd);
		} else {
			rc = sysctlbyname("kern.echo.config", NULL, NULL, data.buf, data.len);
			if (rc != 0) {
				err(1, "Set sysctl value");
			}
		}
	} else if (action == IOCTL_GET) {
		nvlist_t *nvl = NULL;

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
		if (nvl == NULL) {
			err(1, "unpacking nvlist data");
		}
		print_nvlist(nvl);
		nvlist_destroy(nvl);
		close (fd);
	} else if (action == SYSCTL_GET) {
		nvlist_t *nvl = NULL;

		rc = sysctlbyname("kern.echo.config", NULL, &data.len, NULL, 0);
		if (rc != 0) {
			err(1, "Get sysctl size");
		}
		rc = sysctlbyname("kern.echo.config", data.buf, &data.len, NULL, 0);
		if (rc != 0) {
			err(1, "Get sysctl value");
		}
		nvl = nvlist_unpack(data.buf, data.len, 0);
		if (nvl == NULL) {
			err(1, "unpacking nvlist data");
		}
		print_nvlist(nvl);
		nvlist_destroy(nvl);
	}
	if (data.buf != NULL) {
		free(data.buf);
	}
	return 0;
}
