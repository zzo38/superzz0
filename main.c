#if 0
gcc -s -O2 -o ~/bin/superzz0 main.c display.o edit.o game.o lumped.o window.o world.o `sdl-config --cflags --libs`
exit
#endif

#include "common.h"

Config config={
#define B(n,t,d) d,
#define F(n,t,d) d,
#define I(n,t,d) d,
#define S(n,t,d) d,
#include "config.inc"
#undef B
#undef F
#undef I
#undef S
};
Uint8 editor=0;

typedef struct {
  const char*name;
  char kind;
  char size;
  void*ptr;
} ConfigInfo;

static const ConfigInfo configinfo[]={
#define B(n,t,d) {#n,'B',sizeof(t),&config.n},
#define F(n,t,d) {#n,'F',sizeof(t),&config.n},
#define I(n,t,d) {#n,'I',sizeof(t),&config.n},
#define S(n,t,d) {#n,'S',sizeof(t),&config.n},
#include "config.inc"
#undef B
#undef F
#undef I
#undef S
};

static int compare_configinfo(const void*a,const void*b) {
  const ConfigInfo*x=a;
  const ConfigInfo*y=b;
  return strcmp(x->name,y->name);
}

static void set_config(const char*s) {
  char buf[128];
  const char*q=strchr(s,'=');
  ConfigInfo*c;
  ConfigInfo k={buf,0,0,0};
  unsigned long long v;
  if(!q) errx(1,"Invalid configuration");
  snprintf(buf,q-s>126?126:q+1-s,"%s",s);
  c=bsearch(&k,configinfo,sizeof(configinfo)/sizeof(ConfigInfo),sizeof(ConfigInfo),compare_configinfo);
  if(!c) errx(1,"Unknown configuration option '%s'",buf);
  switch(c->kind) {
    case 'B': case 'I':
      v=strtoll(q+1,0,0);
      if(c->size==sizeof(int)) *(int*)(c->ptr)=v;
      else if(c->size==sizeof(Uint8)) *(Uint8*)(c->ptr)=v;
      else if(c->size==sizeof(Uint16)) *(Uint16*)(c->ptr)=v;
      else if(c->size==sizeof(Uint32)) *(Uint32*)(c->ptr)=v;
      else if(c->size==sizeof(long)) *(long*)(c->ptr)=v;
      else if(c->size==sizeof(long long)) *(long long*)(c->ptr)=v;
      else errx(1,"Unexpected error in configuration");
      break;
    case 'F':
      if(c->size==sizeof(float)) *(float*)(c->ptr)=strtod(q+1,0);
      else if(c->size==sizeof(double)) *(double*)(c->ptr)=strtod(q+1,0);
      else errx(1,"Unexpected error in configuration");
      break;
    case 'S':
      if(c->size==sizeof(char*)) *(char**)(c->ptr)=strdup(q+1);
      else errx(1,"Unexpected error in configuration");
      break;
  }
}

int main(int argc,char**argv) {
  int b=-1;
  int i;
  const char*s;
  while((i=getopt(argc,argv,"+b:e"))>0) switch(i) {
    case 'b': b=strtol(optarg,0,10); break;
    case 'e': editor=1; break;
    default: errx(1,"Wrong switches");
  }
  if(optind>=argc) errx(1,"Too few arguments");
  for(i=optind+1;i<argc;i++) set_config(argv[i]);
  if(b!=-42) {
    if(open_world(argv[optind])) err(1,"Error opening world");
    if(s=init_world()) errx(1,"Cannot initialize world settings: %s",s);
  }
  init_display();
  if(editor) run_editor(b); else run_game(b);
  return 0;
}

