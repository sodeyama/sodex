#ifndef _LIST_H
#define _LIST_H

/*
 * double linked list
 */
struct dlist_set {
  struct dlist_set *prev, *next;
};

#define DLIST_SET_INIT(name) { &(name), &(name) }
#define DLIST_SET(name) \
  struct dlist_set name = DLIST_SET_INIT(name)

/**
 * dlist_entry - get the struct for this entry
 * @ptr:    the &struct list_head pointer.
 * @type:   the type of the struct this is embedded in.
 * @member: the name of the list_struct within the struct.
 */  
#define dlist_entry(ptr, type, member)  \
  (type*)((char*)ptr - dlist_offset(type, member))

#define dlist_offset(type, member)  \
  (u_int32_t)( (char*)(&(((type*)0)->member)) - ((char*)0) )

#define dlist_for_each(pos, dlist)  \
  for ( pos = (dlist)->next; pos != (dlist); pos = pos->next)

#define dlist_for_each_prev(pos, dlist)  \
  for ( pos = (dlist)->prev; pos != (dlist); pos = pos->prev)

static inline void init_dlist_set(struct dlist_set* list)
{
  list->prev = list;
  list->next = list;
}

static inline void dlist_remove(struct dlist_set* list)
{
  list->next->prev = list->prev;
  list->prev->next = list->next;
}

static inline void dlist_insert_before(struct dlist_set* list,
                                       struct dlist_set* elist)
{
  if (elist->next == elist) {
    elist->prev = list;
    elist->next = list;
    list->next = elist;
    list->prev = elist;
  } else {
    elist->prev->next = list;
    list->prev = elist->prev;
    elist->prev = list;
    list->next = elist;
  }
}

static inline void dlist_insert_after(struct dlist_set* list,
                                      struct dlist_set* elist)
{
  if (elist->next == elist) {
    elist->next = list;
    elist->prev = list;
    list->next = elist;
    list->prev = elist;
  } else {
    elist->next->prev = list;
    list->next = elist->next;
    elist->next = list;
    list->prev = elist;
  }
}

#endif
