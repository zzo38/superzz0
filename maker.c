#if 0
gcc -s -O2 -o ./maker maker.c
exit
#endif

// This file is public domain.

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct Rule {
  const char*exe;
  int*in;
  int*out;
  int nin,nout;
} Rule;

typedef struct Object {
  const char*name;
  time_t mtime;
  int rule;
  int*dep;
  int ndep;
  char done;
} Object;

static Object*objects;
static int nobjects;
static Rule*rules;
static int nrules;
static unsigned short options=0;
static int goal=-1;
static const char*goalname="$";

static int find_object(const char*name) {
  int i;
  struct stat s;
  for(i=0;i<nobjects;i++) if(!strcmp(name,objects[i].name)) return i;
  i=nobjects;
  objects=realloc(objects,++nobjects*sizeof(Object));
  if(!objects) err(1,"Allocation failed");
  objects[i].name=strdup(name);
  if(!objects[i].name) err(1,"Allocation failed");
  objects[i].rule=-1;
  objects[i].dep=0;
  objects[i].ndep=0;
  objects[i].done=0;
  if(*name=='$' || *name=='*') {
    objects[i].mtime=0x7FFFFFFF7FFFFFFFLL;
  } else {
    if(stat(name,&s)) {
      if(errno!=ENOENT) err(1,"Cannot stat file '%s'",name);
      objects[i].mtime=0;
    } else {
      objects[i].mtime=s.st_mtime;
    }
  }
  if(!strcmp(goalname,name)) goal=i;
  return i;
}

static Rule*add_rule(void) {
  int i=nrules;
  rules=realloc(rules,++nrules*sizeof(Rule));
  if(!rules) err(1,"Allocation failed");
  rules[i].exe=0;
  rules[i].in=rules[i].out=0;
  rules[i].nin=rules[i].nout=0;
  return rules+i;
}

static int add_dependency(int ob,int de) {
  int i;
  Object*o=objects+ob;
  if(ob<0 || de<0) return 0;
  if(ob==de) errx(1,"Circular dependency in '%s'",o->name);
  for(i=0;i<o->ndep;i++) if(o->dep[i]==de) return 0;
  ++o->ndep;
  o->dep=realloc(o->dep,o->ndep*sizeof(int));
  if(!o->dep) err(1,"Allocation failed");
  o->dep[o->ndep-1]=de;
  return 1;
}

static int add_dependencies(int o1,int o2) {
  int i;
  int t=0;
  Object*o=objects+o2;
  for(i=0;i<o->ndep;i++) {
    if(o1==o->dep[i]) errx(1,"Circular dependency in '%s'",o->name);
    if(o1!=o2) t|=add_dependency(o1,o->dep[i]);
    t|=add_dependencies(o1,o->dep[i]);
  }
  return t;
}

static void add_all_dependencies(void) {
  int i;
  int t=0;
  Object*o;
  for(i=0;i<nobjects;i++) {
    o=objects+i;
    if(o->rule>=0) {
      o->ndep=rules[o->rule].nin;
      o->dep=malloc(o->ndep*sizeof(int));
      if(!o->dep) err(1,"Allocation failed");
      memcpy(o->dep,rules[o->rule].in,o->ndep*sizeof(int));
    }
  }
  do {
    t=0;
    for(i=0;i<nobjects;i++) t|=add_dependencies(i,i);
  } while(t);
}

static void parse_mak_file(const char*filename) {
  char*line=0;
  char*s;
  char*p;
  Rule*r;
  int i,j;
  int n=0;
  size_t line_size=0;
  FILE*fp=(*filename=='!'?popen(filename+1,"re"):fopen(filename,"r"));
  if(!fp) err(1,"Cannot open file");
  while(getline(&line,&line_size,fp)>0) {
    n++;
    s=line;
    for(i=0;s[i];i++) if((s[i]<32 && s[i]!='\n') || s[i]>126) errx(1,"Improper character encoding on line %d",n);
    while(*s==' ') s++;
    if(*s=='#' || !*s || *s=='\n') continue;
    if(*s=='!') {
      parse_mak_file(s);
      continue;
    }
    r=add_rule();
    if(*s!='-' || s[1]!='>' || s[2]!=' ') for(;;) {
      s=strchr(s,' ');
      if(!s) errx(1,"Malformed rule on line %d",n);
      while(*s==' ') s++;
      r->nin++;
      if(*s==':' && s[1]==' ') errx(1,"Malformed rule on line %d",n);
      if(*s=='-' && s[1]=='>' && s[2]==' ') break;
    }
    s+=3;
    for(;;) {
      s=strchr(s,' ');
      if(!s) errx(1,"Malformed rule on line %d",n);
      while(*s==' ') s++;
      r->nout++;
      if(*s=='-' && s[1]=='>' && s[2]==' ') errx(1,"Malformed rule on line %d",n);
      if(*s==':' && s[1]==' ') break;
    }
    r->exe=strdup(s+2);
    r->in=calloc(r->nin+r->nout,sizeof(int));
    if(!r->exe || !r->in) err(1,"Allocation failed");
    *strchrnul(r->exe,'\n')=0;
    r->out=r->in+r->nin;
    s=line;
    while(*s==' ') s++;
    for(i=0;;) {
      if(*s=='-' && s[1]=='>' && s[2]==' ') s+=3;
      if(*s==':' && s[1]==' ') break;
      p=strchr(s,' ');
      *p++=0;
      while(*p==' ') p++;
      r->in[i++]=j=find_object(s);
      if(i>r->nin) {
        if(objects[j].rule!=-1) errx(1,"Multiple rules with output '%s'",objects[j].name);
        objects[j].rule=nrules-1;
      }
      s=p;
    }
  }
  if(*filename=='!') {
    i=pclose(fp);
    if(i==-1) err(1,"pclose");
    if(!WIFEXITED(i)) errx(1,"Subprocess exited abnormally");
    if(i=WEXITSTATUS(i)) errx(2,"Subprocess terminated with exit code %d",i);
  } else {
    fclose(fp);
  }
  free(line);
}

static void show_object(int id) {
  Object*o=objects+id;
  int i;
  printf("Object %d = \"%s\"\n",id,o->name);
  printf("  Time = %lld\n",(long long)o->mtime);
  printf("  Rule = %d\n",o->rule);
  if(o->ndep) {
    printf("  Dependencies =");
    for(i=0;i<o->ndep;i++) printf(" %d",o->dep[i]);
    printf("\n");
  }
}

static void show_rule(int id) {
  Rule*o=rules+id;
  int i;
  printf("Rule %d = \"%s\"\n",id,o->exe);
  printf("  In =");
  for(i=0;i<o->nin;i++) printf(" %d",o->in[i]);
  printf("\n");
  printf("  Out =");
  for(i=0;i<o->nout;i++) printf(" %d",o->out[i]);
  printf("\n");
}

static void exec_rule(int ru) {
  int i;
  Rule*r=rules+ru;
  puts(r->exe);
  if(!(options&0x0004)) {
    i=system(r->exe);
    if(i==-1) err(1,"pclose");
    if(!WIFEXITED(i)) errx(1,"Subprocess exited abnormally");
    if(i=WEXITSTATUS(i)) errx(2,"Subprocess terminated with exit code %d",i);
  }
  for(i=0;i<r->nout;i++) objects[r->out[i]].done=1;
}

static void work(void) {
  int i,n;
  Object*o;
  for(n=-1;n<objects[goal].ndep;n++) {
    o=objects+(n==-1?goal:objects[goal].dep[n]);
    if(!o->done) {
      if(!o->ndep) goto nowork;
      if(!(options&0x0002) && o->name[0]!='*' && o->name[0]!='$') {
        for(i=0;i<o->ndep;i++) if(objects[o->dep[i]].mtime>=o->mtime) goto work;
        if(options&0x0008) fprintf(stderr,"Object \"%s\" skipped due to timestamp\n",o->name);
        o->done=1;
        goto skip;
      }
      work:
      for(i=0;i<rules[o->rule].nin;i++) if(!objects[rules[o->rule].in[i]].done) goto skip;
      if(o->rule>=0) {
        if(options&0x0008) fprintf(stderr,"Working rule %d for object \"%s\"\n",o->rule,o->name);
        exec_rule(o->rule);
      } else {
        nowork:
        if(options&0x0008) fprintf(stderr,"Nothing to do for object \"%s\"\n",o->name);
      }
      o->done=1;
    }
    skip: ;
  }
}

int main(int argc,char**argv) {
  int i;
  while((i=getopt(argc,argv,"+ag:lntv"))>0) switch(i) {
    case 'a': options|=0x0002; break;
    case 'g': goalname=optarg; break;
    case 'l': options|=0x0001; break;
    case 'n': options|=0x0004; break;
    case 't': options|=0x0010; break; // it is supposed to measure the time usage, but not implemented yet
    case 'v': options|=0x0008; break;
    default: return 1;
  }
  if(optind>=argc) errx(1,"Too few arguments");
  if(argv[optind][0]=='!') errx(1,"Improper file name");
  parse_mak_file(argv[optind]);
  if(goal!=-1) {
    if(objects[goal].name[0]=='$') objects[goal].mtime=0;
  } else if(!(options&0x0001)) {
    errx(1,"No goal");
  }
  add_all_dependencies();
  if(options&0x0001) {
    for(i=0;i<nobjects;i++) show_object(i);
    for(i=0;i<nrules;i++) show_rule(i);
    return 0;
  }
  while(!objects[goal].done) work();
  return 0;
}
