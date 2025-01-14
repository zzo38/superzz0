#if 0
(sed -rn 's/^\[(...) (.*)]$/{"\2",0x\1},/p' < opcodes.doc; sed -rn 's/\/\/PSEUDO!(.*)$/{\1},/p' < asm.c) | sort > opcodes.inc
gcc -s -O2 -o ~/bin/sz0asm -fwrapv -Wno-multichar -Wno-unused-result asm.c
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
  Uint32 line;
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
//PSEUDO!"ASS",0x800A
//PSEUDO!"ED",0x802F
//PSEUDO!"ED0",0x8030
//PSEUDO!"ED1",0x8031
//PSEUDO!"ED2",0x8032
//PSEUDO!"ED3",0x8033
//PSEUDO!"ED4",0x8034
//PSEUDO!"ED5",0x8035
//PSEUDO!"ED6",0x8036
//PSEUDO!"ED7",0x8037

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
static int has_unknown;
static Uint16 ed_addr;
static Uint16 ed_addr_end=0x280;
static const char**ed_strings;
static int ed_nstrings;
static Uint32 ed_tstrings;
static Uint16 ed_mem[0x10000];

static Name*add_name(const char*name,Sint32 value,char kind) {
  name=strdup(name);
  if(!name) err(1,"Allocation failed");
  names=realloc(names,++nnames*sizeof(Name));
  if(!names) err(1,"Allocation failed");
  names[nnames-1].name=name;
  names[nnames-1].value=value;
  names[nnames-1].line=linenum;
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

static void exchange_strings(void) {
  const char**a=strings;
  int b=nstrings;
  Uint32 c=tstrings;
  strings=ed_strings;
  nstrings=ed_nstrings;
  tstrings=ed_tstrings;
  ed_strings=a;
  ed_nstrings=b;
  ed_tstrings=c;
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

static int parse_reg16(void) {
  if(*linept>='A' && *linept<='H') return *linept++-'A';
  if(*linept>='S' && *linept<='Z') return *linept++-'S'+8;
  errx(1,"Improper register code on line %d",linenum);
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
        if(linept[1]=='F') {
          if(e) errx(1,"Improper forward reference on line %d",linenum);
          has_unknown=1;
        }
        i=*linept-'0';
        if(linept[1]=='B' && !pass && flabel[i]) fflush(flabel[i]);
        if(linept[1]=='B' || pass) {
          if(linept[1]=='B' && !nhlabel[i]) errx(1,"Improper backward reference on line %d",linenum);
          if(!hlabel[i]) errx(1,"Undefined name '%c%c' on line %d",linept[0],linept[1],linenum);
          if(linept[1]=='F' && pass && nhlabel[i]>=mhlabel[i]-(chlabel==i?1:0)) errx(1,"Undefined name '%c%c' on line %d",linept[0],linept[1],linenum);
          num=hlabel[i][nhlabel[i]+(linept[1]=='F'?(chlabel==i?1:0):-1)];
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
      if(strbuf[0] && strbuf[1]) errx(1,"Improper character literal on line %d",linenum);
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
      if(!name || name->line>linenum) has_unknown=1;
      break;
    case '#':
      linept++;
      if(*linept<'A' || *linept>'Z') goto again;
      num=parse_reg16();
      break;
    default:
      errx(1,"Expected numeric expression on line %d",linenum);
  }
  total+=sgn*num;
  num=0;
  if(*linept=='+' || *linept=='-') goto again;
  return total;
}

static inline void put_data(Uint16 d) {
  mem[addr++]=d;
  if(d && addr>addr_end) addr_end=addr;
}

static inline void ed_put_data(Uint16 d) {
  ed_mem[ed_addr++]=d;
  if(d && ed_addr>ed_addr_end) ed_addr_end=ed_addr;
}

static void do_pass(void) {
  char*s;
  Name*name;
  Uint16 op;
  Sint32 u,v;
  int i,j;
  if(option&0x0008) fprintf(stderr,"Pass %d\n",pass);
  rewind(infile);
  linenum=0;
  addr=256;
  ed_addr=0x280;
  while(getline(&line,&linesize,infile)>0) {
    ++linenum;
    if(s=strchr(line,'\r')) *s=0; else if(s=strchr(line,'\n')) *s=0;
    if(*line==';' || !*line) continue;
    chlabel=-1;
    if(*line!=' ' && *line!='\t') {
      if(*line>='0' && *line<='9' && line[1]=='H') {
        chlabel=*line-'0';
        wlabel=&wflabel;
      } else {
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
      i=parse_reg16();
      parse_comma();
      if(i&8) {
        if(op&0x100) i&=7,op|=0x80; else errx(1,"Invalid register on line %d",linenum);
      }
      has_unknown=0;
      if((!linept[1] || linept[1]==';' || linept[1]==' ' || linept[1]=='\t') && *linept>='A' && *linept<='Z') {
        j=parse_reg16();
        if(j>=8 && j<12) errx(1,"Invalid register on line %d",linenum);
        put_data(op|((j^8)<<12)|(i<<9));
      } else if(*linept=='%') {
        op|=i<<9;
        ++linept;
        read_name();
        parse_comma();
        if(!strbuf[0]) strbuf[1]=0;
        if(!strbuf[1]) strbuf[2]=0;
        if(strbuf[2] && strbuf[3]) errx(1,"Invalid extended operand on line %d",linenum);
        j=(strbuf[0]?:'-')*'\1\0\0'+(strbuf[1]?:'-')*'\0\1\0'+(strbuf[2]?:'-')*'\0\0\1';
        i=0;
        if(*linept==',') {
          u=0;
        } else if(*linept=='*') {
          ++linept;
          i=0x3000;
          u=parse_numeric(0);
        } else if(*linept>='A' && *linept<='Z' && linept[1]==',') {
          i=parse_reg16();
          if(i>=8 && i<12) errx(1,"Invalid register on line %d",linenum);
          i=(i^8)<<12;
          u=0;
        } else {
          u=parse_numeric(0);
          if(has_unknown || (u&~1)) i|=0x2000;
        }
        parse_comma();
        if(j=='X--' || j=='Y--') {
          if(*linept=='+') v=0; else if(*linept=='-') v=8; else errx(1,"Invalid extended operand on line %d",linenum);
          linept++;
          v|=parse_reg16()<<4;
          parse_comma();
          v|=parse_numeric(1)&7;
        } else if(j=='OF-' || j=='OT-') {
          if(*linept>='A' && *linept<='H' && (linept[1]<=32 || linept[1]==';')) {
            v=parse_reg16()+8;
          } else {
            v=parse_numeric(0)+3;
            if(v&~7) errx(1,"Operand out of range on line %d",linenum);
          }
        } else {
          v=parse_numeric(0);
        }
        switch(j) {
          case 'I--':
            i|=0x0200;
            // fall through
          case '---':
            if(v<-256 || v>255) errx(1,"Operand out of range on line %d",linenum);
            if(v<0) v+=256,i|=0x0100;
            break;
          case 'L--':
            if(v&~31) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0400;
            break;
          case 'UR-':
            if(v&~31) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0500;
            break;
          case 'SR-':
            if(v&~31) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0600;
            break;
          case 'B--':
            if(v&~255) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0700;
            break;
          case 'R--':
            if(v&~255) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0C00;
            break;
          case 'M--':
            if(v<-256 || v>255) errx(1,"Operand out of range on line %d",linenum);
            if(v<0) v+=256,i|=0x0100;
            i|=0x0800;
            break;
          case 'E--':
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0D10;
            break;
          case 'SP-':
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0D20;
            break;
          case 'SM-':
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0D30;
            break;
          case 'SX-':
            v+=8;
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0DA0;
            break;
          case 'SY-':
            v+=8;
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0DB0;
            break;
          case 'BRD':
            v=0;
            i|=0x0D80;
            break;
          case 'XP-':
            v+=8;
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0D60;
            break;
          case 'YP-':
            v+=8;
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0D70;
            break;
          case 'W--':
            v+=8;
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0D40;
            break;
          case 'H--':
            v+=8;
            if(v&~15) errx(1,"Operand out of range on line %d",linenum);
            i|=0x0D50;
            break;
          case 'X--':
            i|=0x0A00;
            break;
          case 'Y--':
            i|=0x0B00;
            break;
          case 'OT-':
            i|=0x0DC0;
            break;
          case 'OF-':
            i|=0x0DD0;
            break;
          default: errx(1,"Invalid extended operand on line %d",linenum);
        }
        put_data(op|(3<<12));
        put_data(i|(v&0xFF));
        if((i>>12)==2) put_data(u>>16);
        if((i>>12)==2 || (i>>12)==3) put_data(u);
      } else if(*linept=='*') {
        ++linept;
        v=parse_numeric(0);
        if(v&~255) errx(1,"Operand out of range on line %d",linenum);
        put_data(op|(3<<12)|(i<<9));
        put_data(v);
      } else {
        v=parse_numeric(0);
        if(has_unknown || (v&~1)) {
          if(!(v&~0xFFFF)) {
            put_data(op|(2<<12)|(i<<9));
            put_data(v);
          } else if(v>=-256 && v<0) {
            put_data(op|(3<<12)|(i<<9));
            put_data(0x0100|(v+256));
          } else if(v==-257) {
            put_data(op|(3<<12)|(i<<9));
            put_data(0x1900);
          } else if(v==0x10000) {
            put_data(op|(3<<12)|(i<<9));
            put_data(0x1410);
          } else if(has_unknown) {
            errx(1,"Operand out of range on line %d",linenum);
          } else {
            put_data(op|(3<<12)|(i<<9));
            put_data(0x2000);
            put_data(v>>16);
            put_data(v);
          }
        } else {
          put_data(op|(v<<12)|(i<<9));
        }
      }
    } else {
      // Pseudo-op
      if(op&0x20) {
        if(!(option&0x0004)) goto skip;
        exchange_strings();
        if(wlabel) *wlabel=ed_addr;
      }
      switch(op&255) {
        case 0: // AT
          addr=parse_numeric(1);
          break;
        case 1: // COM
          i=mem[0xE2];
          mem[0xE2]=addr;
          put_data(i);
          put_data(parse_numeric(1));
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
          if(*linept==',') {
            s=++linept;
            u=parse_numeric(0);
          } else {
            u=addr;
          }
          mem[v]=u;
          if(addr && v>addr_end) addr_end=v;
          break;
        case 9: // ????
          for(i=0;i<10;i++) fprintf(stderr,"[%d]%d/%d ",i,nhlabel[i],mhlabel[i]);
          fprintf(stderr,"{%d}\n",chlabel);
          break;
        case 10: // ASS
          if(!pass) goto skip;
          v=parse_numeric(0);
          if(*linept!=',' && !v) errx(1,"Assertion failed on line %d",linenum);
          while(*linept==',') {
            ++linept;
            u=parse_numeric(0);
            if(u<v) errx(1,"Assertion failed on line %d",linenum);
            v=u;
          }
          break;
        case 0x2F: // ED
          do ed_put_data(parse_numeric(0)); while(*linept==',' && ++linept);
          break;
        case 0x30: // ED0
        case 0x31: // ED1
          v=parse_numeric(1);
          if(op&1) v=(v&0x7F)+0x200; else v+=0x100;
          if(*linept==',') {
            s=++linept;
            u=parse_numeric(0);
          } else {
            u=ed_addr;
          }
          ed_mem[v]=u;
          if(ed_addr && v>ed_addr_end) ed_addr_end=v;
          break;
        case 0x32: // ED2
          v=0x80|parse_numeric(0)&0xFF;
          parse_comma();
          read_string();
          for(i=0;strbuf[i];i++) if(strbuf[i]=='~') {
            memmove(strbuf+i,strbuf+i+1,strlen(strbuf+i));
            v+=strbuf[i]<<8;
            break;
          }
          ed_put_data(v);
          ed_put_data(find_string(strbuf));
          if(*linept==',') {
            ++linept;
            do ed_put_data(parse_numeric(0)); while(*linept==',' && ++linept);
          }
          break;
        case 0x33: // ED3
          ed_mem[u=ed_addr++]=3;
          do {
            v=parse_numeric(0)&0xFF;
            parse_comma();
            v|=parse_numeric(0)<<8;
            ed_put_data(v);
          } while(*linept==',' && ++linept);
          ed_mem[u]+=(ed_addr-u-1)<<8;
          break;
        case 0x34: // ED4
          i=parse_reg16();
          if(i>=8) errx(1,"Invalid register on line %d",linenum);
          parse_comma();
          ed_put_data((i<<8)+4);
          ed_put_data(parse_numeric(0));
        case 0x35: // ED5
          i=parse_reg16();
          if(i>=8) errx(1,"Invalid register on line %d",linenum);
          parse_comma();
          v=parse_numeric(1);
          parse_comma();
          ed_put_data((i<<8)+(v<<12)+5);
          ed_put_data(parse_numeric(0));
      }
      if(op&0x20) exchange_strings();
    }
    while(*linept==' ' || *linept=='\t') ++linept;
    if(*linept && *linept!=';') errx(1,"Extra text on line %d",linenum);
    skip:
    if(chlabel!=-1) {
      if(!pass) {
        if(!flabel[chlabel]) {
          flabel[chlabel]=open_memstream((char**)(hlabel+chlabel),flabels+chlabel);
          if(!flabel[chlabel]) err(1,"Error with open_memstream");
        }
        fwrite(&wflabel,1,sizeof(Sint32),flabel[chlabel]);
        ++mhlabel[chlabel];
      }
      ++nhlabel[chlabel];
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

static void read_world_file(const char*nam0) {
  char*nam=strdup(nam0);
  char buf[17]="_";
  FILE*f;
  int i,j,c;
  Uint32 z;
  if(!nam) err(1,"Allocation failed");
  i=strlen(nam);
  if(i<4 || nam[i-4]!='.') {
    errx(1,"Cannot determine name of world file");
  } else if(nam[i-3]=='a' && nam[i-2]=='s' && nam[i-1]=='m') {
    nam[i-3]='s'; nam[i-2]='z'; nam[i-1]='0';
  } else if(nam[i-3]=='A' && nam[i-2]=='S' && nam[i-1]=='M') {
    nam[i-3]='S'; nam[i-2]='Z'; nam[i-1]='0';
  } else {
    errx(1,"Cannot determine name of world file");
  }
  f=fopen(nam,"r");
  if(!f) err(1,"Cannot open world file '%s'",nam);
  free(nam);
  for(i=0;;) {
    c=fgetc(f);
    if(c==EOF) errx(1,"ELEMENT lump not found");
    if(i>=0 && i<8 && c=="ELEMENT"[i]) i++; else i=-1;
    if(!c) {
      z=fgetc(f)<<16; z|=fgetc(f)<<24; z|=fgetc(f); z|=fgetc(f)<<8;
      if(i==8) break;
      fseek(f,z,SEEK_CUR);
      i=0;
    }
  }
  if(z>150) {
    fseek(f,148,SEEK_CUR);
    for(i=0;i<256;i++) {
      c=fgetc(f);
      if(c&15) {
        fread(buf+1,1,c&15,f);
        buf[(c&15)+1]=0;
        if(c&0x80) fgetc(f);
        if(c&0x40) fgetc(f);
        if(c&0x20) fgetc(f),fgetc(f),fgetc(f),fgetc(f);
        if(c&0x10) {
          c=fgetc(f); c|=fgetc(f)<<8;
          for(j=0;j<16;j++) if(c&(1<<j)) fgetc(f),fgetc(f);
        }
        add_name(buf,i,0);
      } else {
        i+=c>>4;
      }
    }
    if(option&0x0008) fprintf(stderr,"Found %d element names\n",nnames);
  }
  fclose(f);
}

static void do_output(void) {
  int i;
  if(*outname=='-' && !outname[1]) outfile=stdout; else outfile=fopen(outname,"w");
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
    for(i=0;i<nnames;i++) if(!names[i].kind) fprintf(fp,"%s\tIS $%lX ; %ld\n",names[i].name,(long)names[i].value,(long)names[i].line);
    fclose(fp);
    if(!oo) err(1,"Allocation failed");
    begin_lump("DEBUG",os);
    fwrite(oo,1,os,outfile);
    free(oo);
  }
  if((option&0x0004) && ed_addr_end>256) {
    int n=0;
    int c;
    for(i=256;i<ed_addr_end;n++) {
      if(ed_mem[i]&0xFF00) {
        for(c=0;c<64;c++) if(!ed_mem[i+c] || (ed_mem[i+c]<256 && ed_mem[i+c+1]<256)) break;
        n+=c+c;
      } else if(ed_mem[i]) {
        for(c=0;c<64;c++) if((!ed_mem[i+c] && !ed_mem[i+c+1]) || (ed_mem[i+c]&0xFF00)) break;
        n+=c;
      } else {
        for(c=0;c<64;c++) if(ed_mem[i+c]) break;
      }
      i+=c;
    }
    begin_lump("MEMORY.ED",n);
    for(i=256;i<ed_addr_end;) {
      if(ed_mem[i]&0xFF00) {
        for(c=0;c<64;c++) if(!ed_mem[i+c] || (ed_mem[i+c]<256 && ed_mem[i+c+1]<256)) break;
        fputc(c+0x7F,outfile);
        while(c--) {
          fputc(ed_mem[i],outfile);
          fputc(ed_mem[i++]>>8,outfile);
        }
      } else if(ed_mem[i]) {
        for(c=0;c<64;c++) if((!ed_mem[i+c] && !ed_mem[i+c+1]) || (ed_mem[i+c]&0xFF00)) break;
        fputc(c+0x3F,outfile);
        while(c--) fputc(ed_mem[i++],outfile);
      } else {
        for(c=0;c<64;c++) if(ed_mem[i+c]) break;
        i+=c;
        fputc(c-1,outfile);
      }
    }
    if(ed_nstrings>1) {
      begin_lump("TEXT.ED",ed_tstrings-1);
      for(i=1;i<ed_nstrings;i++) fwrite(ed_strings[i],1,strlen(ed_strings[i])+1,outfile);
    }
  }
  if(*outname!='-' || outname[1]) fclose(outfile);
}

int main(int argc,char**argv) {
  int i;
  while((i=getopt(argc,argv,"+devw"))>0) switch(i) {
    case 'd': option|=0x0001; break;
    case 'e': option|=0x0004; break;
    case 'v': option|=0x0008; break;
    case 'w': option|=0x0002; break;
    default: return 1;
  }
  if(optind+2!=argc) errx(1,"Wrong number of arguments");
  find_string("");
  if(option&0x0004) {
    exchange_strings();
    find_string("");
  }
  infile=fopen(argv[optind],"r");
  if(!infile) err(1,"Cannot open input file");
  outname=argv[optind+1];
  mem[0xEE]=mem[0xEF]=0xFFFF; // scroll rate
  mem[0xE3]=0xFFFF; // light
  if(option&0x0002) read_world_file(argv[optind]);
  do_pass();
  for(i=0;i<10;i++) if(flabel[i]) {
    fclose(flabel[i]);
    if(!hlabel[i]) err(1,"Allocation failed");
    nhlabel[i]=0;
  }
  pass=1;
  mem[0xE2]=0;
  do_pass();
  fclose(infile);
  if(option&0x0008) {
    fprintf(stderr,"Memory: %d",addr_end); if(option&0x0004) fprintf(stderr," (editor: %d)",ed_addr_end); fputc('\n',stderr);
    fprintf(stderr,"Strings: %d/%d",nstrings,tstrings); if(option&0x0004) fprintf(stderr," (editor: %d/%d)",ed_nstrings,ed_tstrings); fputc('\n',stderr);
  }
  do_output();
  return 0;
}

