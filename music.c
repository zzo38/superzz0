#if 0
gcc -s -O2 -o ~/bin/sz0music -std=gnu99 -fwrapv -Wno-multichar -Wno-unused-result music.c asn1.o -lm
exit
#endif

/*
  Uses of registers per track:
    B = note duration
*/

#define _GNU_SOURCE
#include "asn1.h"
#include <err.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
static int32_t tempo=120;
static int32_t tempo_n=1;
static int32_t tempo_d=60;
static uint8_t tuning_mode=0; // 0=default 1=Temperament 2=Tuning
static uint8_t tuning_count=12;
static double tuning_data[64];
static uint8_t omit_standard=0;
static ASN1_Value*songs;
static uint32_t nsongs;
static uint16_t labels[0x4000];
static FILE*lrefs_file;
static Lref*lrefs_data;
static size_t lrefs_size;

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

static uint16_t do_track(char*t) {
  uint16_t inlen=0;
  uint16_t outlen=0;
  uint16_t start=romsize;
  uint8_t transpose=0;
  uint8_t octave=0;
  uint16_t loopstart=0xFFFF;
  int i,j;
  for(;;) {
    while(*t && *t==' ') t++;
    switch(*t++) {
      case 'a' ... 'j':
        i=scale[t[-1]-'a'];
        while((*t=='+' && (i++,1)) || (*t=='-' && (i--,1)) || (*t=='\'' && (i+=tuning_count,1))) t++;
        
        break;
      case 'k':
        
        break;
      case 'l':
        
        break;
      case 'n':
        
        break;
      case 'o':
        
        break;
      case 'r':
        
        break;
      case 'v':
        
        break;
      case 'w':
        
        break;
      case '^':
        
        break;
    }
  }
  if(loopstart==0xFFFF) add_op2(0xDE,0x00); else add_op2w(0xA8,loopstart);
  return start;
}

static void finish_song(void) {
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
    if(songnum>=nsongs) {
      songs=realloc(songs,(songnum+1)*sizeof(ASN1_Value));
      if(!songs) err(1,"Allocation failed");
      while(songnum>=nsongs) songs[nsongs++]=(ASN1_Value){.type=ASN1_NULL};
    }
    if(songs[songnum].type!=ASN1_NULL) errx(1,"Duplicate song number");
    for(i=0;i<m;i++) track[i].addr=do_track(track[i].text);
    e=asn1_start_encoding_constructed_value(songs+songnum,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    if(!e) err(1,"Allocation failed");
      asn1_encode_number(e,tempo);
      if(nextsongnum!=songnum) asn1_encode_number(e,nextsongnum);
      for(i=0;i<m;i++) if(track[i].text) {
        asn1_construct(e,ASN1_CONTEXT_SPECIFIC,0,0);
          asn1_encode_number(e,track[i].addr);
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
  {"OMIT-STANDARD",cmd_omit_standard},
  {"SCALE",cmd_scale},
  {"SONG",cmd_song},
  {"TEMPERAMENT",cmd_temperament},
  {"TEMPO",cmd_tempo},
  {"TEMPO-RATIO",cmd_tempo_ratio},
  {"TUNING",cmd_tuning},
};

static int find_commands(const void*x,const void*y) {
  const char*a=(*(Command*)x)->name;
  const char*b=(*(Command*)y)->name;
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
    ASN1_Encoder*e=asn1_start_encoding_constructed_value(&v,ASN1_CONTEXT_SPECIFIC,1,0);
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
    asn1_encode_int8(e,c&2?0:4);
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
      asn1_encode_number(e,n);
      n=-999;
      q=parse_integer(q,&n,0);
      if(n!=-999) asn1_encode_number(e,n);
    }
    if(*q) errx(1,"Syntax error");
    if(asn1_finish_encoder(e)) err(1,"Allocation failed");
  } else {
    if(asn1_read_item(&v,f,0)) errx(1,"Error in instrument file");
  }
  return v;
}

static void process_line(char*line) {
  FILE*f;
  int32_t n;
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
      p=parse_number(line+1,&n,1);
      if(n<1 || n>255) errx(1,"Improper instrument number: %d",(int)n);
      if(instrum[n-1]) errx(1,"Instrument %d is already defined",(int)n);
      while(*p==' ' || *p=='\t') ++p;
      if(*p++!='=') goto syntax;
      while(*p==' ' || *p=='\t') ++p;
      if(*p++!='"') goto syntax;
      q=p;
      while(*q && *q!='"') q++;
      if(!*q) goto syntax;
      *q++=0;
      f=fopen(p,"r");
      if(!f) err(1,"Cannot open instrument file \"%s\"",p);
      instrum[n-1]=load_instrument(f,q);
      fclose(f);
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

int main(int argc,char**argv) {
  int i;
  enc=asn1_start_encoding_file(stdout);
  if(!enc) errx(1,"Allocation failed");
  lrefs_file=open_memstream((char**)&lrefs_data,&lrefs_size);
  if(!lrefs_file) err(1,"Allocation failed");
  while(getline(&line_mem,&line_size,stdin)>0) process_line(line_mem);
  finish_song();
  fclose(lrefs_file);
  if(!lrefs_data) err(1,"Allocation failed");
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
      asn1_encode_number(enc,tempo_n);
      asn1_encode_number(enc,tempo_d);
    asn1_end();
    // Instruments
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      
    asn1_end(enc);
    // Channels
    
    // Short call addresses
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      asn1_encode_number(enc,labels['!'*128+'N']);
      asn1_encode_number(enc,labels['!'*128+'N']);
      asn1_encode_number(enc,labels['!'*128+'N']);
      asn1_encode_number(enc,labels['!'*128+'L']);
    asn1_end(enc);
    // Real constants
    
    // Program
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,rom,romsize);
  asn1_end(enc);
  asn1_finish_encoder(enc);
  return 0;
}
