#if 0
gcc -s -O2 -o ~/bin/superzz0 -Wno-unused-result main.c display.o edit.o editbrd.o editscr.o game.o lumped.o window.o world.o -lm `sdl-config --cflags --libs`
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

static void combine_raw(void) {
  FILE*fp;
  char nam[16];
  Uint8 buf[4];
  Uint32 len;
  int c,i;
  for(;;) {
    i=0;
    while((c=getchar())>0 && i<15) nam[i++]=(c>='a' && c<='z'?c+'A'-'a':c);
    if(c<0) break;
    nam[i]=0;
    if(c) while((c=getchar())>0);
    fread(buf,1,4,stdin);
    len=(buf[0]<<16)|(buf[1]<<24)|(buf[2]<<0)|(buf[3]<<8);
    fp=open_lump(nam,"w");
    if(!fp) errx(1,"Cannot open %s lump for writing",nam);
    copy_stream(stdin,fp,len);
    fclose(fp);
  }
}

void run_test_game(int b) {
  FILE*fp;
  Uint8 r=*v_status;
  memset(v_color,0,80*25);
  *v_status='$';
  redisplay();
  unlink(".superzz0_testgame");
  if(save_world(".superzz0_testgame")) {
    alert_text("Unable to save temporary world file");
    *v_status=r;
    return;
  }
  fp=popen("/proc/self/exe -\xFE .superzz0_testgame","w");
  if(!fp) {
    warn(0);
    alert_text("Cannot fork process");
    *v_status=r;
    return;
  }
  // This sequence of writes must match the sequence of reads below.
    fwrite(&b,1,sizeof(b),stdin);
    fwrite(&config,1,sizeof(config),stdin);
  pclose(fp);
  unlink(".superzz0_testgame");
  *v_status=r;
}

int main(int argc,char**argv) {
  Uint8 o=0;
  int b=-1;
  int i;
  const char*s;
  while((i=getopt(argc,argv,"+ab:er\xFE"))>0) switch(i) {
    case 'a': case 'r': o=(o&0x80)|i; break;
    case 'b': b=strtol(optarg,0,10); break;
    case 'e': editor=1; break;
    case '\xFE': o|=0x80; break;
    default: errx(1,"Wrong switches");
  }
  if(optind>=argc) errx(1,"Too few arguments");
  for(i=optind+1;i<argc;i++) set_config(argv[i]);
  if(o&0x80) {
    fread(&b,1,sizeof(b),stdin);
    fread(&config,1,sizeof(config),stdin);
  }
  if(open_world(argv[optind])) err(1,"Error opening world");
  if(editor && o) switch(o&0x7F) {
    case 'a':
      if(s=init_world()) errx(1,"Cannot initialize world settings: %s",s);
      combine_assembled();
      save_world(0);
      return 0;
    case 'r':
      combine_raw();
      save_world(0);
      return 0;
  }
  if(s=init_world()) errx(1,"Cannot initialize world settings: %s",s);
  if(b>=0) {
    cur_board_id=b;
    if(!editor && (s=select_board(b))) errx(1,"Cannot load board: %s",s);
  }
  init_display();
  if(editor) run_editor(); else run_game();
  return 0;
}

