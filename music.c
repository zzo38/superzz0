#if 0
gcc -g -O0 -o ~/bin/sz0music -std=gnu99 -fwrapv -Wno-multichar -Wno-unused-result music.c asn1.o -lm
exit
#endif

#define _GNU_SOURCE
#include <err.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "asn1.h"

typedef struct {
  FILE*file;
  char*text;
  size_t text_size;
  uint16_t addr;
} Track;

typedef struct {
  const char*name;
  void(*call)(const char*arg);
} Command;

typedef struct {
  uint16_t at,name;
} Lref;

static ASN1_Encoder*enc;
static char*line_mem;
static size_t line_size;
static Track track[26];
static uint8_t rom[0x10000];
static uint16_t romsize;
static char*textmacro[128];
static uint16_t songnum,nextsongnum;
static int8_t scale[10]={9,11,0,2,4,5,7,11,0,1};
static uint8_t scaleoct=12;
static uint16_t divisions=64;
static ASN1_Value*instrum[255];
static uint8_t ninstrum;
static int32_t tempo=120;
static int32_t tempo_n=1;
static int32_t tempo_d=60;
static uint8_t tuning_mode=0; // 0=default 1=Temperament 2=Tuning
static uint8_t tuning_count=12;
static double tuning_data[64]={2.0};
static uint8_t omit_standard=0;
static ASN1_Value*songs;
static uint32_t nsongs;
static uint16_t labels[0x4000];
static FILE*lrefs_file;
static Lref*lrefs_data;
static size_t lrefs_size;
static double*reals;
static uint8_t nreals;
static uint8_t lonote=96;
static uint8_t hinote=1;
static uint8_t waits[32];
static uint8_t nwaits;
static uint8_t nchannels;
static uint16_t envelope[255];

static char*parse_integer(const char*t,int32_t*v,char r) {
  int32_t n=0;
  while(*t==' ' || *t=='\t') ++t;
  if(*t=='$' || (*t=='0' && t[1]=='x')) {
    t+=(*t=='$'?1:2);
    while(*t) {
      if(*t>='0' && *t<='9') n=(n<<4)+*t++-'0';
      else if(*t>='A' && *t<='F') n=(n<<4)+*t++-'A'+10;
      else if(*t>='a' && *t<='f') n=(n<<4)+*t++-'a'+10;
      else break;
    }
    *v=n;
  } else if(*t=='-') {
    t++;
    while(*t) {
      if(*t>='0' && *t<='9') n=10*n+'0'-*t++;
      else break;
    }
    *v=n;
  } else if(*t=='+' || (*t>='0' && *t<='9')) {
    if(*t=='+') t++;
    while(*t) {
      if(*t>='0' && *t<='9') n=10*n+*t++-'0';
      else break;
    }
    *v=n;
  } else if(r) {
    errx(1,"Required integer");
  }
  return (char*)t;
}

static char*parse_real(const char*t,double*v,char r) {
  double d;
  char h[64];
  char*p;
  while(*t==' ' || *t=='\t') ++t;
  if(*t=='$') {
    if((t[1]<'0' || t[1]>'9') && (t[1]<'a' || t[1]>'f') && (t[1]<'A' || t[1]>'F')) {
      if(r) errx(1,"Required real number");
      return (char*)t;
    }
    snprintf(h,64,"0x%s",t+1);
    d=strtod(h,&p);
    if(p!=h) *v=d; else if(r) errx(1,"Required real number");
    return ((char*)t)+(p-h)+1;
  } else {
    if(*t!='-' && *t!='+' && *t!='.' && (*t<'0' || *t>'9')) {
      if(r) errx(1,"Required real number");
      return (char*)t;
    }
    d=strtod(t,&p);
    if(p!=t) *v=d; else if(r) errx(1,"Required real number");
    return p;
  }
}

static inline void add_op(uint8_t op) {
  if(romsize>=0xFFFE) errx(1,"Too big ROM size");
  rom[romsize++]=op;
}

static inline void add_op2(uint8_t op,uint8_t op2) {
  if(romsize>=0xFFFE) errx(1,"Too big ROM size");
  rom[romsize++]=op;
  rom[romsize++]=op2;
}

static inline void add_op2w(uint8_t op,uint16_t op2) {
  if(romsize>=0xFFFD) errx(1,"Too big ROM size");
  rom[romsize++]=op;
  rom[romsize++]=op2;
  rom[romsize++]=op2>>8;
}

static inline void add_lref(uint16_t name) {
  Lref x={.at=romsize,.name=name};
  if(romsize>=0xFFFE) errx(1,"Too big ROM size");
  fwrite(&x,1,sizeof(x),lrefs_file);
  romsize+=2;
}

static inline void add_numb(uint16_t x) {
  if(x<4) add_op(x+0xB0); else if(x<256) add_op2(0xB4,x); else add_op2w(0xB5,x);
}

static char*send_raw_codes(char*t) {
  int h;
  for(;;) {
    if(*t>='0' && *t<='9') h=*t++-'0'; else if(*t>='A' && *t<='F') h=10+*t++-'A'; else if(*t>='a' && *t<='f') h=10+*t++-'a'; else break;
    h<<=4;
    if(*t>='0' && *t<='9') h+=*t++-'0'; else if(*t>='A' && *t<='F') h+=10+*t++-'A'; else if(*t>='a' && *t<='f') h+=10+*t++-'a'; else errx(1,"Improper %%%% code");
    add_op(h);
  }
  return t;
}

static int find_real(double d) {
  int i;
  if(d==0.0) return 0;
  if(d==1.0) return 1;
  if(d==2.0) return 2;
  if(d==0.5) return 3;
  for(i=0;i<nreals;i++) if(reals[i]==d) return i+16;
  if(nreals==100) errx(1,"Too many real constants");
  reals=realloc(reals,(nreals+1)*sizeof(double));
  if(!reals) err(1,"Allocation failed");
  reals[nreals]=d;
  return 16+nreals++;
}

static int find_wait(uint8_t w) {
  int i;
  for(i=0;i<nwaits;i++) if(waits[i]==w) return i+0x60;
  if(nwaits==32) return 0;
  waits[nwaits++]=w;
  return nwaits+0x5F;
}

static uint16_t parse_notelen(char**p,uint16_t n) {
  char*t=*p;
  uint16_t v=n;
  uint16_t u;
  if(*t==',') t++;
  while(*t==' ') t++;
  if(*t>='0' && *t<='9') {
    n=*t++-'0';
    while(*t>='0' && *t<='9') n=10*n+*t++-'0';
    if(!n || !(n=divisions/n)) errx(1,"Improper note duration");
  } else if(*t=='=') {
    n=0; t++;
    while(*t>='0' && *t<='9') n=10*n+*t++-'0';
  }
  u=n;
  while(*t=='.') t++,n+=u/=2;
  *p=t;
  if(*t=='^') {
    ++*p;
    return n+parse_notelen(p,v);
  } else {
    return n;
  }
}

#define WaitBefore do{ if(waiting) { while(waiting>255) add_numb(128),add_op(0xEF),waiting-=128; if(waiting==outlen) add_op(0x91); else add_numb(waiting); waiting=0; add_op(0xEF); } }while(0)
static uint16_t do_track(char*t) {
  char*p;
  double d;
  int32_t v;
  uint32_t totaltime=0;
  uint32_t totaltime1=0;
  uint16_t inlen=divisions/4;
  uint16_t outlen=0;
  uint16_t start=romsize;
  int32_t transpose=0;
  uint8_t octave=0;
  uint16_t loopstart=0xFFFF;
  uint32_t waiting=0;
  uint8_t state='r';
  int i,j;
  for(;;) {
    while(*t && *t==' ') t++;
    switch(*t++) {
      case 'a' ... 'j':
        v=scale[t[-1]-'a']+scaleoct*octave;
        while((*t=='+' && (v++,1)) || (*t=='-' && (v--,1)) || (*t=='\'' && (v+=scaleoct,1))) t++;
        goto note;
      case 'k':
        WaitBefore;
        totaltime+=waiting=parse_notelen(&t,inlen);
        if(state=='r' || state=='k') break;
        state='k';
        i=0xF6; goto wait_op;
      case 'l':
        inlen=parse_notelen(&t,inlen);
        break;
      case 'n':
        t=parse_integer(t,&v,1);
      note:
        totaltime+=j=parse_notelen(&t,inlen);
        i=v+transpose;
        if(i<0 || i>95) errx(1,"Note number (%d) out of range",i);
        if(lonote>i) lonote=i;
        if(hinote<i) hinote=i;
        WaitBefore;
        if(j!=outlen) {
          j=find_wait(outlen=j);
          if(j) add_op(j); else add_numb(outlen),add_op(0x99);
        }
        add_op(i);
        state='n';
        break;
      case 'o':
        t=parse_integer(t,&v,1);
        if(v<0 || v*scaleoct>95) errx(1,"Octave number (%d) out of range",(int)v);
        octave=v;
        break;
      case 'r':
        WaitBefore;
        totaltime+=waiting=parse_notelen(&t,inlen);
        if(state=='r') break;
        state='r';
        i=0xF7; goto wait_op;
      case 't':
        WaitBefore;
        t=parse_integer(t,&v,1);
        add_numb(v); add_op2(0xB9,0x01);
        break;
      case 'v':
        WaitBefore;
        t=parse_real(t,&d,1);
        add_op2(0xC1,find_real(d));
        add_op2(0xBD,0xC1);
        break;
      case 'w':
        waiting+=i=parse_notelen(&t,inlen);
        totaltime+=i;
        break;
      case '<': if(octave>0) --octave; break;
      case '>': if(octave<9) ++octave; break;
      case 'K':
        t=parse_integer(t,&transpose,1);
        break;
      case 'L':
        if(loopstart!=0xFFFF) errx(1,"Too many global loop start commands in one track");
        totaltime1=totaltime;
        loopstart=romsize;
        outlen=state=0;
        break;
      case '%':
        WaitBefore;
        switch(*t++) {
          case '%':
            t=send_raw_codes(t);
            break;
          case ':':
            if(*t<=32 || *t>126) goto bad;
            if((*t>='0' && *t<='9') || *t=='$') {
              t=parse_integer(t,&v,1);
            } else {
              v=*t++*128;
              if(*t<=32 || *t>126) goto bad;
              v+=*t++;
            }
            if(v&~0x3FFF) errx(1,"Improper label name");
            if(labels[v]) errx(1,"Label %d already defined",(int)v);
            labels[v]=romsize;
            break;
          case '0' ... '9': case '$':
            t=parse_integer(t,&v,1);
            if(v&~0xFFFF) errx(1,"Immediate value out of range");
            add_numb(v);
            break;
          default: t--; goto bad;
        }
        break;
      case '@':
        WaitBefore;
        if((*t>='0' && *t<='9') || *t=='$') {
          t=parse_integer(t,&v,1);
          if(v&~255) errx(1,"Improper instrument number");
          add_numb(v); add_op2(0xB9,0x80);
        } else if(*t=='v' || *t=='u') {
          i=(*t=='v'?0x90:0x92);
          t=parse_integer(t+1,&v,1);
          if(v&~255) errx(1,"Improper envelope number");
          add_numb(v?envelope[v-1]:0); add_op2(0xB9,i);
        } else {
          goto bad;
        }
        break;
      case '|':
        if(*t!=':') goto bad;
        p=strstr(t+2,":|");
        if(!p) errx(1,"Improper repeat");
        parse_integer(p+2,&v,1);
        if(v<2 || v>255) errx(1,"Improper repeat count");
        add_numb(v);
        outlen=state=0;
        t+=2;
        WaitBefore;
        add_op(0xF2); break;
      case ':':
        if(*t++!='|') goto bad;
        t=parse_integer(t,&v,0);
        outlen=state=0;
        i=0xF4; goto wait_op;
      case '\\':
        i=0xF3; goto wait_op;
      case '!':
        if(*t++!='!') goto stop;
        start=romsize;
        outlen=0;
        break;
      default: bad: errx(1,"Improper character in track \"%c\" near \"%7.7s\"",t[-1],t);
      wait_op:
        if(waiting) {
          if(waiting==outlen) add_op(0x91); else add_numb(waiting);
          add_op(i-0x10);
          waiting=0;
        } else {
          add_op(i);
        }
    }
  }
  stop:
  if(loopstart==0xFFFF) add_op(0xCA); else add_op2w(0xA8,loopstart);
  fprintf(stderr," $%04X   %7lu  ",start,(unsigned long)totaltime);
  if(loopstart!=0xFFFF) fprintf(stderr,"%7lu",(unsigned long)totaltime1);
  fputc('\n',stderr);
  return start;
}

static void expand_loops(char*in,FILE*out) {
  char*p;
  char*q;
  int32_t m,n,k;
  loop:
  if(p=strchr(in,91)) {
    m=n=k=0;
    fwrite(in,1,p-in,out);
    q=++p;
    fputc(' ',out);
    for(;;) {
      if(*q=='|' && !k && q[-1]!=':' && q[+1]!=':') {
        if(n==1) break; else q++;
        fputc(' ',out);
      } else if(*q==91) {
        k++; fputc(*q++,out);
      } else if(*q==93) {
        if(k) {
          k--; fputc(*q++,out);
        } else {
          fputc(' ',out);
          if(!m) in=parse_integer(q+1,&m,1),n=m;
          if(m<=1) errx(1,"Improper loop count");
          if(--n) q=p; else break;
        }
      } else if(q) {
        fputc(*q++,out);
      } else {
        errx(1,"Loop misnesting");
      }
    }
    if(*in) goto loop;
  } else {
    fputs(in,out);
  }
}

static void finish_song(void) {
  char*t;
  char*p;
  ASN1_Encoder*e;
  int i,m;
  for(i=m=0;i<26;i++) {
    if(track[i].file) {
      fputs(" ! ",track[i].file);
      fclose(track[i].file);
      if(!track[i].text) err(1,"Allocation failed");
      track[i].file=0;
      m=i+1;
    } else {
      track[i].text=0;
      track[i].text_size=0;
    }
    track[i].addr=0;
  }
  if(m) {
    if(m>nchannels) nchannels=m;
    if(songnum>=nsongs) {
      songs=realloc(songs,(songnum+1)*sizeof(ASN1_Value));
      if(!songs) err(1,"Allocation failed");
      while(songnum>=nsongs) songs[nsongs++]=(ASN1_Value){.type=ASN1_NULL};
    }
    fprintf(stderr,"Song #%d:\n",songnum);
    if(songs[songnum].type!=ASN1_NULL) errx(1,"Duplicate song number");
    for(i=0;i<m;i++) if(t=track[i].text) {
      while(p=strchr(t,91)) {
        track[i].text=0;
        track[i].text_size=0;
        track[i].file=open_memstream(&track[i].text,&track[i].text_size);
        expand_loops(t,track[i].file);
        fclose(track[i].file);
        if(!t) err(1,"Allocation failed");
        track[i].file=0;
        t=track[i].text;
      }
      fprintf(stderr,"  %c ",i+'A');
      track[i].addr=do_track(t);
      free(t);
    }
    e=asn1_start_encoding_constructed_value(songs+songnum,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    if(!e) err(1,"Allocation failed");
      asn1_encode_integer(e,tempo);
      if(nextsongnum!=songnum) asn1_encode_integer(e,nextsongnum);
      for(i=0;i<m;i++) if(track[i].text) {
        asn1_construct(e,ASN1_CONTEXT_SPECIFIC,0,0);
          asn1_encode_integer(e,track[i].addr);
        asn1_end(e);
      }
    if(asn1_finish_encoder(e)) errx(1,"Unexpected error");
  }
  for(i=0;i<26;i++) {
    track[i].file=0;
    track[i].text=0;
    track[i].text_size=0;
  }
}

static void append_track_text(int n,const char*t) {
  FILE*f=track[n].file;
  fputc(' ',f);
  while(*t && *t!=';') {
    if(*t=='*') {
      if(!t[1] || (t[1]&~0x7F) || !textmacro[t[1]]) errx(1,"Improper text macro");
      fputs(textmacro[t[1]],f);
      t+=2;
    } else {
      if(*t=='\t') fputc(' ',f),t++; else fputc(*t++,f);
    }
  }
}

static void process_line(char*line);

static void cmd_chdir(const char*arg) {
  if(chdir(arg)) err(1,"Cannot change directory");
}

static void cmd_english(const char*arg) {
  scale[1]=11;
}

static void cmd_divisions(const char*arg) {
  int32_t v;
  arg=parse_integer(arg,&v,1);
  if(*arg || v<1) errx(1,"Improper divisions");
  divisions=v;
}

static void cmd_german(const char*arg) {
  scale[1]=10;
}

static void cmd_include(const char*arg) {
  FILE*f=fopen(arg,"r");
  if(!f) err(1,"Cannot open include file \"%s\"",arg);
  while(getline(&line_mem,&line_size,f)>0) process_line(line_mem);
  fclose(f);
}

static void cmd_next(const char*arg) {
  int32_t v=-1;
  arg=parse_integer(arg,&v,1);
  if(*arg) errx(1,"Improper song number");
  if(v&~0xFFFF) errx(1,"Improper song number %d",(int)v);
  nextsongnum=v;
}

static void cmd_note_duration(const char*arg) {
  int32_t v;
  while(*arg) {
    v=-1;
    arg=parse_integer(arg,&v,0);
    if(v<0) break;
    if(v>0 && v<256) find_wait(v);
  }
}

static void cmd_omit_standard(const char*arg) {
  omit_standard=1;
}

static void cmd_scale(const char*arg) {
  int i;
  for(i=0;arg[i];i++) {
    if(arg[i]>='a' && arg[i]<='j') scale[arg[i]-'a']=i;
  }
  scaleoct=i;
}

static void cmd_song(const char*arg) {
  int32_t v=-1;
  finish_song();
  arg=parse_integer(arg,&v,1);
  if(*arg) errx(1,"Improper song number");
  if(v&~0xFFFF) errx(1,"Improper song number %d",(int)v);
  if(nsongs>v && songs[v].constructed) errx(1,"Duplicate song number %d",(int)v);
  nextsongnum=songnum=v;
}

static void cmd_temperament(const char*arg) {
  int32_t i;
  if(tuning_mode) errx(1,"Too many #TEMPERAMENT and/or #TUNING commands");
  tuning_mode=1;
  arg=parse_real(arg,tuning_data,1);
  arg=parse_integer(arg,&i,1);
  if(*arg || i<1 || i>64 || *tuning_data<=0.0) errx(1,"Improper #TEMPERAMENT");
  tuning_count=i;
}

static void cmd_tempo(const char*arg) {
  if(*parse_integer(arg,&tempo,1) || tempo<1 || tempo>0xFFFF) errx(1,"Improper tempo");
}

static void cmd_tempo_ratio(const char*arg) {
  uint32_t u,v;
  arg=parse_integer(arg,&tempo_n,1);
  arg=parse_integer(arg,&tempo_d,1);
  if(tempo_n<1 || tempo_d<1) errx(1,"Improper tempo ratio");
  u=tempo_n; v=tempo_d;
  while(u!=v) if(u>v) u-=v; else v-=u;
  tempo_n/=u; tempo_d/=u;
}

static void cmd_tuning(const char*arg) {
  double f;
  if(tuning_mode) errx(1,"Too many #TEMPERAMENT and/or #TUNING commands");
  tuning_mode=2;
  tuning_count=0;
  arg=parse_real(arg,tuning_data,1);
  if(*tuning_data<=0.0) errx(1,"Improper #TUNING");
  while(*arg) {
    if(tuning_count>60) errx(1,"Improper #TUNING");
    arg=parse_real(arg,tuning_data+ ++tuning_count,1);
    if(*arg=='/') {
      arg=parse_real(arg,&f,1);
      tuning_data[tuning_count]/=f;
    }
    if(tuning_data[tuning_count]<=0.0) errx(1,"Improper #TUNING");
  }
  if(!tuning_count) errx(1,"Improper #TUNING");
}

static const Command commands[]={
  {"CHDIR",cmd_chdir},
  {"DEUTSCH",cmd_german},
  {"DIVISIONS",cmd_divisions},
  {"ENGLISH",cmd_english},
  {"GERMAN",cmd_german},
  {"INCLUDE",cmd_include},
  {"NEXT",cmd_next},
  {"NOTE-DURATION",cmd_note_duration},
  {"OMIT-STANDARD",cmd_omit_standard},
  {"SCALE",cmd_scale},
  {"SONG",cmd_song},
  {"TEMPERAMENT",cmd_temperament},
  {"TEMPO",cmd_tempo},
  {"TEMPO-RATIO",cmd_tempo_ratio},
  {"TUNING",cmd_tuning},
};

static int find_commands(const void*x,const void*y) {
  const char*a=((Command*)x)->name;
  const char*b=((Command*)y)->name;
  while(*a==*b || (*a>='a' && *a+'A'-'a'==*b) || (*b>='a' && *b+'A'-'a'==*a)) a++,b++;
  return ((*a==' ' || *a=='\t')?0:*a)-((*b==' ' || *b=='\t')?0:*b);
}

static ASN1_Value*load_instrument(FILE*f,char*q) {
  ASN1_Value*v=calloc(1,sizeof(ASN1_Value));
  if(!v) err(1,"Allocation failed");
  while(*q==' ' || *q=='\t') q++;
  if(*q) {
    int i,j;
    uint8_t c=0;
    double a;
    int32_t n;
    ASN1_Encoder*e=asn1_start_encoding_constructed_value(v,ASN1_CONTEXT_SPECIFIC,1,0);
    FILE*g;
    if(!e) err(1,"Allocation failed");
    if(*q=='U' || *q=='u') c=0x80; else if(*q!='S' && *q!='s') errx(1,"Improper instrument definition");
    q++;
    if(*q=='8') {
      c^=0x81; q++;
    } else if(*q=='1') {
      if(q[1]!='6') errx(1,"Improper instrument definition");
      q+=2;
      if(*q=='B' || *q=='b') c^=4; else if(*q!='L' && *q!='l') errx(1,"Improper instrument definition");
      if(q[1]!='E' && q[1]!='e') errx(1,"Improper instrument definition");
      q+=2;
    } else {
      errx(1,"Improper instrument definition");
    }
    while(*q==' ' || *q=='\t') q++;
    q=parse_real(q,&a,1);
    if(a<=0.0 || fpclassify(a)!=FP_NORMAL) errx(1,"Improper frequency number for instrument definition");
    asn1_encode_double(e,a);
    asn1_implicit(e,ASN1_UNIVERSAL,ASN1_ENUMERATED);
    asn1_encode_int8(e,c&1?0:4);
    g=asn1_primitive_stream(e,ASN1_UNIVERSAL,ASN1_OCTET_STRING);
    if(!g) err(1,"Allocation failed");
    if(c&1) {
      // 8-bits
      while((i=fgetc(f))!=EOF) fputc(i^(c&0x80),g);
    } else {
      // 16-bits
      while((i=fgetc(f))!=EOF) {
        j=fgetc(f);
        fputc(c&4?j:i,g);
        fputc((c&4?i:j)^(c&0x80),g);
      }
    }
    asn1_end(e);
    n=-999;
    q=parse_integer(q,&n,0);
    if(n!=-999) {
      asn1_encode_integer(e,n);
      n=-999;
      q=parse_integer(q,&n,0);
      if(n!=-999) asn1_encode_integer(e,n);
    }
    if(*q) errx(1,"Syntax error");
    if(asn1_finish_encoder(e)) err(1,"Allocation failed");
  } else {
    if(asn1_read_item(f,v,0)) errx(1,"Error in instrument file");
  }
  return v;
}

static void define_envelope(char*p) {
  uint8_t f=0;
  uint16_t h=romsize;
  uint16_t s=h+4;
  int32_t v;
  double d;
  romsize+=4;
  rom[h]=2; rom[h+1]=rom[h+2]=rom[h+3]=0;
  switch(*p) {
    case 'A': rom[h+1]=0x41; break;
    case 'F': rom[h+1]=0x42; break;
    case 'I': rom[h+1]=0x80; break;
    case 'O': rom[h+1]=0x81; break;
    case 'R': rom[h+1]=0x43; break;
    default: error: errx(1,"Syntax error in envelope definition");
  }
  for(;;) {
    while(*p==' ' || *p=='\t') ++p;
    if(*p=='{' || *p=='}' || !*p) break;
    switch(*p++) {
      case '+': rom[h]|=1; break;
      case '!': rom[h]|=4; break;
      case '$': rom[h]|=8; break;
      default: goto error;
    }
  }
  if(*p++!=123) goto error;
  while(*p!=125) {
    while(*p==' ' || *p=='\t') ++p;
    switch(*p++) {
      case '0' ... '9': case '-': case '+': case '.': case '$':
        if(rom[h+1]&0x80) {
          p=parse_integer(p-1,&v,1);
          if((v<(rom[h]&1?-127:0)) || (v>(rom[h]&1?127:255))) goto error;
        } else {
          p=parse_real(p-1,&d,1);
          if((rom[h]&1) && (romsize!=h+4 || !(rom[h]&16))) {
            v=find_real(d);
            if(f&1) v|=128;
          } else {
            v=find_real(fabs(d));
            if(d<0.0 && v) v|=128;
          }
        }
        rom[romsize++]=v;
        f&=~1;
        if(romsize>=0xFFF0 || romsize==s+253) errx(1,"Envelope is too long");
        break;
      case '=':
        if(romsize!=h+4) goto error;
        rom[h]|=16;
        break;
      case '*':
        if((romsize==h+4 && (rom[h]&16)) || !(rom[h]&1) || (rom[h+1]&0x80)) goto error;
        f|=1;
        break;
      case '|':
        if(f&5) goto error;
        if(f&2) {
          if(romsize==s) errx(1,"Improper loop point in envelope");
          rom[h+3]=romsize-s-1;
          f|=4;
        } else {
          rom[h+2]=romsize-s;
          f|=2;
        }
        break;
      default: goto error;
    }
  }
  if(romsize==s) goto bad;
  switch(f) {
    case 0: // normal
      rom[h+2]=255;
      rom[h+3]=romsize-s;
      break;
    case 2: // loop
      rom[h+3]=romsize-s;
      break;
    case 6: // loop incl. end mark
      rom[h]|=32;
      for(v=s;v<romsize-1;v++) if(rom[v]==0 || rom[v]==128) errx(1,"Value 0 and 128 cannot be used in a envelope with two loop marks");
      if(rom[romsize-1]&127) rom[romsize++]=rom[h+1]&0x80&~(rom[h]<<7);
      break;
    default: bad: errx(1,"Improper envelope definition");
  }
}

static void process_line(char*line) {
  FILE*f;
  int32_t m,n;
  Command key={line+1};
  Command*cmd;
  char*p=line+strlen(line);
  char*q;
  while(p>line && (p[-1]=='\r' || p[-1]=='\n' || p[-1]==' ' || p[-1]=='\t')) *--p=0;
  switch(*line) {
    case 'A' ... 'Z':
      p=strchr(line,' ');
      if(!p) goto syntax;
      while(*line>='A' && *line<='Z') {
        if(!track[*line-'A'].file && !(track[*line-'A'].file=open_memstream(&track[*line-'A'].text,&track[*line-'A'].text_size))) err(1,"Allocation failed");
        append_track_text(*line-'A',p+1);
        line++;
      }
      if(*line!=' ') errx(1,"Syntax error");
      break;
    case '*':
      if(!line[1] || (line[1]&~0x7F)) goto syntax;
      free(textmacro[line[1]]);
      textmacro[line[1]]=strdup(line+2);
      if(!textmacro[line[1]]) err(1,"Allocation failed");
      break;
    case '@':
      if(line[1]=='v' || line[1]=='V' || line[1]=='u' || line[1]=='U') {
        p=parse_integer(line+2,&n,1);
        if(n<1 || n>255) errx(1,"Improper envelope number: %d",(int)n);
        if(envelope[n-1]) errx(1,"Envelope %d is already defined",(int)n);
        if(romsize>=0xFFF0) errx(1,"Out of memory for envelopes");
        envelope[n-1]=romsize+1;
        while(*p==' ' || *p=='\t') ++p;
        if(*p++!='=') goto syntax;
        while(*p==' ' || *p=='\t') ++p;
        define_envelope(p);
      } else {
        p=parse_integer(line+1,&n,1);
        if(n<1 || n>255) errx(1,"Improper instrument number: %d",(int)n);
        if(instrum[n-1]) errx(1,"Instrument %d is already defined",(int)n);
        if(ninstrum<n) ninstrum=n;
        while(*p==' ' || *p=='\t') ++p;
        if(*p++!='=') goto syntax;
        while(*p==' ' || *p=='\t') ++p;
        if(*p=='@') {
          p=parse_integer(p+1,&m,1);
          if(m<1 || m>=n) errx(1,"Improper instrument number to be copied: %d",(int)m);
          instrum[n-1]=malloc(sizeof(ASN1_Value));
          q=malloc(4);
          if(!instrum[n-1] || !q) err(1,"Allocation failed");
          *(instrum[n-1])=(ASN1_Value){.class=ASN1_CONTEXT_SPECIFIC,.type=3,.data=q,.length=(m&128?4:3),.constructed=1};
          q[0]=ASN1_INTEGER; q[1]=(m&128?2:1); q[2]=(m&128?0:m); q[3]=m;
        } else {
          if(*p++!='"') goto syntax;
          q=p;
          while(*q && *q!='"') q++;
          if(!*q) goto syntax;
          *q++=0;
          f=fopen(p,"r");
          if(!f) err(1,"Cannot open instrument file \"%s\"",p);
          instrum[n-1]=load_instrument(f,q);
          fclose(f);
        }
      }
      break;
    case '#':
      cmd=bsearch(&key,commands,sizeof(commands)/sizeof(*commands),sizeof(*commands),find_commands);
      if(!cmd) goto syntax;
      p=line+1;
      while(*p && *p!=' ' && *p!='\t') p++;
      while(*p==' ' || *p=='\t') p++;
      cmd->call(p);
      break;
    case 0: case ';': return;
    default: syntax: errx(1,"Syntax error in \"%s\"",line);
  }
}

static void send_standard(void) {
  int i,w;
  labels['!'*128+'N']=romsize;
  add_op2(0xC9,nreals+16-lonote);
  add_op(0xF5);
  add_op(0x91);
  add_op(0xE0);
  w=romsize;
  for(i=0;i<nwaits;i++) add_op(waits[i]);
  labels['!'*128+'L']=romsize;
  add_numb(w-0x60);
  add_op(0x8E);
  add_op(0x99);
  add_op(0xF0);
}

int main(int argc,char**argv) {
  int i;
  enc=asn1_start_encoding_file(stdout);
  if(!enc) errx(1,"Allocation failed");
  lrefs_file=open_memstream((char**)&lrefs_data,&lrefs_size);
  if(!lrefs_file) err(1,"Allocation failed");
  while(getline(&line_mem,&line_size,stdin)>0) process_line(line_mem);
  finish_song();
  if(!omit_standard) send_standard();
  fclose(lrefs_file);
  if(lrefs_size && !lrefs_data) err(1,"Allocation failed");
  lrefs_size/=sizeof(Lref);
  for(i=0;i<lrefs_size;i++) {
    rom[lrefs_data[i].at]=labels[lrefs_data[i].name];
    rom[lrefs_data[i].at+1]=labels[lrefs_data[i].name]>>8;
  }
  asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    // Song list
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      for(i=0;i<nsongs;i++) asn1_encode(enc,songs+i);
    asn1_end(enc);
    // Tempo ratio
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_RATIONAL,0);
      asn1_encode_integer(enc,tempo_n);
      asn1_encode_integer(enc,tempo_d);
    asn1_end(enc);
    // Instruments
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      for(i=0;i<ninstrum;i++) if(instrum[i]) asn1_encode(enc,instrum[i]); else asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_NULL,"",0);
    asn1_end(enc);
    // Channels
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      for(i=0;i<nchannels;i++) {
        asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,0,0);
        asn1_end(enc);
      }
    asn1_end(enc);
    // Short call addresses
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      asn1_encode_integer(enc,labels['!'*128+'N']);
      asn1_encode_integer(enc,labels['!'*128+'N']);
      asn1_encode_integer(enc,labels['!'*128+'N']);
      asn1_encode_integer(enc,labels['!'*128+'L']);
    asn1_end(enc);
    // Real constants
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      for(i=0;i<nreals;i++) asn1_encode_double(enc,reals[i]);
      if(hinote>lonote) {
        if(nreals+hinote-lonote>110) errx(1,"Too many real constants");
        if(tuning_mode==2) {
          for(i=lonote;i<=hinote;i++) asn1_encode_double(enc,pow(*tuning_data,i/tuning_count)*tuning_data[i%tuning_count+1]);
        } else {
          asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,10,0);
            asn1_encode_integer(enc,hinote+1-lonote);
            asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,8,0);
              asn1_encode_double(enc,*tuning_data);
              asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,4,0);
                if(lonote) asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,1,0);
                  if(lonote) asn1_encode_double(enc,lonote);
                  asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,9,0);
                  asn1_end(enc);
                if(lonote) asn1_end(enc);
                asn1_encode_double(enc,tuning_count);
              asn1_end(enc);
            asn1_end(enc);
          asn1_end(enc);
        }
      } else if(hinote==lonote) {
        if(tuning_mode==2) {
          asn1_encode_double(enc,pow(*tuning_data,lonote/tuning_count)*tuning_data[lonote%tuning_count+1]);
        } else {
          asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,8,0);
            asn1_encode_double(enc,*tuning_data);
            asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,4,0);
              asn1_encode_double(enc,lonote);
              asn1_encode_double(enc,tuning_count);
            asn1_end(enc);
          asn1_end(enc);
        }
      }
    asn1_end(enc);
    // Program
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,rom,romsize);
  asn1_end(enc);
  asn1_finish_encoder(enc);
  return 0;
}
