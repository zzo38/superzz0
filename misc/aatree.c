#if 0
gcc -g -O0 -o ./aatree_test aatree.c
exit
#endif

/*
 Implementation of AA tree (not necessarily complete); might later be used
 (possibly with some modifications) as a part of implementation of ECS. (A
 different implementation not using this is another possibility.)

 (This program also includes testing funcion.)
*/

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef struct Tree Tree;
typedef struct Cursor Cursor;

struct Tree {
  Tree*left;
  Tree*right;
  Uint8 level;
  Uint8 data[0];
};

struct Cursor {
  Uint8 index;
  Uint32 path;
  Tree*link[0];
};

// These two variables are used when deleting.
static Tree*t_deleted;
static Tree*t_last;

static Tree*t_skew(Tree*t) {
  if(t && t->left && t->left->level==t->level) {
    Tree*x=t->left;
    t->left=x->right;
    x->right=t;
    return x;
  } else return t;
}

static Tree*t_split(Tree*t) {
  if(t && t->right && t->right->right && t->right->right->level==t->level) {
    Tree*x=t->right;
    t->right=x->left;
    x->left=t;
    x->level++;
    return x;
  } else return t;
}

static Tree*t_insert(Tree*t,const Uint8*key,size_t klen,size_t mlen,Tree**result,char*ok) {
  if(t) {
    int i=memcmp(key,t->data,klen);
    if(i<0) {
      t->left=t_insert(t->left,key,klen,mlen,result,ok);
    } else if(i>0) {
      t->right=t_insert(t->right,key,klen,mlen,result,ok);
    } else {
      *ok=0;
      return *result=t;
    }
    return ok?t_split(t_skew(t)):t;
  } else {
    t=malloc(sizeof(Tree)+mlen);
    if(!t) err(1,"Allocation failed");
    t->left=t->right=0;
    t->level=1;
    memcpy(t->data,key,klen);
    *ok=1;
    return *result=t;
  }
}

static Tree*t_delete(Tree*t,const Uint8*key,size_t klen,size_t mlen,char*ok) {
  *ok=0;
  if(!t) return 0;
  t_last=t;
  if(memcmp(key,t->data,klen)<0) {
    t->left=t_delete(t->left,key,klen,mlen,ok);
  } else {
    t_deleted=t;
    t->right=t_delete(t->right,key,klen,mlen,ok);
  }
  if(t==t_last && t_deleted && !memcmp(key,t_deleted->data,klen)) {
    memcpy(t_deleted->data,t->data,mlen);
    t_deleted=0;
    t=t->right;
    free(t_last);
    t_last=0;
    *ok=1;
  } else if((t->left?t->left->level:0)<t->level-1 || (t->right?t->right->level:0)<t->level-1) {
    t->level--;
    if(t->right && t->right->level>t->level) t->right->level=t->level;
    t=t_skew(t);
    if(t->right=t_skew(t->right)) t->right->right=t_skew(t->right->right);
    t=t_split(t);
    t->right=t_split(t->right);
  }
  return t;
}

static void t_deleteall(Tree*t) {
  if(!t) return;
  t_deleteall(t->left);
  t_deleteall(t->right);
  free(t);
}

static void t_deleterange(Tree*t,const Uint8*lo,const Uint8*hi,size_t klen,size_t mlen) {
  // Not implemented yet
}

static Tree*t_find(Tree*t,const Uint8*key,size_t klen) {
  int i;
  while(t && (i=memcmp(key,t->data,klen))) t=(i<0?t->left:t->right);
  return t;
}

static Tree*t_init(Uint32 count,size_t mlen) {
  // This function is supposed to make a tree with no data, which has the
  // specified number of nodes and is balanced, but is not implemented yet.
}

static Tree*tc_first(Cursor*c) {
  Tree*t=c->link[0];
  int n=0;
  do c->link[n++]=t; while(t=t->left);
  c->index=n-1;
  c->path=0;
  return c->link[n-1];
}

static Tree*tc_find(Cursor*c,const Uint8*key,size_t klen) {
  Tree*t=c->link[0];
  int i;
  int n=0;
  Uint32 p=0;
  retry:
  while((c->link[n]=t) && (i=memcmp(key,t->data,klen))) {
    if(i<0) {
      t=t->left;
    } else {
      p|=(0x80000000UL>>n);
      t=t->right;
    }
    n++;
  }
  c->index=n;
  c->path=p;
  return t;
}

static Tree*tc_findnear(Cursor*c,const Uint8*key,size_t klen) {
  Tree*t=c->link[0];
  int i;
  int n=0;
  int m=-1;
  Uint32 p=0;
  retry:
  while((c->link[n]=t) && (i=memcmp(key,t->data,klen))) {
    if(i<0) {
      m=n;
      t=t->left;
    } else {
      p|=(0x80000000UL>>n);
      t=t->right;
    }
    n++;
  }
  if(!t && m>=0) {
    t=c->link[n=m];
    p&=~((0x80000000UL>>n)-1);
  }
  c->index=n;
  c->path=p;
  return t;
}

static Tree*tc_next(Cursor*c) {
  Tree*t;
  Uint32 p;
  int n=c->index;
  if(t=c->link[n]->right) {
    c->path|=(0x80000000UL>>n);
    do c->link[++n]=t; while(t=t->left);
    return c->link[c->index=n];
  }
  if(!n--) return 0;
  p=c->path+(0x80000000UL>>n);
  c->path&=p;
  if(!p) return 0;
  c->index=31-__builtin_ctz(p);
  return c->link[c->index];
}

static Tree*tc_skip(Cursor*c,const Uint8*key,size_t klen,char*more) {
  Tree*t;
  Uint32 p=c->path;
  int i;
  int n=c->index;
  int m=-1;
  if(!(t=c->link[n])) goto end;
  if(!memcmp(key,t->data,klen)) {
    if(more) *more=1;
    return t;
  }
  retry:
  if(t=t->right) {
    p|=(0x80000000UL>>n);
    while((c->link[n]=t) && (i=memcmp(key,t->data,klen))) {
      if(i<0) {
        m=n;
        t=t->left;
      } else {
        p|=(0x80000000UL>>n);
        t=t->right;
      }
      n++;
    }
    if(!t && m>=0) {
      n=m;
      p&=~((0x80000000UL>>n)-1);
    }
  } else {
    if(!n--) {
      end:
      if(more) *more=0;
      return 0;
    }
    p+=(0x80000000UL>>n);
    if(!p) return 0;
    n=31-__builtin_ctz(p);
    p&=c->path;
    if(t=c->link[n]) {
      i=memcmp(key,t->data,klen);
      if(i<0) goto end;
      if(i>0) goto retry;
    }
  }
  if(more) *more=1;
  c->index=n;
  c->path=p;
  return t;
}

static Cursor*tc_open(Tree*t) {
  Cursor*c=malloc(sizeof(Cursor)+(t->level*2+2)*sizeof(Tree*));
  if(!c) err(1,"Allocation failed");
  c->index=0;
  c->link[0]=t;
  return c;
}

#define KEYSIZE 1
int main(int argc,char**argv) {
  Cursor*c;
  Tree*t=0;
  Tree*y;
  char ok;
  int i;
  Uint8 k[16];
  for(i=1;i<argc;i++) {
    snprintf(k,16,"%s",argv[i]);
    t=t_insert(t,k,KEYSIZE,16,&y,&ok);
    printf("RootLevel=%d InsertedLevel=%d OK=%d Data='%s'\n",t->level,y->level,ok,k);
    if(ok) memcpy(y->data,k,16);
  }
  c=tc_open(t);

#if 1
  for(y=tc_first(c);y;y=tc_next(c)) {
    printf("Index=%d Path=$%08lX Level=%d Data='%s'\n",c->index,(unsigned long)c->path,y->level,y->data);
  }
#endif

#if 0
  for(*k='a';*k<='z';(*k)++) {
    if(y=tc_findnear(c,k,1)) {
      printf("'%c' Index=%d Path=$%08lX Level=%d Data='%s'\n",*k,c->index,(unsigned long)c->path,y->level,y->data);
      while(y=tc_next(c)) printf(" -  Index=%d Path=$%08lX Level=%d Data='%s'\n",c->index,(unsigned long)c->path,y->level,y->data);
    }
  }
#endif

  return 0;
}
