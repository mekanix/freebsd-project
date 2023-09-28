#include <sys/queue.h>
#include <sys/tree.h>

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATTR_RO 0x001
#define ATTR_NODELETE 0x002
#define ATTR_JID_MASK (ATTR_RO | ATTR_NODELETE)

#define ATTR_BOOL 0x010
#define ATTR_NUMBER 0x020
#define ATTR_STRING 0x040
#define ATTR_NULL 0x080
#define ATTR_ARRAY 0x100
#define ATTR_NESTED 0x200
#define ATTR_SIMPLE_MASK (ATTR_BOOL | ATTR_NUMBER | ATTR_STRING | ATTR_NULL)

struct array_t;
struct params_t;

typedef struct attr_t {
  TAILQ_ENTRY(attr_t) next;
  RB_ENTRY(attr_t) entry;
  char *name;
  size_t type;
  union {
    bool b;
    uint64_t num;
    void *ptr;
    char *string;
    struct params_t *params;
    struct array_t *array;
  } value;
} attr_t;

TAILQ_HEAD(array_t, attr_t);
typedef struct array_t array_t;

RB_HEAD(params_t, attr_t);
typedef struct params_t params_t;

static int attr_name_compare(const attr_t *a1, const attr_t *a2) {
  return strcmp(a1->name, a2->name);
}

RB_GENERATE(params_t, attr_t, entry, attr_name_compare)

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
  node->value.ptr = NULL;
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

int main() {
  attr_t *node = NULL;
  attr_t *tmpnode = NULL;
  attr_t *arr = NULL;
  params_t *params = NULL;

  params = malloc(sizeof(params_t));
  RB_INIT(params);

  arr = new_array("array");

  node = new_number(NULL, 4);
  TAILQ_INSERT_TAIL(arr->value.array, node, next);

  if ((tmpnode = RB_INSERT(params_t, params, arr)) != NULL) {
    free(node);
    err(1, "node with name '%s' already exists\n", arr->name);
  }

  return 0;
}
