#include <regex.h>

#define NTRANS_MAX 128
#define NNODE_MAX 128
#define EMPTY -1
#define END -1
#define DFA_MAX 128

char current;
char *inspectedStr, *baseStr;
int nodeNum;
int ntransNum;
int nnodeMaxNum;
int numOfDnode;

struct Leaf *regexRoot;

struct Nnode** nnode;
struct Ntrans** ntrans;
struct Dtrans** dtrans;
struct Dnode** dnode;

static void perror(const char * ptr);
static char getNextCurrent();
static char getBackCurrent();
static void setNextCurrent();
static struct Leaf* growTree(struct Leaf* left, struct Leaf* right, enum op_t op);
static struct Leaf* regItr1();
static struct Leaf* regItr2();
static struct Leaf* regItr3();
static int isalpha(char c);
static void Stree(char *setch);
static struct Leaf* regexp();

static void NfaClass();
static void nfaInitial();
static void nfaIter(struct Leaf* leaf, int start, int last);
static void createTrans(int from, int to, char ch);
static void setTransToNode();
static int getNewNode();

static void dfaInitial();
static int dfaRegexp(char* search);
static void nfaToDfa();
static void setEmptyFromNnode(struct Nnode* node, struct Dtrans** dtrans);
static int compareDtrans(struct Dtrans* dtrans_tmp, int checkNum);
static int getLinkSize(struct Dtrans* dtrans);
static int isFinal(int dfastate);
static void DfaClass();

int regex(char* regexStr, char* check)
{
  nnode = (struct Nnode**)malloc(sizeof(struct Nnode*) * NNODE_MAX);
  ntrans = (struct Ntrans**)malloc(sizeof(struct Ntrans*) * NTRANS_MAX);
  dtrans = (struct Dtrans**)malloc(sizeof(struct Dtrnas*) * DFA_MAX);
  dnode = (struct Dnode**)malloc(sizeof(struct Dnode*) * DFA_MAX);
 
  Stree(regexStr);
  NfaClass();
  return dfaRegexp(check);
}

static void Stree(char *setch) {
  inspectedStr = setch;
  baseStr = setch;
  current = *(inspectedStr);
  regexRoot = regexp();
}

static char getNextCurrent()
{
  return *(inspectedStr+1);
}

static char getBackCurrent()
{
  if (baseStr < (inspectedStr-1))
    return '\0';
  return *(inspectedStr-1);
}

static void setNextCurrent()
{
  current = *(++inspectedStr);
}

/*
 * This function is mainly dealing with "union".
 */
static struct Leaf* regexp()
{
  struct Leaf *xLeaf, *yLeaf;

  xLeaf = regItr1();

  if (current == '|') {
    setNextCurrent();
    yLeaf = regexp();
    xLeaf = growTree(xLeaf, yLeaf, e_union);
  }

  return xLeaf;
}

/*
 * This function is mainly dealing with "concat".
 */
static struct Leaf* regItr1()
{
  struct Leaf *xLeaf, *yLeaf;

  xLeaf = regItr2();

  if (xLeaf == NULL)
    return NULL;

  if (current != '\0' && current != '|' && current != ')') {
    yLeaf = regItr1();
    xLeaf = growTree(xLeaf, yLeaf, e_concat);
  }

  return xLeaf;
}

static struct Leaf* regItr2()
{
  struct Leaf *xLeaf;

  xLeaf = regItr3();

  if (xLeaf == NULL)
    return NULL;

  if (current == '*') {
    xLeaf = growTree(xLeaf, NULL, e_closure);
    setNextCurrent();
  } else if (current == '+') {
    xLeaf = growTree(xLeaf, NULL, e_pclosure);
    setNextCurrent();
  }

  return xLeaf;
}

static struct Leaf* regItr3()
{
  struct Leaf *xLeaf = (struct Leaf*)malloc(sizeof(struct Leaf));
  xLeaf->eleft = NULL;
  xLeaf->eright = NULL;

  if (current == '(') {
    setNextCurrent();
    xLeaf = regexp();
    if (current != ')')
      perror("parse error");
    else
      setNextCurrent();
  } else if (isalpha(current)) {
    xLeaf->op = e_char;
    xLeaf->ech = current;
    setNextCurrent();
  } else {
    if (current == '|') {
      xLeaf->op = e_empty;
      setNextCurrent();
    } else if (getBackCurrent() == '|'){
      xLeaf->op = e_empty;
      setNextCurrent();
    } else if (current == '\0') {
      return NULL;
    } else {
      perror("sentence error");
    }
  }

  return xLeaf;
}

static int isalpha(char current)
{
  if ((current >= 'a' && current <= 'z') ||
      (current >= 'A' && current <= 'Z'))
    return TRUE;
  else
    return FALSE;
}

static struct Leaf* growTree(struct Leaf *left, struct Leaf *right, enum op_t op)
{
  struct Leaf *leaf = (struct Leaf*)malloc(sizeof(struct Leaf));
  leaf->op = op;
  leaf->eleft = left;
  leaf->eright = right;

  return leaf;
}

void perror(const char * ptr)
{
  //printf("%s\n", ptr);
  //exit(1);
}



void NfaClass()
{
  nfaInitial();
  struct Leaf* leaf = regexRoot;
  nfaIter(leaf, 0, 1);
  setTransToNode();
}

static int getNewNode()
{
  return ++nodeNum;
}

static void nfaIter(struct Leaf* leaf, int start, int last)
{
  switch (leaf->op) {
  case e_union:
    {
      int newNodeA, newNodeB, newNodeC, newNodeD;
      newNodeA = getNewNode();
      newNodeB = getNewNode();
      newNodeC = getNewNode();
      newNodeD = getNewNode();
      
      createTrans(start, newNodeA, EMPTY);
      createTrans(newNodeB, last, EMPTY);
      createTrans(start, newNodeC, EMPTY);
      createTrans(newNodeD, last, EMPTY);

      nfaIter(leaf->eleft, newNodeA, newNodeB);
      nfaIter(leaf->eright, newNodeC, newNodeD);
    }
    break;

  case e_concat:
    {
      int newNode = getNewNode();
      nfaIter(leaf->eleft, start, newNode);
      nfaIter(leaf->eright, newNode, last);
    }
    break;

  case e_closure:
    {
      int newNodeA, newNodeB;
      newNodeA = getNewNode();
      newNodeB = getNewNode();
      
      createTrans(start, newNodeA, EMPTY);
      createTrans(newNodeB, newNodeA, EMPTY);
      createTrans(newNodeA, last, EMPTY);

      nfaIter(leaf->eleft, newNodeA, newNodeB);
    }
    break;

  case e_pclosure:
    {
      int newNodeA, newNodeB;
      newNodeA = getNewNode();
      newNodeB = getNewNode();
      
      createTrans(start, newNodeA, EMPTY);
      createTrans(newNodeB, newNodeA, EMPTY);
      createTrans(newNodeB, last, EMPTY);

      nfaIter(leaf->eleft, newNodeA, newNodeB);
    }
    break;

  case e_char:
    createTrans(start, last, leaf->ech);
    break;

  case e_empty:
    createTrans(start, last, EMPTY);
    break;
  }
}

static void createTrans(int from, int to, char ch)
{
  struct Ntrans* nt = (struct Ntrans*)malloc(sizeof(struct Ntrans));
  nt->ch = ch;
  nt->to = to;
  nt->from = from;
  ntrans[ntransNum++] = nt;
}

static void setTransToNode()
{
  int i;
  for (i = 0; i < ntransNum; i++) {
    int from = ntrans[i]->from;
    if (nnode[from] != NULL) { // create new Nnode, and set to next link of nnode[i].

      struct Nnode* newNode = (struct Nnode*)malloc(sizeof(struct Nnode));
      newNode->ch = ntrans[i]->ch;
      newNode->to = ntrans[i]->to;
      newNode->next = nnode[from];
      nnode[from] = newNode;
    } else { // create new nnode[i]

      nnode[from] = (struct Nnode*)malloc(sizeof(struct Nnode));
      nnode[from]->ch = ntrans[i]->ch;
      nnode[from]->to = ntrans[i]->to;
      nnode[from]->next = NULL;

      if (from > nnodeMaxNum) nnodeMaxNum = from;
    }
  }
}

static void nfaInitial()
{
  nodeNum = 1;
  ntransNum = 0;
  nnodeMaxNum = 0;

  int i,j;
  for (i = 0; i < NTRANS_MAX; i++)
    ntrans[i] = NULL;

  for (j = 0; j < NNODE_MAX; j++)
    nnode[j] = NULL;
}


static void DfaClass()
{
  dfaInitial();
  nfaToDfa();
}

static void dfaInitial()
{
  // make the DFA state of number 0
  dnode[0] = (struct Dnode *)malloc(sizeof(struct Dnode));
  dtrans[0] = (struct Dtrans *)malloc(sizeof(struct Dtrans));

  dnode[0]->op = e_node;
  dnode[0]->nd_sib = NULL;

  dtrans[0]->nnode_no = 0;
  dtrans[0]->next = NULL;
  struct Nnode* node = nnode[0];
  setEmptyFromNnode(node, &dtrans[0]);

  if (isFinal(0))
    dnode[0]->op = e_final;

  numOfDnode = 0;
}

static void setEmptyFromNnode(struct Nnode* node, struct Dtrans** dtrans)
{
  if (node == NULL) return;
  
  struct Nnode* p;
  for (p = node; p != NULL; p = p->next) {
    if (p->ch == EMPTY) {
      struct Dtrans* newTrans = (struct Dtrans *)malloc(sizeof(struct Dtrans));
      newTrans->nnode_no = p->to;
      newTrans->next = *dtrans;
      *dtrans = newTrans;

      /** Check if the nnode next to this node is EMPTY */
      struct Nnode* nextNode = nnode[p->to];
      setEmptyFromNnode(nextNode, dtrans);
    }
  }
}

static void nfaToDfa()
{
  int numCheckingDnode = 0;

  while (numCheckingDnode <= numOfDnode) {

    struct Dtrans* dtOuter;
    for (dtOuter = dtrans[numCheckingDnode];
         dtOuter != NULL;
         dtOuter = dtOuter->next) {

      int nnode_no = dtOuter->nnode_no;

      // final point of nnode
      if (nnode[nnode_no] == NULL) continue;

      char ch = nnode[nnode_no]->ch;
      if (ch == EMPTY) continue;

      int next_nnode_no = nnode[nnode_no]->to;
      struct Dtrans* dtrans_tmp = (struct Dtrans *)malloc(sizeof(struct Dtrans));
      dtrans_tmp->nnode_no = next_nnode_no;
      dtrans_tmp->next = NULL;

      // If the next of this nnode[] is EMPTY,
      // add the EMPTY state to dtrans_tmp.
      setEmptyFromNnode(nnode[next_nnode_no], &dtrans_tmp);

      // ch != EMPTY. Search same ch at next dtranses.
      struct Dtrans* dtInner;
      for (dtInner = dtOuter->next;
           dtInner != NULL;
           dtInner = dtInner->next) {

        int noNext = dtInner->nnode_no;
        char chNext = nnode[noNext]->ch;
        if (chNext != ch) continue;

        int next_noNext = nnode[noNext]->to;
        struct Dtrans* newTransNext = (struct Dtrans *)malloc(sizeof(struct Dtrans));
        newTransNext->nnode_no = next_noNext;
        newTransNext->next = dtrans_tmp;
        dtrans_tmp = newTransNext;

        // If the next of this nnode[] is EMPTY,
        // add the EMPTY state to dtrans_tmp.
        setEmptyFromNnode(nnode[next_noNext], &dtrans_tmp);
      }

      /** Now, not empty dtrans_tmp exist.
       *  We compare this dtrans_tmp to dtrans[x] as x <= numOfDnode
       */
      int needMakeNew = compareDtrans(dtrans_tmp, numOfDnode);
      if (needMakeNew != -1) { // The same state already exist as dtrans_tmp.

        struct Dnode* newDnode = (struct Dnode *)malloc(sizeof(struct Dnode));
        newDnode->op = e_transition;
        newDnode->tr_ch = ch;
        newDnode->tr_next = dnode[needMakeNew];
        newDnode->tr_sib = dnode[numCheckingDnode]->nd_sib;
        dnode[numCheckingDnode]->nd_sib = newDnode;
      } else { // not exist. and make new state

        numOfDnode++;
        dtrans[numOfDnode] = dtrans_tmp;
        dnode[numOfDnode] = (struct Dnode *)malloc(sizeof(struct Dnode));
        dnode[numOfDnode]->op = e_node;
        dnode[numOfDnode]->nd_sib = NULL;

        struct Dnode* newDnode = (struct Dnode *)malloc(sizeof(struct Dnode));
        newDnode->op = e_transition;
        newDnode->tr_ch = ch;
        newDnode->tr_next = dnode[numOfDnode];
        newDnode->tr_sib = dnode[numCheckingDnode]->nd_sib;
        dnode[numCheckingDnode]->nd_sib = newDnode;

        if (isFinal(numOfDnode))
          dnode[numOfDnode]->op = e_final;
      }
    }
    
    numCheckingDnode++;
  }
}

/**
 * If a dtrans exist the same dtrans_tmp, return the state number
 * else return -1
 */
static int compareDtrans(struct Dtrans* dtrans_tmp, int checkNum)
{
  const int NOT_EQUAL = 0;
  const int EQUAL = 1;
  int compFlag = EQUAL;

  int count;
  for (count = 0; count <= checkNum; count++) {
    struct Dtrans* dtp = dtrans_tmp;
    struct Dtrans* dti  = dtrans[count];
    
    if (getLinkSize(dtp) != getLinkSize(dti))
      continue;

    for (; dtp != NULL && dti != NULL ; dtp = dtp->next, dti = dti->next) {
      if ( dtp->nnode_no == dti->nnode_no ) continue;
      else {
        compFlag = NOT_EQUAL;
        break;
      }
    }

    if (compFlag == NOT_EQUAL) {
      compFlag = EQUAL;
      continue;
    }

    return count;
  }

  return -1;
}

static int getLinkSize(struct Dtrans* dtrans)
{
  int size = 0;
  struct Dtrans* p;
  for (p = dtrans; p != NULL; p = p->next)
    size++;

  return size;
}

static int isFinal(int dfastate)
{
  struct Dtrans* p = dtrans[dfastate];
  for (; p != NULL; p = p->next)
    if (p->nnode_no == 1)  return TRUE;

  return FALSE;
}

static int dfaRegexp(char* search)
{
  DfaClass();

  char* p = search;
  struct Dnode* node = dnode[0];
  struct Dnode* prev = node;
  int checkFlag = FALSE;

  while (*p != '\0') {

    if (node == NULL) return FALSE;

    struct Dnode* sib = node->e.sibling;
    for (; sib != NULL; sib = sib->tr_sib) {
      
      if (sib->tr_ch != *p) continue;

      checkFlag = TRUE;
      break;
    }
    if (checkFlag == FALSE) return FALSE;
    
    checkFlag = FALSE;
    
    node = sib->tr_next;
    prev = node;
    p++;
  }

  if (prev->op != e_final) return FALSE;

  return TRUE;
}

