#ifndef _REGEX_INCLUDE_CHECK
#define _REGEX_INCLUDE_CHECK

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define eleft e.child.left
#define eright e.child.right
#define ech e.ch

#define nd_sib e.sibling
#define tr_ch e.transition.ch
#define tr_sib e.transition.sibling
#define tr_next e.transition.next

enum op_t {
  e_union,
  e_concat,
  e_closure,
  e_pclosure,
  e_empty,
  e_char
};

struct Leaf {
  enum op_t op;
  /*
   * if op == "char", then the param of this union is character
   * else , then the param of this union is the struct that represents
   * a left tree and a right tree.
   */
  union LeafEntity {
    char ch;
	struct {
	  struct Leaf *left;
	  struct Leaf *right;
	} child;
  } e;
};

struct Ntrans {
  char ch;
  int to;
  int from;
};

struct Nnode {
  char ch;
  int to;
  struct Nnode* next;
};

enum dfaOp_t {
  e_node,
  e_transition,
  e_final
};

struct Dtrans {
  int nnode_no;
  struct Dtrans* next;
};

struct Dnode {
  enum dfaOp_t op; // op_t represent the two statement as node or transition.
  union DnodeEntity {
	struct Dnode* sibling; // when op is "node"
	struct { // when op is "transition"
	  char ch;
	  struct Dnode* sibling; // If other charcter exist in this node, it is linked at sibling.
	  struct Dnode* next; // 
	} transition;
  } e;
};

int regex(char* regexStr, char* check);

#endif
