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
	const char *key = NULL, *value = NULL;
	const ucl_object_t *obj = NULL, *cur = NULL;
	ucl_object_iter_t it = NULL, itobj = NULL;

	if (nvl == NULL || top == NULL) {
		err(1, "NVList or UCL object is NULL in uclobj2nv");
	}

	while ((obj = ucl_iterate_object(top, &it, false))) {
		key = ucl_object_key(obj);
		switch(obj->type) {
			case UCL_OBJECT:
				nested = nvlist_create(0);
				while ((cur = ucl_iterate_object(obj, &itobj, true))) {
					uclobj2nv(nested, cur);
				}
				if (nvlist_exists_nvlist_array(nvl, key)) {
					nvlist_append_nvlist_array(nvl, key, nested);
				} else if (obj->next != NULL) {
					nvlist_add_nvlist_array(nvl, key, (const nvlist_t * const *)&nested, 1);
				} else {
					nvlist_add_nvlist(nvl, key, nested);
				}
				break;
			case UCL_ARRAY:
				break;
			default:
				value = ucl_object_tostring_forced(obj);
				if (nvlist_exists_string_array(nvl, key)) {
					nvlist_append_string_array(nvl, key, value);
				} else if (obj->next != NULL) {
					nvlist_add_string_array(nvl, key, &value, 1);
				} else {
					nvlist_add_string(nvl, key, value);
				}
				break;
		}
	}
}

static void
print_nvlist(const nvlist_t *nvl, size_t ident) {
	const char *name = NULL;
	const char *value = NULL;
	void *cookie = NULL;
	int type = 0;
	char closing = 0;
	const char * const *array = NULL;
	size_t nitems = 0;
  const nvlist_t * const *arr = NULL;

	if (nvl == NULL) {
		return;
	}

  while ((name = nvlist_next(nvl, &type, &cookie)) != NULL) {
    for (size_t i = 0; i < ident; ++i) {
      printf("  ");
    }
    printf("%s ", name);
    switch (type) {
      case NV_TYPE_NVLIST:
        printf("= {\n");
        print_nvlist(nvlist_get_nvlist(nvl, name), ident + 1);
        for (size_t i = 0; i < ident; ++i) {
          printf("  ");
        }
        printf("}");
        break;
      case NV_TYPE_NVLIST_ARRAY:
        printf("= [\n");
        arr = nvlist_get_nvlist_array(nvl, name, &nitems);
        for (size_t i = 0; i < nitems; ++i) {
          for (size_t i = 0; i < ident + 1; ++i) {
            printf("  ");
          }
          printf("{\n");
          print_nvlist(arr[i], ident + 2);
          for (size_t i = 0; i < ident + 1; ++i) {
            printf("  ");
          }
          printf("}\n");
        }
        for (size_t i = 0; i < ident; ++i) {
          printf("  ");
        }
        printf("]");
        break;
      case NV_TYPE_STRING_ARRAY:
        printf("= [");
        array = nvlist_get_string_array(nvl, name, &nitems);
        for (size_t i = 0; i < nitems; ++i) {
          printf(" %s,", array[i]);
        }
        printf(" ]");
        break;
      case NV_TYPE_STRING:
        value = nvlist_get_string(nvl, name);
        printf("= %s", value);
        break;
    }
    printf("\n");
  }
	// do {
	// 	while ((name = nvlist_next(nvl, &type, &cookie)) != NULL) {
	// 		for (size_t i = 0; i < ident; ++i) {
	// 			printf("  ");
	// 		}
	// 		printf("%s ", name);
	// 		switch (type) {
	// 			case NV_TYPE_NVLIST:
	// 				++ident;
	// 				printf("{");
	// 				parent = nvl;
	// 				closing = '}';
	// 				nvl = nvlist_get_nvlist(nvl, name);
	// 				cookie = NULL;
	// 				break;
	// 			case NV_TYPE_NVLIST_ARRAY:
	// 				++ident;
	// 				printf("[");
	// 				closing = ']';
	// 				nvl = nvlist_get_nvlist_array(nvl, name, &nitems)[0];
	// 				cookie = NULL;
	// 				break;
	// 			case NV_TYPE_STRING_ARRAY:
	// 				printf("[");
	// 				array = nvlist_get_string_array(nvl, name, &nitems);
	// 				for (size_t i = 0; i < nitems; ++i) {
	// 					printf(" %s,", array[i]);
	// 				}
	// 				printf(" ]");
	// 				break;
	// 			case NV_TYPE_STRING:
	// 				value = nvlist_get_string(nvl, name);
	// 				printf("= %s", value);
	// 				break;
	// 			default:
	// 				printf("type %d", type);
	// 				break;
	// 		}
	// 		printf("\n");
	// 	}
	// 	if (ident > 0) {
	// 		--ident;
	// 	}
	// 	for (size_t i = 0; i < ident; ++i) {
	// 		printf("  ");
	// 	}
	// 	if (nvl == parent) {
	// 		printf("%c\n", closing);
	// 	}
	// } while ((nvl = nvlist_get_pararr(nvl, &cookie)) != NULL);
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
		print_nvlist(nvl, 0);
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
		print_nvlist(nvl, 0);
		nvlist_destroy(nvl);
		close (fd);
	} else if (action == SYSCTL_GET) {
		nvlist_t *nvl = NULL;

		rc = sysctlbyname("kern.echo.config", NULL, &data.len, NULL, 0);
		if (rc != 0) {
			err(1, "Get sysctl size");
		}
		if (data.len == 0) {
			err(1, "no config available");
		}
		data.buf = malloc(data.len);
		rc = sysctlbyname("kern.echo.config", data.buf, &data.len, NULL, 0);
		if (rc != 0) {
			err(1, "Get sysctl value");
		}
		nvl = nvlist_unpack(data.buf, data.len, 0);
		if (nvl == NULL) {
			err(1, "unpacking nvlist data");
		}
		print_nvlist(nvl, 0);
		nvlist_destroy(nvl);
	}
	if (data.buf != NULL) {
		free(data.buf);
	}
	return 0;
}
