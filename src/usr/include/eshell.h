#ifndef _ESHELL_H
#define _ESHELL_H

#include <sys/types.h>

#define BUF_SIZE 64
#define PROMPT_BUF 64
#define NAME_MAX 16
#define PATH_MAX 128

struct list {
  struct list* next;
  struct list* prev;
  char name[NAME_MAX];
};

#define LIST_INSERT_BEFORE(new, list)    \
  if (list->next == list) {              \
    list->prev = new;                    \
    list->next = new;                    \
    new->next = list;                    \
    new->prev = list;                    \
  } else {                               \
    list->prev->next = new;              \
    new->prev = list->prev;              \
    list->prev = new;                    \
    new->next = list;                    \
  }

int redirect_check(const char** arg_buf);
int pipe_check(const char** arg_buf);

#endif
