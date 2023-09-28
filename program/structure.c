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

attr_t *new_number(char *name) {
  attr_t *node = new_param(name);
  node->type = ATTR_NUMBER;
  return node;
}

attr_t *new_bool(char *name) {
  attr_t *node = new_param(name);
  node->type = ATTR_BOOL;
  return node;
}

attr_t *new_string(char *name) {
  attr_t *node = new_param(name);
  node->type = ATTR_STRING;
  return node;
}

attr_t *new_null(char *name) {
  attr_t *node = new_param(name);
  node->value.ptr = NULL;
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

int main() {
  attr_t *node = NULL;
  attr_t *tmpnode = NULL;
  params_t *params = NULL;

  params = malloc(sizeof(params_t));
  RB_INIT(params);

  for (uint64_t n = 0; n < 6; ++n) {
    node = new_number("cvrc");
    node->value.num = n;
    // TAILQ_INSERT_TAIL(&array, node, next);
    tmpnode = RB_INSERT(params_t, params, node);
    if (tmpnode) {
      printf("Insert Node %lu\n", tmpnode->value.num);
      free(node);
    } else {
      printf("Insert Node %lu\n", node->value.num);
    }
  }

  // TAILQ_FOREACH(node, &array, next) { printf("Dump Node %d\n", node->value);
  // }

  // while ((node = TAILQ_FIRST(&array)) != NULL) {
  //   printf("Delete Node %d\n", node->value);
  //   TAILQ_REMOVE(&array, node, next);
  //   free(node);
  // }
  return 0;
}
