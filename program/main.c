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

static void print_nv(const nvlist_t *nvl);
static void print_nvlist(const nvlist_t *nvl);
static void array_add(nvlist_t *nvl, const char *key, const ucl_object_t *obj);
static void uclobj2nv(nvlist_t *nvl, const ucl_object_t *top);
static nvlist_t * ucl2nv(struct ucl_parser *parser);

typedef struct nvecho {
	void *buf;
	size_t len;
} nvecho_t;
#define ECHO_IOCTL _IOWR('H', 1, nvecho_t)

static char *program;
static enum {IOCTL_GET, IOCTL_SET, SYSCTL_GET, SYSCTL_SET} action = IOCTL_GET;

static void
usage() {
	printf("Usage: %s [-ghs] [-i config file]\n", program);
}

static void
print_nv(const nvlist_t *nvl) {
	size_t size = 0;
	const char *name = NULL;
	void *cookie = NULL;
	int type = 0;
	char *fmt = NULL;

	if (nvl == NULL) {
		return;
	}

	while ((name = nvlist_next(nvl, &type, &cookie)) != NULL) {
		size = strlen(name) + 7;
		switch (type) {
			case NV_TYPE_NVLIST: {
				xo_open_container_d(name);
				print_nv(nvlist_get_nvlist(nvl, name));
				xo_close_container_d();
				break;
			}
			case NV_TYPE_NVLIST_ARRAY: {
				size_t items;
				const nvlist_t * const *arr = nvlist_get_nvlist_array(nvl, name, &items);

				xo_open_list_d(name);
				for (size_t i = 0; i < items; ++i) {
					xo_open_instance_d(name);
					print_nv(arr[i]);
					xo_close_instance_d();
				}
				xo_close_list_d();
				break;
			}
			case NV_TYPE_STRING_ARRAY: {
				size_t items;
				const char * const *array = nvlist_get_string_array(nvl, name, &items);

				xo_open_list_d(name);
				for (size_t i = 0; i < items; ++i) {
					xo_emit("{l:name/%s}", array[i]);
				}
				xo_close_list_d();
				break;
			}
			case NV_TYPE_STRING: {
				fmt = malloc(size + 1);
				snprintf(fmt, size, "{:%s/%%s}", name);
				fmt[size] = '\0';
				xo_emit(fmt, nvlist_get_string(nvl, name));
				break;
			}
			case NV_TYPE_BOOL_ARRAY: {
				size_t items;
				const bool *array = nvlist_get_bool_array(nvl, name, &items);

				xo_open_list_d(name);
				for (size_t i = 0; i < items; ++i) {
					xo_emit("{ln:name/%s}", array[i] ? "true" : "false");
				}
				xo_close_list_d();
				break;
			}
			case NV_TYPE_BOOL: {
				bool value = nvlist_get_bool(nvl, name);
				fmt = malloc(size + 2);
				snprintf(fmt, size + 1, "{n:%s/%%s}", name);
				fmt[size] = '\0';
				xo_emit(fmt, value ? "true" : "false");
				break;
			}
			case NV_TYPE_NUMBER_ARRAY: {
				size_t items;
				const uint64_t *array = nvlist_get_number_array(nvl, name, &items);

				xo_open_list_d(name);
				for (size_t i = 0; i < items; ++i) {
					xo_emit("{l:name/%lu}", array[i]);
				}
				xo_close_list_d();
				break;
			}
			case NV_TYPE_NUMBER: {
				uint64_t value = nvlist_get_number(nvl, name);

				fmt = malloc(size + 2);
				snprintf(fmt, size + 1, "{:%s/%%lu}", name);
				fmt[size] = '\0';
				xo_emit(fmt, value);
				break;
			}
		}
		if (fmt != NULL) {
			free(fmt);
			fmt = NULL;
		}
	}
}

static void
print_nvlist(const nvlist_t *nvl) {
	print_nv(nvl);
	xo_finish();
}

static void
array_add(nvlist_t *nvl, const char *key, const ucl_object_t *obj) {
	bool bvalue;
	uint64_t ivalue = 0;
	const char *svalue = NULL;
	nvlist_t *nested = NULL;
	const ucl_object_t *cur = NULL;
	ucl_object_iter_t it = NULL;

	switch(obj->type) {
		case UCL_OBJECT:
			nested = nvlist_create(0);
			while ((cur = ucl_iterate_object(obj, &it, true))) {
				uclobj2nv(nested, cur);
			}
			if (nvlist_exists_nvlist_array(nvl, key)) {
				nvlist_append_nvlist_array(nvl, key, nested);
			} else {
				nvlist_add_nvlist_array(nvl, key, (const nvlist_t * const *)&nested, 1);
			}
			break;
		case UCL_INT:
			ivalue = ucl_object_toint(obj);
			if (nvlist_exists_number_array(nvl, key)) {
				nvlist_append_number_array(nvl, key, ivalue);
			} else {
				nvlist_add_number_array(nvl, key, &ivalue, 1);
			}
			break;
		case UCL_FLOAT:
			break;
		case UCL_STRING:
			svalue = ucl_object_tostring(obj);
			if (nvlist_exists_string_array(nvl, key)) {
				nvlist_append_string_array(nvl, key, svalue);
			} else {
				nvlist_add_string_array(nvl, key, &svalue, 1);
			}
			break;
		case UCL_BOOLEAN:
			bvalue = ucl_object_toboolean(obj);
			if (nvlist_exists_bool_array(nvl, key)) {
				nvlist_append_bool_array(nvl, key, bvalue);
			} else {
				nvlist_add_bool_array(nvl, key, &bvalue, 1);
			}
			break;
		case UCL_TIME:
			break;
	}
}

static void
uclobj2nv(nvlist_t *nvl, const ucl_object_t *top) {
	nvlist_t *nested = NULL;
	const char *key = NULL, *svalue = NULL;
	const ucl_object_t *obj = NULL, *cur = NULL;
	ucl_object_iter_t it = NULL, itobj = NULL;
	bool bvalue;
	uint64_t ivalue = 0;

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
				while ((cur = ucl_iterate_object(obj, &itobj, true))) {
					array_add(nvl, key, cur);
				}
				break;
			case UCL_INT:
				ivalue = ucl_object_toint(obj);
				if (nvlist_exists_number_array(nvl, key)) {
					nvlist_append_number_array(nvl, key, ivalue);
				} else if (obj->next != NULL) {
					nvlist_add_number_array(nvl, key, &ivalue, 1);
				} else {
					nvlist_add_number(nvl, key, ivalue);
				}
				break;
			case UCL_FLOAT:
				break;
			case UCL_STRING:
				svalue = ucl_object_tostring_forced(obj);
				if (nvlist_exists_string_array(nvl, key)) {
					nvlist_append_string_array(nvl, key, svalue);
				} else if (obj->next != NULL) {
					nvlist_add_string_array(nvl, key, &svalue, 1);
				} else {
					nvlist_add_string(nvl, key, svalue);
				}
				break;
			case UCL_BOOLEAN:
				bvalue = ucl_object_toboolean(obj);
				if (nvlist_exists_bool_array(nvl, key)) {
					nvlist_append_bool_array(nvl, key, bvalue);
				} else if (obj->next != NULL) {
					nvlist_add_bool_array(nvl, key, &bvalue, 1);
				} else {
					nvlist_add_bool(nvl, key, bvalue);
				}
				break;
			case UCL_TIME:
				break;
			case UCL_USERDATA:
				nvlist_add_binary(nvl, key, obj->value.ud, obj->len);
				break;
			case UCL_NULL:
				nvlist_add_null(nvl, key);
				break;
			default:
				err(1, "unknown UCL type");
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

int
main(int argc, char **argv) {
	size_t size;
	int ch, r = 0, fd, rc;
	nvecho_t data = {0};
	const char *config;

	program = argv[0];
	argc = xo_parse_args(argc, argv);
	if (argc < 0) {
		exit(1);
	}
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
		print_nvlist(nvl);
		nvlist_destroy(nvl);
	}
	if (data.buf != NULL) {
		free(data.buf);
	}
	return 0;
}
