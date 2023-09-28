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

// typedef union {
//   RB_HEAD(parameters_t, attribute_t) parameters;
//   TAILQ_HEAD(array_t, attribute_t) array;
//   void *pointer;
//   bool boolean;
//   uint64_t number;
//   char *string;
// } value_t;

// typedef struct attribute_t {
//   RB_ENTRY(attribute_t) entry;
//   TAILQ_ENTRY(attribute_t) next;

//   size_t type;
//   char *name;
//   value_t value;
// } attribute_t;

// static int attr_name_compare(const attribute_t *a1, const attribute_t *a2) {
//   return strcmp(a1->name, a2->name);
// }

// RB_GENERATE(parameters_t, attribute_t, entry, attr_name_compare)
// typedef struct parameters_t parameters_t;
// typedef struct array_t array_t;

// int main() {
//   parameters_t *p = NULL;
//   attribute_t *a = NULL;

//   p = malloc(sizeof(parameters_t));
//   RB_INIT(p);
//   TAILQ_INIT(p);
//   a = malloc(sizeof(attribute_t));
//   a->name = "cvrc";
//   RB_INSERT(parameters_t, p, a);
//   return 0;
// }

struct array_t;
struct params_t;

typedef struct attr_t {
  TAILQ_ENTRY(attr_t) next;
  RB_ENTRY(attr_t) entry;
  char *name;
  size_t type;
  union {
    bool b;
    uint64_t n;
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

attr_t *new_number(char *name) {
  attr_t *node = malloc(sizeof(attr_t));
  node->name = name;
  node->type = ATTR_NUMBER;
  return node;
}

int main() {
  attr_t *node = NULL;
  params_t *params = NULL;

  params = malloc(sizeof(params_t));
  RB_INIT(params);

  for (uint64_t n = 0; n < 6; ++n) {
    node = new_number("cvrc");
    node->value.n = n;
    // TAILQ_INSERT_TAIL(&array, node, next);
    RB_INSERT(params_t, params, node);
    printf("Insert Node %lu\n", node->value.n);
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
