#if 0
(sed -rn 's/^\[(...) (.*)]$/{"\2",0x\1},/p' < opcodes.doc; sed -rn 's/\/\/PSEUDO!(.*)$/{\1},/p' < asm.c) | sort > opcodes.inc
gcc -s -O2 -o ~/bin/sz0asm asm.c
exit
#endif

#define _GNU_SOURCE
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint8_t Uint8;
typedef int8_t Sint8;

typedef struct {
  const char*name;
  Uint16 op;
} Opcodes;

typedef struct {
  const char*name;
  Sint32 value;
  char kind;
} Name;

static const Opcodes opcodes[]={
#include "opcodes.inc"
};
//PSEUDO!"AT",0x8000
//PSEUDO!"COM",0x8001
//PSEUDO!"DATA",0x8002
//PSEUDO!"EV",0x8003
//PSEUDO!"FILL",0x8004
//PSEUDO!"IS",0x8005
//PSEUDO!"ISNT",0x8006
//PSEUDO!"PT",0x8007
//PSEUDO!"TA",0x8008
//PSEUDO!"????",0x8009

static char pass;
static Uint16 addr;
static Uint32 addr_end=256;
static Uint16 mem[0x10000];
static Uint16 events[0x1000];
static Name*names;
static int nnames;
static Sint32*hlabel[10];
static Uint16 nhlabel[10];
static Uint16 mhlabel[10];
static Sint8 chlabel;
static FILE*flabel[10];
static size_t flabels[10];
static FILE*infile;
static char*outname;
static FILE*outfile;
static Uint16 option;
static char*line;
static size_t linesize;
static int linenum;
static char*linept;
static const char**strings;
static int nstrings;
static Uint32 tstrings;
static char strbuf[81];
static Sint32*wlabel;
static Sint32 wflabel;
static int atcount;

static Name*add_name(const char*name,Sint32 value,char kind) {
  name=strdup(name);
  if(!name) err(1,"Allocation failed");
  names=realloc(names,++nnames*sizeof(Name));
  if(!names) err(1,"Allocation failed");
  names[nnames-1].name=name;
  names[nnames-1].value=value;
  names[nnames-1].kind=kind;
  return names+nnames-1;
}

static int find_string(const char*t) {
  int i;
  for(i=0;i<nstrings;i++) if(!strcmp(t,strings[i])) return i;
  t=strdup(t);
  if(!t) err(1,"Allocation failed");
  strings=realloc(strings,++nstrings*sizeof(char*));
  if(!strings) err(1,"Allocation failed");
  strings[nstrings-1]=t;
  tstrings+=strlen(t)+1;
  return nstrings-1;
}

static int compare_op(const void*a,const void*b) {
  const Opcodes*x=a;
  const Opcodes*y=b;
  return strcmp(x->name,y->name);
}

static Uint16 find_op(const char*op) {
  char k[5]="****";
  Opcodes key={k,0};
  Opcodes*z;
  strncpy(k,op,4);
  *strchrnul(k,' ')=0;
  z=bsearch(&key,opcodes,sizeof(opcodes)/sizeof(Opcodes),sizeof(Opcodes),compare_op);
  if(!z) errx(1,"Unknown instruction '%s' on line %d",k,linenum);
  return z->op;
}

static Name*find_name(const char*n,int e) {
  int i;
  for(i=0;i<nnames;i++) if(!strcmp(n,names[i].name)) return names+i;
  if(e || pass) errx(1,"Undefined name '%s' on line %d",n,linenum);
  return 0;
}

static void read_name(void) {
  int n=strspn(linept,"0123456789_?ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  if(n>80) n=80;
  snprintf(strbuf,81,"%.*s",n,linept);
  linept+=n;
}

static void read_string(void) {
  char t=*linept++;
  char*s=linept;
  int n=0;
  int i;
  while(*s && *s!=t) {
    if(n>=80) errx(1,"Too long string on line %d",linenum);
    if(*s!='\\') {
      strbuf[n++]=*s++;
    } else {
      s++;
      if(*s=='\\' || *s=='\'' || *s=='"') {
        strbuf[n++]=*s++;
      } else if(*s=='r') {
        s++;
        strbuf[n++]='\r';
      } else if(*s=='n') {
        s++;
        strbuf[n++]='\n';
      } else if(*s=='x') {
        s++;
        if(*s>='0' && *s<='9') i=*s-'0';
        else if(*s>='A' && *s<='F') i=*s+10-'A';
        else errx(1,"Improper escape in string on line %d",linenum);
        s++;
        i<<=4;
        if(*s>='0' && *s<='9') i+=*s-'0';
        else if(*s>='A' && *s<='F') i+=*s+10-'A';
        else errx(1,"Improper escape in string on line %d",linenum);
        s++;
        if(!i) errx(1,"Improper escape in string on line %d",linenum);
        strbuf[n++]=i;
      } else {
        errx(1,"Improper escape in string on line %d",linenum);
      }
    }
  }
  if(!*s) errx(1,"Unterminated string on line %d",linenum);
  strbuf[n]=0;
  linept=s+1;
}

static inline void parse_comma(void) {
  if(*linept++!=',') errx(1,"Expected comma on line %d",linenum);
}

static Sint32 parse_numeric(char e) {
  Sint32 total=0;
  Uint32 num=0;
  Sint8 sgn=1;
  int i;
  Name*name;
  again:
  while(*linept==' ' || *linept=='\t') ++linept;
  if(*linept=='+') {
    sgn=1;
    ++linept;
  } else if(*linept=='-') {
    sgn=-1;
    ++linept;
  }
  switch(*linept) {
    case '0' ... '9':
      if(linept[1]=='B' || linept[1]=='F') {
        if(e && linept[1]=='F') errx(1,"Improper forward reference on line %d",linenum);
        i=*linept-'0';
        if(linept[1]=='B' && !pass && flabel[i]) fflush(flabel[i]);
        if(linept[1]=='B' || pass) {
          if(linept[1]=='B' && !nhlabel[i]) errx(1,"Improper backward reference on line %d",linenum);
          if(!hlabel[i]) errx(1,"Undefined name '%c%c' on line %d",linept[0],linept[1],linenum);
          if(linept[1]=='F' && pass && nhlabel[i]>=mhlabel[i]-(chlabel==i?1:0)) errx(1,"Undefined name '%c%c' on line %d",linept[0],linept[1],linenum);
          num=hlabel[i][nhlabel[i]+(linept[1]=='F'?(chlabel==i?2:1):0)];
        }
        linept+=2;
      } else {
        while(*linept) {
          if(*linept>='0' && *linept<='9') num=10*num+*linept++-'0';
          else break;
        }
      }
      break;
    case '$':
      linept++;
      while(*linept) {
        if(*linept>='0' && *linept<='9') num=16*num+*linept++-'0';
        else if(*linept>='A' && *linept<='F') num=16*num+*linept++-'A'+10;
        else break;
      }
      break;
    case '@':
      ++linept;
      num=addr;
      atcount+=sgn;
      break;
    case '\'':
      read_string();
      if(!strbuf || strbuf[1]) errx(1,"Improper character literal on line %d",linenum);
      num=0xFF&*strbuf;
      break;
    case '"':
      read_string();
      num=find_string(strbuf);
      break;
    case 'A' ... 'Z': case '_':
      read_name();
      name=find_name(strbuf,e);
      if(name && name->kind) errx(1,"Reserved name on line %d",linenum);
      if(name) num=name->value;
      break;
    default:
      errx(1,"Expected numeric expression on line %d",linenum);
  }
  total+=sgn*num;
  num=0;
  if(*linept=='+' || *linept=='-') goto again;
  return total;
}

static int parse_reg16(void) {
  if(*linept>='A' && *linept<='H') return *linept++-'A';
  if(*linept>='S' && *linept<='Z') return *linept++-'S'+8;
  errx(1,"Improper register code on line %d",linenum);
}

static inline void put_data(Uint16 d) {
  mem[addr++]=d;
  if(d && addr>addr_end) addr_end=addr;
}

static void do_pass(void) {
  char*s;
  Name*name;
  Uint16 op;
  Sint32 u,v;
  int i,j;
  rewind(infile);
  linenum=0;
  addr=256;
  while(getline(&line,&linesize,infile)>0) {
    ++linenum;
    if(s=strchr(line,'\r')) *s=0; else if(s=strchr(line,'\n')) *s=0;
    if(*line==';' || !*line) continue;
    if(*line!=' ' && *line!='\t') {
      if(*line>='0' && *line<='9' && line[1]=='H') {
        chlabel=*line-'0';
        wlabel=&wflabel;
      } else {
        chlabel=-1;
        linept=line;
        read_name();
        if(*strbuf && !strbuf[1]) errx(1,"Reserved label name '%s' on line %d",strbuf,linenum);
        name=find_name(strbuf,0);
        if(name && name->kind) errx(1,"Reserved label name '%s' on line %d",strbuf,linenum);
        if(name && !pass) errx(1,"Duplicate definition of name '%s' on line %d",strbuf,linenum);
        if(!name) name=add_name(strbuf,addr,0);
        wlabel=&name->value;
      }
      if(wlabel) *wlabel=addr;
    } else {
      wlabel=0;
    }
    s=line+strcspn(line," \t");
    s+=strspn(s," \t");
    if(!*s || *s==';') continue;
    op=find_op(s);
    linept=strchr(s,' ');
    if(!linept) errx(1,"Missing operand on line %d",linenum);
    ++linept;
    if(op<0x8000) {
      // Normal
      
    } else {
      // Pseudo-op
      switch(op&255) {
        case 0: // AT
          addr=parse_numeric(1);
          break;
        case 1: // COM
          // not implemented yet
          break;
        case 2: // DATA
          do put_data(parse_numeric(0)); while(*linept==',' && ++linept);
          break;
        case 3: // EV
          if(*linept>='A' && *linept<='Z' && linept[1]==',') {
            i=parse_reg16();
          } else {
            i=parse_numeric(0);
            if(i&~15) errx(1,"Improper event number on line %d",linenum);
          }
          parse_comma();
          u=parse_numeric(0)&255;
          if(*linept==',') ++linept,v=parse_numeric(0); else v=addr;
          events[u|(i<<8)]=v;
          break;
        case 4: // FILL
          u=parse_numeric(1)+addr;
          if(u<addr || u>=0x10000) errx(1,"Too big fill size on line %d",linenum);
          atcount=0;
          if(*linept==',') {
            s=++linept;
            v=parse_numeric(0);
          } else {
            v=0;
          }
          while(addr<u) {
            mem[addr++]=v;
            v+=atcount;
          }
          if((v || atcount) && addr>addr_end) addr_end=addr;
          break;
        case 5: // IS
          if(wlabel) *wlabel=parse_numeric(1);
          break;
        case 6: // ISNT
          if(wlabel) *wlabel=~parse_numeric(1);
          break;
        case 7: // PT
          if(*linept!='"') errx(1,"String expected on line %d",linenum);
          read_string();
          for(i=0;strbuf[i];i+=2) {
            put_data((strbuf[i]&255)|((strbuf[i+1]&255)<<8));
            if(!strbuf[i+1]) break;
          }
          if(!strbuf[i]) put_data(0);
          break;
        case 8: // TA
          v=parse_numeric(1);
          if(v&~0xFFFF) errx(1,"Improper address on line %d",linenum);
          mem[v]=addr;
          if(addr && v>addr_end) addr_end=v;
          break;
        case 9: // ????
          fprintf(stderr,"%lX\n",(long)parse_numeric(0));
          break;
      }
    }
    while(*linept==' ' || *linept=='\t') ++linept;
    if(*linept && *linept!=';') errx(1,"Extra text on line %d",linenum);
    if(chlabel!=-1 && !pass) {
      if(!flabel[chlabel]) {
        flabel[chlabel]=open_memstream((char**)(hlabel+chlabel),flabels+chlabel);
        if(!flabel[chlabel]) err(1,"Error with open_memstream");
      }
      fwrite(&wflabel,1,sizeof(Sint32),flabel[chlabel]);
      ++nhlabel[chlabel];
      ++mhlabel[chlabel];
    }
  }
}

static void begin_lump(const char*name,Uint32 len) {
  while(*name) fputc(*name++,outfile);
  fputc(0,outfile);
  fputc(len>>16,outfile);
  fputc(len>>24,outfile);
  fputc(len>>0,outfile);
  fputc(len>>8,outfile);
}

static void do_output(void) {
  int i;
  outfile=fopen(outname,"w");
  if(!outfile) err(1,"Cannot open output file");
  if(addr_end<256 || addr_end>0x10000) errx(1,"Output size exceeds maximum");
  begin_lump("MEMORY",addr_end<<1);
  for(i=0;i<addr_end;i++) {
    fputc(mem[i],outfile);
    fputc(mem[i]>>8,outfile);
  }
  if(nstrings>1) {
    begin_lump("TEXT",tstrings-1);
    for(i=1;i<nstrings;i++) fwrite(strings[i],1,strlen(strings[i])+1,outfile);
  }
  begin_lump("EVENT",0x2000);
  for(i=0;i<0x1000;i++) {
    fputc(events[i],outfile);
    fputc(events[i]>>8,outfile);
  }
  if(option&0x0001) {
    char*oo=0;
    size_t os=0;
    FILE*fp=open_memstream(&oo,&os);
    if(!fp) err(1,"Allocation failed");
    for(i=0;i<nnames;i++) if(!names[i].kind) fprintf(fp,"%s\tIS $%lX\n",names[i].name,(long)names[i].value);
    fclose(fp);
    if(!oo) err(1,"Allocation failed");
    begin_lump("DEBUG",os);
    fwrite(oo,1,os,outfile);
    free(oo);
  }
  fclose(outfile);
}

int main(int argc,char**argv) {
  int i;
  while((i=getopt(argc,argv,"+d"))>0) switch(i) {
    case 'd': option|=0x0001; break;
    default: return 1;
  }
  if(optind+2!=argc) errx(1,"Wrong number of arguments");
  add_name("BW",0x0D41,1);
  add_name("BH",0x0D51,1);
  add_name("PX",0x0D62,1);
  add_name("PY",0x0D72,1);
  add_name("BRD",0x0D80,1);
  add_name("SCR",0x0D90,1);
  find_string("");
  infile=fopen(argv[optind],"r");
  if(!infile) err(1,"Cannot open input file");
  outname=argv[optind+1];
  do_pass();
  for(i=0;i<10;i++) if(flabel[i]) {
    fclose(flabel[i]);
    if(!hlabel[i]) err(1,"Allocation failed");
    nhlabel[i]=0;
  }
  pass=1;
  do_pass();
  fclose(infile);
  do_output();
  return 0;
}

