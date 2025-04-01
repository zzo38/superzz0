#if 0
gcc -g -O0 -o ~/bin/superzz0 -Wno-unused-result main.c asn1.o audio.o display.o edit.o editbrd.o editscr.o edittext.o game.o lumped.o printer.o savegame.o window.o world.o -lm `sdl-config --cflags --libs`
exit
#endif

#define USING_RW_DATA
#include "common.h"
#include "version.inc"
#include "asn1.h"

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

static void load_config(char*nam) {
  char div=1;
  char*x;
  FILE*f;
  int n;
  char*line=0;
  size_t linesize=0;
  if(nam) {
    if(!*nam) return;
    f=fopen(nam,"r");
    if(!f) err(1,"Cannot open configuration file");
  } else {
    char*e=getenv("HOME");
    if(!e) return;
    nam=malloc(n=strlen(e)+14);
    if(!nam) err(1,"Allocation failed");
    snprintf(nam,n,"%s/.superzz0rc",e);
    f=fopen(nam,"r");
    free(nam);
    if(!f) return;
  }
  while(getline(&line,&linesize,f)>0) {
    *(x=strchrnul(line,'\n'))=0;
    if(x>line && x[-1]=='\r') *--x=0;
    if(*line=='#' || !*line) continue;
    if(*line=='[' && x>line && x[-1]==']') {
      div=0;
      if(!strcmp(line,"[Options]")) div=1;
      if(!editor && !strcmp(line,"[Joystick]")) div=2;
      continue;
    }
    switch(div) {
      case 1: set_config(line); break;
      case 2: configure_joystick(0,line); break;
    }
  }
  fclose(f);
  free(line);
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
  unlink(config.temporary_file_2);
  if(save_world(config.temporary_file_2)) {
    alert_text("Unable to save temporary world file");
    *v_status=r;
    return;
  }
  fp=popen("/proc/$PPID/exe -\\\\ .superzz0_testgame","w");
  if(!fp) {
    warn(0);
    alert_text("Cannot fork process");
    *v_status=r;
    return;
  }
  // This sequence of writes must match the sequence of reads below.
    fwrite(&b,1,sizeof(b),fp);
    fwrite(&config,1,sizeof(config),fp);
#define B(n,t,d)
#define F(n,t,d)
#define I(n,t,d)
#define S(n,t,d) if(config.n) { write32(fp,strlen(config.n)); fwrite(config.n,1,strlen(config.n),fp); }
#include "config.inc"
#undef B
#undef F
#undef I
#undef S
  pclose(fp);
  unlink(config.temporary_file_2);
  *v_status=r;
}

static void random_test(const char*s) {
  unsigned long long a=0,b=0,c=0;
  sscanf(s,"%llu,%llu,%llu",&a,&b,&c);
  reseed(a);
  while(c--) printf("%lu\n",(unsigned long)dice(b));
}

static void create_world(const char*template,const char*name) {
  static const Uint8 data[]={
    '!','S','Z','0', 0, 0,0,7,0,
      1,1,1,1,1,1,1,
    'E','L','E','M','E','N','T', 0, 0,0,1,0,
      0,
    'S','T','A','R','T', 0, 0,0,12,0,
      1,0, 0,0, 255,255, 0,0,0,0,0,0,
  };
  Uint8 buf[256];
  FILE*f;
  FILE*ft=0;
  int i;
  if(template) {
    ft=fopen(template,"r");
    if(!ft) err(1,"Cannot open template file");
  }
  f=fopen(name,"wx");
  if(!f) err(1,"Cannot create world file");
  if(template) {
    if(fread(buf,1,16,ft)!=16 || memcmp(buf,data,9)) errx(1,"Not a real template file");
    fwrite(data,1,16,f);
    copy_stream(ft,f,-1);
    fclose(ft);
  } else {
    fwrite(data,1,sizeof(data),f);
  }
  fclose(f);
  if(template) {
    if(open_world(name)) err(1,"Cannot open world");
    if(f=open_lump("CATALOG.DER","w")) fclose(f);
    if(f=open_lump("HISTORY.DER","w")) fclose(f);
    if(f=open_lump("TEMPLATE.DER","r")) {
      ASN1_Value a,b,c;
      ASN1_Iterator t,tt;
      FILE*g;
      if(asn1_read_item(f,&a,0) || a.class || a.type!=ASN1_SEQUENCE || !a.constructed) {
        warnx("ASN.1 error with TEMPLATE.DER");
      } else {
        asn1_foreach(i,&t,&a,&b) {
          if(b.class==ASN1_CONTEXT_SPECIFIC && b.constructed) {
            switch(b.type) {
              case 0: // delete lumps from world file
                asn1_foreach(i,&tt,&b,&c) {
                  if(c.type==ASN1_VISIBLE_STRING && !c.constructed && c.length>0 && c.length<256) {
                    memcpy(buf,c.data,c.length);
                    buf[c.length]=0;
                    if(g=open_lump(buf,"w")) fclose(g);
                  }
                }
                break;
              default: warnx("Unrecognized command [%lu] in TEMPLATE.DER",(unsigned long)b.type);
            }
          } else {
            warnx("ASN.1 error with TEMPLATE.DER");
            break;
          }
        }
        if(i!=ASN1_DONE) asn1error: warnx("ASN.1 error with TEMPLATE.DER");
      }
      asn1_free(&a);
      fclose(f);
      if(f=open_lump("TEMPLATE.DER","w")) fclose(f);
    }
    save_world(0);
  }
}

static void show_version(void) {
  int a;
  printf("%sHash: ",version_name);
  for(a=0;a<32;a++) printf("%02X",version_hash[a]);
  putchar('\n');
}

int main(int argc,char**argv) {
  Uint8 o=0;
  int b=-1;
  int i;
  char*configname=0;
  const char*s=0;
  while((i=getopt(argc,argv,"+TVab:c:denq:rt:w\\"))>0) switch(i) {
    case 'T': case 'a': case 'r': o=(o&0x80)|i; break;
    case 'V': show_version(); return 0;
    case 'b': b=strtol(optarg,0,10); break;
    case 'c': configname=optarg; break;
    case 'd': config.debug=1; break;
    case 'e': editor=1; break;
    case 'n': configname=""; break;
    case 'q': random_test(optarg); return 0;
    case 't': o=1; s=optarg; break;
    case 'w': o=1; break;
    case '\\': o|=0x80; break;
    default: errx(1,"Wrong switches");
  }
  if(optind>=argc && o!='T') errx(1,"Too few arguments");
  load_config(configname);
  for(i=optind+1;i<argc;i++) set_config(argv[i]);
  if(o=='T') {
    init_display();
    puts(text_editor(0)?:(Uint8*)"");
    return 0;
  }
  if(o==1) {
    if(!editor) errx(1,"Cannot use -w or -t without -e");
    create_world(s,argv[optind]);
  }
  if(o&0x80) {
    fread(&b,1,sizeof(b),stdin);
    fread(&config,1,sizeof(config),stdin);
#define B(n,t,d)
#define F(n,t,d)
#define I(n,t,d)
#define S(n,t,d) if(config.n) { char*s; i=read32(stdin); config.n=s=malloc(i+1); if(!s) err(1,"Allocation failed"); fread(s,1,i,stdin); s[i]=0; }
#include "config.inc"
#undef B
#undef F
#undef I
#undef S
    if(open_world(config.temporary_file_2)) err(1,"Error opening world");
  } else {
    if(open_world(argv[optind])) err(1,"Error opening world");
  }
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
  if(joystat) configure_joystick(2,0);
  if(config.version_warn&4) {
    alert_text("This file requires a newer version of Super ZZ Zero");
    if(!editor) errx(1,"This file requires a newer version of Super ZZ Zero.");
  } else if(config.version_warn&2) {
    alert_text("Warning: This file should use a newer version of Super ZZ Zero");
  }
  if(config.audio_buffer && !editor) audio_init();
  if(editor) run_editor(); else run_game();
  return 0;
}

