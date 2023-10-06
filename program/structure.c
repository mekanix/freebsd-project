#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/nv.h>

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ATTR_RO		0x001
#define ATTR_NODELETE	0x002

#define ATTR_BOOL	0x010
#define ATTR_NUMBER	0x020
#define ATTR_STRING	0x040
#define ATTR_NULL	0x080
#define ATTR_ARRAY	0x100
#define ATTR_NESTED	0x200
#define ATTR_SIMPLE	(ATTR_BOOL | ATTR_NUMBER | ATTR_STRING | ATTR_NULL)

#define	NVLIST_HEADER_MAGIC	0x6c
#define	NVLIST_HEADER_VERSION	0x00

struct array_t;
struct params_t;

typedef union {
	bool b;
	uint64_t num;
	char *string;
	struct params_t *params;
	struct array_t *array;
} value_t;

typedef struct attr_t {
	TAILQ_ENTRY(attr_t) next;
	RB_ENTRY(attr_t) entry;
	char *name;
	size_t type;
	value_t value;
} attr_t;

typedef TAILQ_HEAD(array_t, attr_t) array_t;
typedef RB_HEAD(params_t, attr_t) params_t;

struct nvpair_header {
	uint8_t		nvph_type;
	uint16_t	nvph_namesize;
	uint64_t	nvph_datasize;
	uint64_t	nvph_nitems;
} __packed;

struct nvlist_header {
	uint8_t		nvlh_magic;
	uint8_t		nvlh_version;
	uint8_t		nvlh_flags;
	uint64_t	nvlh_descriptors;
	uint64_t	nvlh_size;
} __packed;

static int attr_name_compare(const attr_t *a1, const attr_t *a2) {
	if (a1 == NULL) {
		if (a2 == NULL) {
			return 0;
		}
		return -1;
	} else if (a2 == NULL) {
		return 1;
	}
	return strcmp(a1->name, a2->name);
}

RB_GENERATE(params_t, attr_t, entry, attr_name_compare)

params_t * params_init() {
	params_t *params = NULL;
	params = malloc(sizeof(params_t));
	memset(params, 0, sizeof(params_t));
	RB_INIT(params);
	return params;
}

attr_t *new_param(char *name) {
	attr_t *node = malloc(sizeof(attr_t));
	memset(node, 0, sizeof(attr_t));
	node->name = name;
	return node;
}

attr_t *new_number(char *name, uint64_t value) {
	attr_t *node = new_param(name);
	node->type = ATTR_NUMBER;
	node->value.num = value;
	return node;
}

attr_t *new_bool(char *name, bool value) {
	attr_t *node = new_param(name);
	node->type = ATTR_BOOL;
	node->value.b = value;
	return node;
}

attr_t *new_string(char *name, char *value) {
	attr_t *node = new_param(name);
	node->type = ATTR_STRING;
	node->value.string = value;
	return node;
}

attr_t *new_null(char *name) {
	attr_t *node = new_param(name);
	node->type = ATTR_NULL;
	return node;
}

attr_t *new_params(char *name) {
	attr_t *node = new_param(name);
	node->type = ATTR_NESTED;
	node->value.params = malloc(sizeof(params_t));
	RB_INIT(node->value.params);
	return node;
}

attr_t *new_array(char *name) {
	attr_t *node = new_param(name);
	node->type = ATTR_ARRAY;
	node->value.array = malloc(sizeof(array_t));
	TAILQ_INIT(node->value.array);
	return node;
}

attr_t *new_nested(char *name) {
	attr_t *node = new_param(name);
	node->type = ATTR_NESTED;
	node->value.params = params_init();
	return node;
}

size_t params_size(params_t *p) {
	attr_t *attr = NULL;
	attr_t *node = NULL;
	uint64_t nitems = 0;
	size_t size = sizeof(struct nvlist_header);

	RB_FOREACH(attr, params_t, p) {
		size += sizeof(struct nvpair_header);
		if (attr->name != NULL) {
			size += strlen(attr->name) + 1;
		}
		if (attr->type & ATTR_ARRAY) {
			switch(attr->type & ~ATTR_ARRAY) {
				case ATTR_BOOL: {
					TAILQ_FOREACH(node, attr->value.array, next) {
						++nitems;
					}
					size += nitems * sizeof(bool);
					break;
				}
				case ATTR_NUMBER: {
					TAILQ_FOREACH(node, attr->value.array, next) {
						++nitems;
					}
					size += nitems * sizeof(uint64_t);
					break;
				}
				case ATTR_STRING: {
					TAILQ_FOREACH(node, attr->value.array, next) {
						size += strlen(node->value.string) + 1;
					}
					break;
				}
			}
		} else {
			switch(attr->type) {
				case ATTR_BOOL: {
					size += sizeof(bool);
					break;
				}
				case ATTR_NUMBER: {
					size += sizeof(uint64_t);
					break;
				}
				case ATTR_STRING: {
					size += strlen(attr->value.string) + 1;
					break;
				}
				case ATTR_NESTED: {
					size += params_size(attr->value.params);
					break;
				}
			}
		}
	}
	return size;
}

void * params_pack(params_t *p, uint8_t *buf, size_t *sz) {
	size_t size = 0;
	uint8_t *ptr = NULL;
	attr_t *attr = NULL;
	struct nvlist_header nvl = {0};
	struct nvpair_header nvp = {0};

	if (p == NULL) {
		return NULL;
	}

	size = params_size(p);
	if (buf == NULL) {
		buf = malloc(size);
	}
	*sz = size;

	nvl.nvlh_magic = NVLIST_HEADER_MAGIC;
	nvl.nvlh_version = NVLIST_HEADER_VERSION;
	nvl.nvlh_flags = 0;
	nvl.nvlh_descriptors = 0;
	nvl.nvlh_size = size - sizeof(nvl);
	memcpy(buf, &nvl, sizeof(nvl));
	ptr = buf + sizeof(nvl);
	RB_FOREACH(attr, params_t, p) {
		nvp.nvph_namesize = strlen(attr->name) + 1;
		nvp.nvph_nitems = 0;
		if (attr->type & ATTR_ARRAY) {
			attr_t *node = NULL;
			nvp.nvph_datasize = 0;

			switch(attr->type & ~ATTR_ARRAY) {
				case ATTR_BOOL: {
					TAILQ_FOREACH(node, attr->value.array, next) {
						++nvp.nvph_nitems;
					}
					nvp.nvph_datasize = nvp.nvph_nitems * sizeof(bool);
					nvp.nvph_type = NV_TYPE_BOOL_ARRAY;
					memcpy(ptr, &nvp, sizeof(nvp));
					ptr += sizeof(nvp);
					memcpy(ptr, attr->name, nvp.nvph_namesize);
					ptr += nvp.nvph_namesize;
					node = NULL;
					TAILQ_FOREACH(node, attr->value.array, next) {
						memcpy(ptr, &(node->value.b), nvp.nvph_datasize);
						ptr += nvp.nvph_datasize;
					}
					break;
				}
				case ATTR_NUMBER: {
					TAILQ_FOREACH(node, attr->value.array, next) {
						++nvp.nvph_nitems;
					}
					nvp.nvph_datasize = nvp.nvph_nitems * sizeof(uint64_t);
					nvp.nvph_type = NV_TYPE_NUMBER_ARRAY;
					memcpy(ptr, &nvp, sizeof(nvp));
					ptr += sizeof(nvp);
					memcpy(ptr, attr->name, nvp.nvph_namesize);
					ptr += nvp.nvph_namesize;
					node = NULL;
					TAILQ_FOREACH(node, attr->value.array, next) {
						memcpy(ptr, &(node->value.num), nvp.nvph_datasize);
						ptr += nvp.nvph_datasize;
					}
					break;
				}
				case ATTR_STRING: {
					TAILQ_FOREACH(node, attr->value.array, next) {
						++nvp.nvph_nitems;
						nvp.nvph_datasize += strlen(node->value.string) + 1;
					}
					nvp.nvph_type = NV_TYPE_STRING_ARRAY;
					memcpy(ptr, &nvp, sizeof(nvp));
					ptr += sizeof(nvp);
					memcpy(ptr, attr->name, nvp.nvph_namesize);
					ptr += nvp.nvph_namesize;
					node = NULL;
					TAILQ_FOREACH(node, attr->value.array, next) {
						size = strlen(node->value.string) + 1;
						memcpy(ptr, node->value.string, size);
						ptr += size;
					}
					break;
				}
			}
		} else if (attr->type & ATTR_NESTED) {
			size_t size;
			uint8_t *bytes = ptr;

			nvp.nvph_type = NV_TYPE_NVLIST;
			ptr += sizeof(nvp);
			memcpy(ptr, attr->name, nvp.nvph_namesize);
			ptr += nvp.nvph_namesize;
			params_pack(attr->value.params, ptr, &size);
			nvp.nvph_datasize = size;
			memcpy(bytes, &nvp, sizeof(nvp));
			ptr += nvp.nvph_datasize;
		} else {
			switch(attr->type) {
				case ATTR_NULL: {
					nvp.nvph_type = NV_TYPE_NULL;
					nvp.nvph_datasize = 0;
					memcpy(ptr, &nvp, sizeof(nvp));
					ptr += sizeof(nvp);
					memcpy(ptr, attr->name, nvp.nvph_namesize);
					ptr += nvp.nvph_namesize;
					break;
				}
				case ATTR_BOOL: {
					nvp.nvph_type = NV_TYPE_BOOL;
					nvp.nvph_datasize = sizeof(bool);
					memcpy(ptr, &nvp, sizeof(nvp));
					ptr += sizeof(nvp);
					memcpy(ptr, attr->name, nvp.nvph_namesize);
					ptr += nvp.nvph_namesize;
					memcpy(ptr, &attr->value.b, nvp.nvph_datasize);
					ptr += nvp.nvph_datasize;
					break;
				}
				case ATTR_NUMBER: {
					nvp.nvph_type = NV_TYPE_NUMBER;
					nvp.nvph_datasize = sizeof(uint64_t);
					memcpy(ptr, &nvp, sizeof(nvp));
					ptr += sizeof(nvp);
					memcpy(ptr, attr->name, nvp.nvph_namesize);
					ptr += nvp.nvph_namesize;
					memcpy(ptr, &(attr->value.num), nvp.nvph_datasize);
					ptr += nvp.nvph_datasize;
					break;
				}
				case ATTR_STRING: {
					nvp.nvph_type = NV_TYPE_STRING;
					nvp.nvph_datasize = strlen(attr->value.string) + 1;
					memcpy(ptr, &nvp, sizeof(nvp));
					ptr += sizeof(nvp);
					memcpy(ptr, attr->name, nvp.nvph_namesize);
					ptr += nvp.nvph_namesize;
					memcpy(ptr, attr->value.string, nvp.nvph_datasize);
					ptr += nvp.nvph_datasize;
					break;
				}
			}
		}
	}
	buf[size] = 0;
	return buf;
}

int main() {
	size_t size = 0;
	void *buf = NULL;
	attr_t *node = NULL;
	attr_t *tmpnode = NULL;
	params_t *params = NULL;
	nvlist_t *nvl = NULL;

	params = params_init();
	node = new_number("a", 4);
	if (RB_INSERT(params_t, params, node) != NULL) {
		free(node);
		err(1, "node with name '%s' already exists\n", node->name);
	}
	node = new_bool("b", true);
	if (RB_INSERT(params_t, params, node) != NULL) {
		free(node);
		err(1, "node with name '%s' already exists\n", node->name);
	}
	node = new_string("c", "c");
	if (RB_INSERT(params_t, params, node) != NULL) {
		free(node);
		err(1, "node with name '%s' already exists\n", node->name);
	}
	node = new_null("x");
	if (RB_INSERT(params_t, params, node) != NULL) {
		free(node);
		err(1, "node with name '%s' already exists\n", node->name);
	}
	node = new_nested("z");
	if (RB_INSERT(params_t, params, node) != NULL) {
		free(node);
		err(1, "node with name '%s' already exists\n", node->name);
	}
	node = new_array("na");
	node->type |= ATTR_STRING;
	tmpnode = new_string(NULL, "one");
	TAILQ_INSERT_TAIL(node->value.array, tmpnode, next);
	tmpnode = new_string(NULL, "two");
	TAILQ_INSERT_TAIL(node->value.array, tmpnode, next);
	if (RB_INSERT(params_t, params, node) != NULL) {
		free(node);
		err(1, "node with name '%s' already exists\n", node->name);
	}

	buf = params_pack(params, NULL, &size);
	nvl = nvlist_unpack(buf, size, 0);
	nvlist_dump(nvl, STDOUT_FILENO);
	return 0;
}
