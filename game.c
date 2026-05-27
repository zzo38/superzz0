#if 0
gcc $CFLAGS -c -std=gnu99 -Wno-unused-result -fwrapv game.c `sdl-config --cflags`
exit
#endif

#include "common.h"
#include "opcodes.h"
#include <math.h>
#include <time.h>

ElementDef elem_def[256];
Uint8 appearance_mapping[128];
Animation animation[4];
Uint16 cur_board_id;
BoardInfo board_info;
Tile*b_under;
Tile*b_main;
Tile*b_over;
Stat*stats;
Uint8 maxstat;
NumericFormat num_format[16];
Screen cur_screen;
Uint16 cur_screen_id;
Sint32 scroll_x,scroll_y; // relative to screen(0,0)
Uint16 memory[0x10000];
Sint32 regs[8];
Uint8 condflag;
Uint8**gtext;
Uint8*vgtext;
Uint16 ngtext;
Sint32 status_vars[16];
Uint8**boardnames;
Uint16 maxboard;
Uint8 textbuf[81];
Uint8 ntextbuf;
Uint8 vtextbuf[81];
Uint8 nvtextbuf;
Uint16 vtexttime;
NamedFlag namedflag[16];
Uint8*global_text;
Uint16 global_length;
Uint16 global_frameoffset;
Uint16 global_frameptr;
DynaString*dynastr;
Uint8 ndynastr;
Uint16 start_mode=1;
VarProperty pvarproperty;
Uint8*itemnames;
ItemDef*itemdefs;
Uint16 nitemdefs;
Inventory inventory[8];
UnordZone*uzone[16];
OrdZone*ozone[16];
ASN1_Value asn1reg[4];

static uint64_t rseed;
static char soundon;
static Uint8 autofire=0;
static Sint8 autofire_dir=-1;

#define PLAYSTATE_NORMAL 16
#define PLAYSTATE_FAST 175
#define PLAYSTATE_PAUSED 186
static Uint8 playstate=PLAYSTATE_NORMAL;
static Uint8 saved_playstate=PLAYSTATE_NORMAL;

typedef struct {
  Uint8 text[81];
} MessageScrollback;
static MessageScrollback*scrback;
static Uint16 nscrback;

#define TEXTREC 81
static FILE*textfile;
static char*textfile_text;
static size_t textfile_size;
// Text window
static Uint16 tnlines,tcursor,tscroll;

static const char*end_of_label;

static Sint32 run_program(Uint16 pc,Sint32 w,Sint32 x,Sint32 y,Sint32 z);
static Sint32 give_item(Uint32 item,Uint32 qty,Uint16 how);
static Sint32 take_item(Uint32 item,Uint32 qty,Uint16 how);
static Sint32 move_item(Uint32 item,Uint32 qty,Uint16 how);
static Sint32 count_items(Sint32 t,Uint32 m);

static Uint32 do_joystick(Uint8 mode) {
  Uint8 b=event.jbutton.button;
  Uint16 v;
  if((joystat->user_shift&0x200) && (v=(joystat->user_shift>>5)&7)) v+=16; else v=mode;
  v=joystat->map[b].a[v];
  if(v==0x300) {
    if(joystat->world_shift&0x200) v=(joystat->world_shift>>5)&15; else v=(memory[MEM_JOY_LEVEL]>>(4*(mode&3)))&15;
    v=joystat->map[b].a[v];
  }
  if(event.type==SDL_JOYBUTTONDOWN) {
    switch(v) {
      case 0x000 ... 0x07F: return v;
      case 0x080 ... 0x0FF: autofire=v&=0x7F; autofire_dir=-1; return v;
      case 0x100 ... 0x17F: return v;
      case 0x200 ... 0x20F: joystat->world_shift=(joystat->world_shift<<10)+0x200+((v&0x0F)<<5)+b; v_status[79]='w'; return 0;
      case 0x210 ... 0x217: joystat->user_shift=(joystat->user_shift<<10)+0x200+((v&0x0F)<<5)+b; v_status[79]='u'; return 0;
      default: return 0;
    }
  } else {
    if(joystat->state) {
      while((joystat->user_shift&0x200) && !((1ULL<<(joystat->user_shift&0x1F))&joystat->state)) joystat->user_shift>>=10;
      while((joystat->world_shift&0x200) && !((1ULL<<(joystat->world_shift&0x1F))&joystat->state)) joystat->world_shift>>=10;
    } else {
      joystat->world_shift=joystat->user_shift=autofire=v_status[79]=0;
    }
    switch(v) {
      case 0x080 ... 0x0FF: if(autofire==(v&0x7F)) autofire=0; return 0;
      case 0x100 ... 0x17F: return v+0x80;
      default: return 0;
    }
  }
}

static void debug_log(Uint8 fo,Sint32 so,Sint32 w,Sint32 x,Sint32 y,Sint32 z,Uint16 pc) {
  FILE*f=stdout; // should it be configurable?
  int i;
  fprintf(f,"[$%04X] %ld ($%lX) : [%c]",pc,(long)so,(unsigned long)so,fo+'A');
  switch(fo) {
    case 0: // All registers
      for(i=0;i<8;i++) fprintf(f," %c=%ld(%lX)",i+'A',(long)regs[i],(unsigned long)regs[i]);
      fprintf(f," W=%ld(%lX) X=%ld(%lX) Y=%ld(%lX) Z=%ld(%lX)",(long)w,(unsigned long)w,(long)x,(unsigned long)x,(long)y,(unsigned long)y,(long)z,(unsigned long)z);
      fprintf(f," ?=%d",condflag);
      break;
    case 1: // Board/screen info
      fprintf(f," B#%d %dx%d S#%d %+ld%+ld",cur_board_id,board_info.width,board_info.height,cur_screen_id,(long)scroll_x,(long)scroll_y);
      break;
    case 2: // Text buffer
      fprintf(f," %d {",ntextbuf);
      for(i=0;i<ntextbuf;i++) if(textbuf[i]>=0x20 && textbuf[i]<0x7F) fputc(textbuf[i],f); else fprintf(f,"<%02X>",textbuf[i]);
      fputc('}',f);
      break;
    case 3: // Status variables
      for(i=0;i<16;i++) fprintf(f," %c=%ld",i+(i&8?'S'-8:'A'),(long)status_vars[i]);
      break;
    case 4: // Memory management
      fputs("(Not implemented)",f);
      break;
    case 5: // Stats
      for(i=0;i<maxstat;i++) fprintf(f,"\n %d: count=%d xy=%p length=%d text=%p speed=%d",i+1,stats[i].count,stats[i].xy,stats[i].length,stats[i].text,stats[i].speed);
      break;
    case 6: // Inventory
      for(i=0;i<inventory[so&7].count;i++)
       fprintf(f,"\n q=%lu ext=%lu,%u,%u i=%u f=(%lX)",(long)inventory[so&7].item[i].quantity,(long)inventory[so&7].item[i].ext0,inventory[so&7].item[i].ext1,inventory[so&7].item[i].ext2,
       inventory[so&7].item[i].item,(long)inventory[so&7].item[i].flag);
      break;
  }
  fputc('\n',f);
}

static int allow_saving(void) {
  int i;
  if(!(memory[MEM_CONTROL]&CONTROL_DISABLE_SAVING)) return 1;
  if(!(board_info.flag&0x0300) || !maxstat || !stats->count) return 0;
  for(i=0;i<stats->count;i++) {
    if((board_info.flag&BF_SAVE_ON_SENSOR) && stats->xy[i].sensor.kind) return 1;
    if(stats->xy[i].x<board_info.width && stats->xy[i].y<board_info.height) {
      if(((elem_def[stats->xy[i].sensor.kind].attrib|elem_def[b_under[stats->xy[i].y*board_info.width+stats->xy[i].x].kind].attrib|elem_def[b_main[stats->xy[i].y*board_info.width+stats->xy[i].x].kind].attrib)
       &A_SENSOR?BF_SAVE_ON_SENSOR:BF_SAVE_NOT_SENSOR)&board_info.flag) return 1;
    }
  }
  return 0;
}

#define CBRANDOM_KEY 6738671342737314685ULL
static inline Uint32 cbrandom(Uint64 c) {
  // Square RNG counter-based random numbers.
  Uint64 x=CBRANDOM_KEY*c;
  Uint64 y=x;
  Uint64 z=CBRANDOM_KEY+y;
  x=x*x+y; x=(x>>32)|(x<<32);
  x=x*x+z; x=(x>>32)|(x<<32);
  x=x*x+y; x=(x>>32)|(x<<32);
  return (x*x+z)>>32;
}

Uint32 dice(Uint32 n) {
  Uint32 o;
  Uint32 m=n-1;
  m|=m>>1; m|=m>>2; m|=m>>4; m|=m>>8; m|=m>>16;
  do {
    rseed^=rseed>>12; rseed^=rseed<<25; rseed^=rseed>>27;
    o=((rseed*0x2545F4914F6CDD1DULL)>>32)&m;
  } while(o>=n);
  return o;
}

Uint32 reseed(uint64_t n) {
  rseed=n?:((uint64_t)time(0))?:(uint64_t)1;
  rseed^=rseed>>12; rseed^=rseed<<25; rseed^=rseed>>27;
  rseed^=rseed>>12; rseed^=rseed<<25; rseed^=rseed>>27;
  rseed++;
  if(!rseed) rseed=1;
  rseed^=rseed>>12; rseed^=rseed<<25; rseed^=rseed>>27;
  rseed^=rseed>>12; rseed^=rseed<<25; rseed^=rseed>>27;
}

const char*select_board(Uint16 b) {
  FILE*fp=0;
  const char*e;
  if(maxstat && stats->text==global_text) stats->text=0;
  if((memory[MEM_CONTROL]&CONTROL_RESTORE_BOARD) && (fp=open_lump("PREVIOUS.BRD","r")) && !lump_size) fclose(fp),fp=0;
  if(!fp) fp=open_lump_by_number(b,"BRD","r");
  if(fp) {
    e=load_board(fp);
    fclose(fp);
    if(!e && (memory[MEM_CONTROL]&CONTROL_RESTORE_BOARD)) revert_lump("PREVIOUS.BRD");
    return e;
  } else {
    return "Cannot open lump";
  }
}

static void load_script_library(Stat*s,const Uint8*name) {
  FILE*fp;
  char buf[16];
  int i;
  for(i=0;i<8;i++) {
    if(name[i]=='.' || name[i]==';' || name[i]<39) break;
    buf[i]=name[i];
  }
  buf[i]='.'; buf[i+1]='L'; buf[i+2]='I'; buf[i+3]='B'; buf[i+4]=0;
  fp=open_lump(buf,"r");
  if(!fp) errx(1,"Cannot open %s",buf);
  if(lump_size>65530) errx(1,"Script library is too big");
  if(s->text!=global_text) free(s->text);
  s->text=malloc(lump_size+1);
  if(!s->text) err(1,"Allocation failed");
  fread(s->text,1,s->length=lump_size,fp);
  s->text[s->length]=0;
  fclose(fp);
}

static Uint16 do_asn1_operator(Uint8 fo,Uint16 op,Uint16 pc) {
  FILE*fp;
  ASN1_Encoder*enc;
  ASN1_Value*r1=asn1reg+(op&3);
  ASN1_Value*r2=asn1reg+((op>>2)&3);
  ASN1_Value u,v;
  size_t s;
  Uint16 pc1=pc;
  Uint32 n;
  int q;
  switch(op>>8) {
    case 0x00: if(r1==r2) break; asn1_free(r1); if(asn1_copy(r2,r1)) err(1,"Allocation failed"); break;
    case 0x01: if(r1==r2) break; asn1_free(r1); *r1=*r2; *r2=(ASN1_Value){.type=ASN1_NULL}; break;
    case 0x02:
      fp=open_lump_by_number(regs[fo]&0xFFFF,"USD","w");
      if(!fp) errx(1,"Cannot open .USD lump");
      asn1_write_type(r1->constructed,r1->class,r1->type,fp);
      asn1_write_length(r1->length,fp);
      fwrite(r1->data,1,r1->length,fp);
      fclose(fp);
      break;
    case 0x03:
      asn1_free(r1);
      fp=open_lump_by_number(regs[fo]&0xFFFF,"USD","r");
      if(!fp) errx(1,"Cannot open .USD lump");
      if(asn1_read_item(fp,r1,0)) errx(1,"Error reading from .USD lump");
      fclose(fp);
      break;
    case 0x04: memory[MEM_ARG_J]=r1->class; memory[MEM_ARG_K]=r1->type; regs[fo]=r1->length; condflag=r1->constructed; break;
    case 0x05: r1->class=memory[MEM_ARG_J]; r1->type=memory[MEM_ARG_K]; r1->constructed=condflag; break;
    case 0x06: case 0x07:
      asn1_free(r1);
      if(!(enc=asn1_start_encoding_value(r1))) err(1,"Allocation failed");
      if(op&0x100?asn1_encode_uint32(enc,regs[fo]):asn1_encode_int32(enc,regs[fo])) errx(1,"Error encoding ASN.1 value");
      if(asn1_finish_encoder(enc)) err(1,"Allocation failed");
      break;
    case 0x08: condflag=!asn1_decode_int32(r1,ASN1_AUTO,(void*)(regs+fo)); break;
    case 0x09: condflag=!asn1_decode_uint32(r1,ASN1_AUTO,(void*)(regs+fo)); break;
#ifndef CONFIG_DISABLE_FRONT
    case 0x0A: asn1_encode(extern_out,r1); asn1_flush(extern_out); break;
#endif
    case 0x0B:
      condflag=1;
      for(s=0;condflag && s<r1->length;s++) {
        if(ntextbuf>=80 || !r1->data[s]) condflag=0; else textbuf[ntextbuf++]=r1->data[s];
      }
      textbuf[ntextbuf]=0;
      regs[fo]=ntextbuf;
      break;
    case 0x0C: revert_lump_by_number(regs[fo]&0xFFFF,"USD"); break;
    case 0x0D: case 0x0E: case 0x2D: case 0x2E:
      q=0x0100&~op;
      v=(ASN1_Value){};
#ifndef CONFIG_DISABLE_FRONT
      if(op&0x2000) {
        enc=extern_out;
        if(!q) asn1_construct(enc,memory[MEM_ARG_J],memory[MEM_ARG_K],fo==2?ASN1_KVSORT:fo==1?ASN1_SORT:0);
      } else
#endif
      enc=q?asn1_start_encoding_value(&v):asn1_start_encoding_constructed_value(&v,memory[MEM_ARG_J],memory[MEM_ARG_K],fo==2?ASN1_KVSORT:fo==1?ASN1_SORT:0);
      if(!enc) errx(1,"Internal error");
      for(n=1;n;) {
        op=run_program(pc,0,0,0,0);
        pc=(op&0x20?pc1:memory[MEM_RETURNED_PC]);
        switch(op>>8) {
          case 0x00: case 0x01: case 0x0A: case 0x0F: pc=do_asn1_operator(0,op,pc); break;
          case 0x10: n++; asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0); break;
          case 0x11: n++; asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SET,ASN1_SORT); break;
          case 0x12: n++; asn1_construct(enc,ASN1_UNIVERSAL,ASN1_KEY_VALUE_LIST,ASN1_KVSORT); break;
          case 0x13: asn1_implicit(enc,memory[MEM_ARG_J],memory[MEM_ARG_K]); break;
          case 0x14: asn1_encode(enc,asn1reg+(op&3)); goto once;
          case 0x15: asn1_encode_boolean(enc,condflag); goto once;
          case 0x16: asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_NULL,"",0); goto once;
          case 0x17: asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,textbuf,ntextbuf); goto once;
          case 0x18: asn1_encode_integer(enc,status_vars[op&15]); goto once;
          case 0x19: if(n>1 || !q) asn1_encode(enc,asn1reg+(op&3)); asn1_encode(enc,asn1reg+((op>>4)&3)); goto once;
          default: errx(1,"Unimplemented ASN.1 operator (or incorrect context): $%04X",op); break;
          once: if(q && n==1) goto fin; break;
        }
        if(op&0x10) if(--n) {
          asn1_end(enc);
          if(q && n<=1) goto fin;
        }
      }
#ifndef CONFIG_DISABLE_FRONT
      fin: if(enc==extern_out?(q?0:asn1_end(enc))||asn1_flush(enc):asn1_finish_encoder(enc)) err(1,"Allocation failed");
#else
      fin: if(asn1_finish_encoder(enc)) err(1,"Allocation failed");
#endif
      asn1_free(r1);
      *r1=v;
      break;
    case 0x0F: v=*r1; *r1=*r2; *r2=v; break;
    case 0x28: if(regs[fo]>=0 && regs[fo]<r1->length && !r1->constructed) memory[MEM_ARG_J]=r1->data[regs[fo]],condflag=1; else condflag=0; break;
    case 0x29:
      q=regs[fo]&0xFF;
      if(q<1 || q>maxstat || stats[q-1].text || r1->constructed || r1->length>0xFFF8 || r1->class!=ASN1_UNIVERSAL || r1->length<0
       || (r1->type!=ASN1_IA5_STRING && r1->type!=ASN1_PC_STRING && r1->type!=ASN1_OCTET_STRING) || (r1->length && memchr(r1->data,0,r1->length))) errx(1,"Improper use of ASN1 $%04X",op);
      if(!r1->length) break;
      if(!(stats[q-1].text=malloc(r1->length+1))) err(1,"Allocation failed");
      memcpy(stats[q-1].text,r1->data,r1->length);
      stats[q-1].text[r1->length]=0;
      stats[q-1].length=r1->length;
      stats[q-1].frame=0;
      break;
    case 0x30:
      u=*r1; r1->own=0; asn1_free(r2); *r2=(ASN1_Value){.type=ASN1_NULL};
      condflag=0;
      if(!asn1_first_of(&v,&u)) for(condflag=1;;) {
        asn1_free(r2); *r2=v;
        op=run_program(pc,v.class,v.type,v.length,v.constructed);
        pc=(op&0x20?pc1:memory[MEM_RETURNED_PC]);
        switch(op>>8) {
          case 0x00: case 0x01: case 0x0A: case 0x0F: pc=do_asn1_operator(0,op,pc); break;
          case 0x31: condflag=0; if(asn1_next_of(&v,&u)) goto finr; condflag=1; break;
          case 0x32: condflag=1; asn1_first_of(&v,&u); break;
          default: errx(1,"Unimplemented ASN.1 operator (or incorrect context): $%04X",op); break;
        }
        if(op&0x10) break;
        if((op&0x40) && condflag) pc=pc1;
      }
      finr:
      asn1_free(r1); *r1=u;
      asn1_free(r2); *r2=(ASN1_Value){.type=ASN1_NULL};
      break;
    case 0x38: case 0x39:
      v=(ASN1_Value){};
#ifndef CONFIG_DISABLE_FRONT
      if(op&0x100) enc=extern_out; else
#endif
      enc=asn1_start_encoding_value(&v);
      if(!enc) errx(1,"Internal error");
      fp=asn1_primitive_stream(enc,memory[MEM_ARG_J],memory[MEM_ARG_K]);
      if(!fp) errx(1,"Internal error");
      for(;;) {
        op=run_program(pc,v.class,v.type,v.length,v.constructed);
        pc=(op&0x20?pc1:memory[MEM_RETURNED_PC]);
        switch(op>>8) {
          case 0x3A: fputc(memory[MEM_ARG_J],fp); break;
          case 0x3B: fputc(regs[op&7],fp); break;
          case 0x3C: fwrite(textbuf,1,ntextbuf,fp); break;
          case 0x3D: fwrite(asn1reg[op&3].data,1,asn1reg[op&3].length,fp); break;
          case 0x3E:
            if(regs[0]<0) regs[1]+=regs[0],regs[0]=0;
            if(regs[0]>asn1reg[op&3].length) regs[0]=asn1reg[op&3].length;
            if(regs[1]<0) regs[1]=0;
            if(regs[1]>asn1reg[op&3].length-regs[0]) regs[1]=asn1reg[op&3].length-regs[0];
            if(regs[1]>0 && regs[0]>=0 && regs[0]<asn1reg[op&3].length) fwrite(asn1reg[op&3].data+regs[0],1,regs[1],fp);
            break;
          default: errx(1,"Unimplemented ASN.1 operator (or incorrect context): $%04X",op); break;
        }
        if(op&0x10) break;
      }
      if(asn1_end(enc)) errx(1,"Internal error");
#ifndef CONFIG_DISABLE_FRONT
      if(enc==extern_out?asn1_flush(enc):asn1_finish_encoder(enc)) err(1,"Allocation failed");
      if(enc!=extern_out)
#else
      if(asn1_finish_encoder(enc)) err(1,"Allocation failed");
#endif
      { asn1_free(r1); *r1=v; }
      break;
    default: errx(1,"Unimplemented ASN.1 operator (or incorrect context): $%04X",op);
  }
  if(op&0x80) regs[fo]=memory[MEM_ARG_J];
  return pc;
}

static void init_new_board(void) {
  Uint16 m=memory[MEM_CREATE_BOARD];
  Uint16 n=memory[m]&0xFF;
  Uint32 a,i;
  if((m+n)&~0xFFFF) errx(1,"Create board memory has too many fields");
  if(maxstat && stats->text==global_text) stats->text=0,stats->length=0;
  board_info.width=(n<1?0:memory[m+1])?:board_info.width;
  board_info.height=(n<2?0:memory[m+2])?:board_info.height;
  if(!board_info.width || !board_info.height) errx(1,"Trying to create a board with improper dimensions");
  board_info.screen=(n<4?0:memory[m+4]);
  board_info.flag=(n<5?0:memory[m+5]);
  for(i=0;i<4;i++) board_info.exits[i]=0;
  if(!(memory[m]&0x2000)) board_info.userdata=0;
  for(i=0;i<16;i++) {
    free(ozone[i]); ozone[i]=0;
    free(uzone[i]); uzone[i]=0;
  }
  free(b_under);
  b_under=calloc(3*sizeof(Tile),board_info.width*(uint32_t)board_info.height);
  if(!b_under) err(1,"Allocation failed");
  b_main=b_under+board_info.height*board_info.width;
  b_over=b_main+board_info.height*board_info.width;
  if(n>=6 && (i=memory[m+6])) {
    for(a=0;a<board_info.width*board_info.height;a++) b_over[a]=(Tile){OVER_VISIBLE,i>>8,i,0};
  }
  if(n>=7 && (i=memory[m+7])) {
    for(a=0;a<board_info.width*board_info.height;a++) b_main[a]=(Tile){i,i>>8,0,0};
  }
  a=(n<3?0:memory[m+3]);
  if(a>maxstat) a=maxstat;
  for(i=a;i<maxstat;i++) {
    free(stats[i].text);
    free(stats[i].xy);
  }
  maxstat=a;
  for(i=0;i<a;i++) if(!((stats[i].mode&STAT_INDEPENDENT) && (memory[m]&0x1000))){
    free(stats[i].xy);
    stats[i].xy=0;
    stats[i].count=0;
  }
  if(!(memory[m]&0x4000)) {
    free(board_info.varprop.item);
    board_info.varprop.item=0;
    board_info.varprop.count=0;
  }
}

static inline Uint8 in_light(Uint32 x,Uint32 y) {
  Sint32 q=y-stats->xy->y-scroll_y;
  Uint16 f;
  if(q>-25 && q<25) {
    f=memory[memory[MEM_LIGHT]+q+24];
    q=128+x-stats->xy->x-scroll_x;
    if(q>=(f>>8) && q<=(f&0xFF)) return 1;
  }
  return 0;
}

Uint8 in_zone(Uint32 x,Uint32 y,Uint8 z) {
  OrdZone*o;
  UnordZone*u;
  Uint8 r=z>>7;
  Uint32 at=y*board_info.width+x;
  if(x>=board_info.width || y>=board_info.height) return 0;
  switch(z&=0x7F) {
    case 0x00 ... 0x0F: if((u=uzone[z&15]) && y<=u->maxy && ((1<<(at&7))&u->data[at/8])) r^=1; break;
    case 0x10 ... 0x1F: if(o=ozone[z&15]) for(at=0;at<o->ncells;at++) if(o->xy[at].x==x && o->xy[at].y==y) return r^1; break;
    case 0x20 ... 0x2F: if((elem_def[b_main[at].kind].attrib&15)==(z&15) || (elem_def[b_under[at].kind].attrib&15)==(z&15)) r^=1; break;
    case 0x30 ... 0x37: if(b_over[at].kind&(1<<(z&7))) r^=1; break;
    case 0x38 ... 0x3F: if((elem_def[b_main[at].kind].attrib|elem_def[b_under[at].kind].attrib)&(A_MISC_A<<(z&7))) r^=1; break;
    case 0x40 ... 0x4F: if((elem_def[b_main[at].kind].attrib&15)==(z&15) || (elem_def[b_under[at].kind].attrib&15)==(z&15)) r^=1; break;
    case 0x50 ... 0x5F: if((elem_def[(elem_def[b_main[at].kind].attrib&A_FLOOR?b_main:b_under)[at].kind].attrib&15)==(z&15)) r^=1; break;
    case 0x7E: if(memory[MEM_LIGHT]<65486 && maxstat && stats->count && in_light(x,y)) r^=1; break;
    case 0x7F: return r^1; break;
  }
  return r;
}

void zone_add(Uint32 x,Uint32 y,Uint8 z,Uint8 w) {
  OrdZone*o;
  UnordZone*u;
  Uint32 at=y*board_info.width+x;
  if(z&0x80) zone_remove(x,y,z&0x7F);
  if(x>=board_info.width || y>=board_info.height) return;
  switch(z) {
    case 0x00 ... 0x0F:
     if(!(u=uzone[z&15])) u=uzone[z&15]=calloc(1,board_info.width/8+1+sizeof(UnordZone));
     if(!u) err(1,"Allocation failed");
     if(y>u->maxy) {
       u=uzone[z&15]=realloc(u,((y+1)*(Uint32)board_info.width)/8+1+sizeof(UnordZone));
       if(!u) err(1,"Allocation failed");
       memset(u->data+((u->maxy+1)*(Uint32)board_info.width)/8+1,0,((y+1)*(Uint32)board_info.width)/8-((u->maxy+1)*(Uint32)board_info.width)/8);
       u->maxy=y;
     }
     u->data[at/8]|=1<<(at&7);
     break;
    case 0x10 ... 0x1F:
      if(!(o=ozone[z&15])) o=ozone[z&15]=calloc(1,sizeof(OrdZone)+sizeof(OrdZoneXY));
      if(!o) err(1,"Allocation failed");
      if(o->ncells==0xFFFF) errx(1,"Too many cells in ordered zone");
      if(o->flag&ZF_REVERSE) w^=1;
      o=ozone[z&15]=realloc(o,sizeof(OrdZone)+(o->ncells+1)*sizeof(OrdZoneXY));
      if(!o) err(1,"Allocation failed");
      if(w) {
        o->xy[o->ncells].x=x; o->xy[o->ncells].y=y;
      } else {
        memmove(o->xy+1,o->xy,o->ncells*sizeof(OrdZoneXY));
        o->xy->x=x; o->xy->y=y;
      }
      o->ncells++;
      break;
    case 0x30 ... 0x37: b_over[at].kind|=1<<(z&7);
    case 0x80 ... 0xFF: zone_remove(x,y,z&0x7F); break;
  }
}

void zone_remove(Uint32 x,Uint32 y,Uint8 z) {
  OrdZone*o;
  UnordZone*u;
  Uint32 at=y*board_info.width+x;
  if(x>=board_info.width || y>=board_info.height) return;
  switch(z) {
    case 0x00 ... 0x0F: if((u=uzone[z&15]) && y<=u->maxy) u->data[at/8]&=~(1<<(at&7)); break;
    case 0x10 ... 0x1F:
      if(o=ozone[z&15]) for(at=0;at<o->ncells;at++) if(o->xy[at].x==x && o->xy[at].y==y) {
        if(at!=o->ncells-1) memmove(o->xy+at,o->xy+at+1,(o->ncells-at-1)*sizeof(OrdZoneXY));
        o->ncells--;
        break;
      }
      break;
    case 0x30 ... 0x37: b_over[at].kind&=~(1<<(z&7));
    case 0x80 ... 0xFF: zone_add(x,y,z&0x7F,1); break;
  }
}

static Uint32 zone_info(Uint8 s,Uint32 v) {
  UnordZone*u=uzone[s&15];
  OrdZone*o=ozone[s&15];
  Uint32 a=(s>>4)&15;
  Uint32 b;
  if(a==6 || a==10) {
    if(!u) u=uzone[s&15]=calloc(1,board_info.width/8+1+sizeof(UnordZone));
    if(!u) errx(1,"Allocation failed");
  } else if(a==7 || a==11) {
    if(!o) o=ozone[s&15]=calloc(1,sizeof(OrdZone)+sizeof(OrdZoneXY));
    if(!o) errx(1,"Allocation failed");
  }
  switch((s>>4)&15) {
    case 0: // unordered read count
      v=0;
      if(u) {
        b=((u->maxy+1)*board_info.width)>>3;
        for(a=0;a<b;a++) v+=__builtin_popcount(u->data[a]);
        if((b=(u->maxy+1)*board_info.width)&7) v+=__builtin_popcount(u->data[b>>3]&~(0xFF<<(b&7)));
      }
      return v;
    case 1: // ordered read count
      return o?o->ncells:0;
    case 2: // unordered write count
      if(u) {
        u->maxy=0;
        memset(u->data,0,board_info.width/8+1);
      }
      return v;
    case 3: // ordered write count
      if(o) o->ncells=0;
      return v;
    case 4: // unordered read flag
      return u?u->flag:0;
    case 5: // ordered read flag
      return o?o->flag:0;
    case 6: // unordered write flag
      return u->flag=v;
    case 7: // ordered write flag
      return o->flag=v;
    case 8: // unordered read extra
      return u?u->extra:0;
    case 9: // ordered read extra
      return o?o->extra:0;
    case 10: // unordered write extra
      return u->extra=v;
    case 11: // ordered write extra
      return o->extra=v;
    default: errx(1,"Improper use of ZINF");
  }
}

static Sint32 zone_enum(Uint8 r,Uint16 z) {
  OrdZone*o=ozone[z&15];
  Sint32 w=regs[r];
  if(!o) {
    if(!w) {
      append:
      zone_add(0,0,(z&15)+16,w?1:0);
      condflag=1;
    }
    return w;
  }
  if(o->ncells==0xFFFF && (z&0x10)) return w;
  if(w<0) return w;
  if(w>=o->ncells) {
    if(w==o->ncells && (z&0x10)) goto append;
    return w;
  }
  if(z&0x10) {
    if(o->ncells==0xFFFF) return w;
    o=ozone[z&15]=realloc(o,sizeof(OrdZone)+(o->ncells+1)*sizeof(OrdZoneXY));
    if(!o) err(1,"Allocation failed");
    memmove(o->xy+w+1,o->xy+w,(o->ncells-w)*sizeof(OrdZoneXY));
    o->ncells++;
  }
  condflag=1;
  return w;
}

static void warp_to_board(Uint16 b,char m) {
  FILE*fp;
  const char*e;
  Sint32 x,y;
  if(global_text && maxstat && stats->text==global_text) {
    global_frameoffset=stats->frame;
    if(stats->count) {
      memory[MEM_GLOBAL_INSTPTR]=stats->xy->instptr;
      x=memory[MEM_GLOBAL_DELAY];
      if(x&0x0100) x=(x&0xFF00)|(stats->xy->delay&0xFF);
      if(x&0x0200) x=(x&0xA3FF)|((stats->xy->layer&0x5C)<<8);
      if(x&0x2000) x=(x&0x7FFF)|((stats->xy->layer&0x80)<<8);
      memory[MEM_GLOBAL_DELAY]=x;
      global_frameptr=stats->xy->frame;
    }
  }
  if((memory[MEM_CONTROL]&CONTROL_SAVE_BOARD) && !m) {
    fp=open_lump("PREVIOUS.BRD","w");
    if(!fp) err(1,"Cannot open PREVIOUS.BRD");
    if(e=save_board(fp,1)) errx(1,"Error saving board #%d as PREVIOUS.BRD: %s",cur_board_id,e);
    fclose(fp);
    memory[MEM_CONTROL]&=~CONTROL_SAVE_BOARD;
  } else if((board_info.flag&BF_PERSIST) && !m) {
    for(x=0;x<maxstat;x++) {
      if(stats[x].xy && !(stats[x].mode&STAT_INDEPENDENT)) for(y=0;y<stats[x].count;y++) {
        if(!(stats[x].xy[y].layer&3) || stats[x].xy[y].x>=board_info.width && stats[x].xy[y].y>=board_info.height) {
          memmove(stats[x].xy+y,stats[x].xy+y+1,(--stats[x].count-y)*sizeof(StatXY));
          --y;
        }
      }
    }
    fp=open_lump_by_number(cur_board_id,"BRD","w");
    if(!fp) err(1,"Cannot open %04X.BRD",cur_board_id);
    if(e=save_board(fp,0)) errx(1,"Error saving board #%d: %s",cur_board_id,e);
    fclose(fp);
  }
  if(cur_board_id!=b || !board_info.width || (!m && !(board_info.flag&BF_PERSIST)) || (memory[MEM_CONTROL]&CONTROL_RESTORE_BOARD)) {
    x=cur_board_id;
    if(e=select_board(cur_board_id=b)) {
      if(memory[MEM_CREATE_BOARD]) {
        init_new_board();
        run_program(memory[MEM_CREATE_BOARD]+(memory[memory[MEM_CREATE_BOARD]]&0xFF),x,0,0,0);
        if(memory[memory[MEM_CREATE_BOARD]]&0x8000) {
          fp=open_lump_by_number(cur_board_id,"BRD","w");
          if(!fp) err(1,"Cannot open %04X.BRD",cur_board_id);
          if(e=save_board(fp,0)) errx(1,"Error saving board #%d: %s",cur_board_id,e);
          fclose(fp);
        }
        goto created;
      }
      errx(1,"Error loading board #%d: %s",b,e);
    }
  }
  for(x=0;x<maxstat;x++) if(stats[x].text && stats[x].text[0]=='@' && stats[x].text[1]=='!' && stats[x].text!=global_text) load_script_library(stats+x,stats[x].text+2);
  created:
  if(global_text && maxstat && !stats->text && !(board_info.flag&BF_NO_GLOBAL)) {
    stats->text=global_text;
    stats->length=global_length;
    stats->frame=global_frameoffset;
    if(stats->count) {
      stats->xy->instptr=memory[MEM_GLOBAL_INSTPTR];
      stats->xy->frame=global_frameptr;
      x=memory[MEM_GLOBAL_DELAY];
      if(x&0x0100) stats->xy->delay=x&0xFF;
      if(x&0x0200) stats->xy->layer=(stats->xy->layer&0xA3)|((x>>8)&0x5C);
      if(x&0x2000) stats->xy->layer=(stats->xy->layer&0x7F)|((x>>8)&0x80);
    }
  }
  if(m || cur_screen_id!=board_info.screen) {
    fp=open_lump_by_number(cur_screen_id=board_info.screen,"SCR","r");
    if(!fp) err(1,"Cannot open %04X.SCR",cur_screen_id);
    if(e=load_screen(fp)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
    fclose(fp);
    work_varproperties(&cur_screen.varprop);
  }
  // Initial scrolling
  if((cur_screen.flag&SF_NO_SCROLL) || !maxstat || !stats->count) {
    scroll_x=-cur_screen.hard_edge[DIR_W];
    scroll_y=-cur_screen.hard_edge[DIR_N];
  } else {
    x=stats->xy->x; y=stats->xy->y;
    scroll_x=x-cur_screen.view_x;
    scroll_y=y-cur_screen.view_y;
    if(scroll_x<-(Sint32)cur_screen.hard_edge[DIR_W]) scroll_x=cur_screen.hard_edge[DIR_W];
     else if(scroll_x>=board_info.width-(Sint32)cur_screen.hard_edge[DIR_E]) scroll_x=board_info.width-cur_screen.hard_edge[DIR_E]-1;
    if(scroll_y<-(Sint32)cur_screen.hard_edge[DIR_N]) scroll_y=cur_screen.hard_edge[DIR_N];
     else if(scroll_y>=board_info.height-(Sint32)cur_screen.hard_edge[DIR_S]) scroll_y=board_info.height-cur_screen.hard_edge[DIR_S]-1;
  }
  work_varproperties(&board_info.varprop);
}

static void display_message_text(void) {
  Uint32 y=cur_screen.message_y;
  Sint32 x,z;
  Uint8 m=(cur_screen.flag&SF_FLASHY_MESSAGE?0x80:0xFF);
  if(y>25) return;
  if(cur_screen.flag&SF_LEFT_ALIGN_MESSAGE) {
    x=cur_screen.message_l;
  } else {
    x=cur_screen.message_x-nvtextbuf/2;
    if(x<cur_screen.message_l) x=cur_screen.message_l;
  }
  z=draw_text(x,y,vtextbuf,(cur_screen.flag&SF_FLASHY_MESSAGE?memory[MEM_FRAME_COUNTER]%7+9:0),nvtextbuf);
  if(cur_screen.flag&SF_MESSAGE_EDGE) {
    if(x>cur_screen.message_l) v_char[--x +y*80]=0,v_color[x+y*80]=0;
    if(z<cur_screen.message_r) v_color[z+y*80]=0,v_char[z++ +y*80]=0;
  }
  x+=y*80;
  z+=y*80;
  while(x<z) {
    v_color[x]|=m&cur_screen.color[x];
    x++;
  }
}

static Uint8 digit_of(Uint32 n,Uint8 f) {
  static const Uint8 roman1[10]={0,1,2,3,2,1,2,3,4,1};
  static const Uint8 roman2[40]={
    0,0,0,0,
    0,0,0,0,
    0,0,0,0,
    0,0,0,0,
    0,1,0,0,
    1,0,0,0,
    1,0,0,0,
    1,0,0,0,
    1,0,0,0,
    0,2,0,0,
  };
  static const Uint8 roman3[7]={'I'-'A','V'-'A','X'-'A','L'-'A','C'-'A','D'-'A','M'-'A'};
  NumericFormat*nf=num_format+(f>>4);
  Uint8 d=nf->div;
  const Uint8*b;
  int i;
  Uint32 q;
  f&=15;
  switch(nf->code) {
    case NF_DECIMAL: decimal:
      n/=d;
      for(i=0;i<f;i++) n/=10;
      return n?(n%10+'0'):nf->lead;
    case NF_HEX_UPPER:
      n/=d;
      n>>=4*f;
      return n?((n&15)+((n&15)>9?'A'-10:'0')):nf->lead;
    case NF_HEX_LOWER:
      n/=d;
      n>>=4*f;
      return n?((n&15)+((n&15)>9?'a'-10:'0')):nf->lead;
    case NF_OCTAL:
      n/=d;
      n>>=3*f;
      return n?(n&7)+'0':nf->lead;
    case NF_COMMA:
      n/=d;
      for(i=0;i<f;i++) n/=10;
      return n?nf->mark:nf->lead;
    case NF_ROMAN:
      n+=n; n/=d;
      if(n>=2000) {
        q=n/2000;
        n%=2000;
        if(f<q) return 'M'-'A'+nf->mark;
        f-=q;
      }
      if(n>=200) {
        q=n/200;
        n%=200;
        if(f<roman1[q]) return nf->mark+roman3[roman2[4*q+f]+4];
        f-=roman1[q];
      }
      if(n>=20) {
        q=n/20;
        n%=20;
        if(f<roman1[q]) return nf->mark+roman3[roman2[4*q+f]+2];
        f-=roman1[q];
      }
      if(n>=2) {
        q=n/2;
        n%=2;
        if(f<roman1[q]) return nf->mark+roman3[roman2[4*q+f]+0];
        f-=roman1[q];
      }
      if(n && !f) return nf->mark+'S'-'A';
      return nf->lead;
    case NF_LSD_MONEY:
      if(f>4) {
        n/=240;
        goto decimal;
      }
      if(!f) {
        if(d==2) return n&1?nf->mark:nf->lead;
        if(d==4) return n&3?(n&3)+'0':nf->lead;
        return nf->lead;
      }
      n/=d;
      if(f<3) n%=12; else n=(n/12)%20;
      if(!(f&1)) n/=10;
      return n?(n%10+'0'):nf->lead;
    case NF_METER:
      return (n+d-1)/d>f?nf->mark:nf->lead;
    case NF_METER_HALF:
      n+=d-1; n/=d;
      return n>=f+f?219:n==f+f-1?nf->mark:nf->lead;
    case NF_METER_EXT:
      return (n+d-1)/d>f+16?nf->mark:nf->lead;
    case NF_METER_HALF_EXT:
      n/=d;
      return n>=f+f+32?219:n==f+f+31?nf->mark:nf->lead;
    case NF_BINARY:
      return ((n/d)>>f)&1?nf->mark:nf->lead;
    case NF_BINARY_EXT:
      return ((n/d)>>(f+16))&1?nf->mark:nf->lead;
    case NF_CHARACTER:
      return (n>>(8*f))?:nf->lead;
    case NF_NONZERO:
      return n>=d?nf->mark:nf->lead;
    case NF_BOARD_NAME:
      if(n>maxboard || !(b=boardnames[n])) return nf->lead;
      for(i=0;i<f && b[i];i++);
      return b[i]?:nf->lead;
    case NF_BOARD_NAME_EXT:
      if(n>maxboard || !(b=boardnames[n])) return nf->lead;
      for(i=0;i<f+16 && b[i];i++);
      return b[i]?:nf->lead;
  }
  return '?';
}

static inline Uint8 line_class_of(Sint32 bx,Sint32 by,Uint32 xy) {
  if(bx<0 || bx>=board_info.width || by<0 || by>=board_info.height) return 0x80;
  return (1<<(elem_def[b_main[xy].kind].app[0]>>6))|(1<<(elem_def[b_under[xy].kind].app[0]>>6));
}

static Uint8 draw_tile(Sint32 bx,Sint32 by,Uint16 at,Uint8 h) {
  Uint32 xy=by*board_info.width+bx;
  Sint32 q;
  Uint16 f;
  Uint8 o=0;
  Uint8 z,m,d;
  ElementDef*e;
  Tile*t;
  if(bx>=0 && bx<board_info.width && by>=0 && by<board_info.height) {
    tile:
    t=b_main+xy;
    e=elem_def+t->kind;
    if(!(h&4) && !(e->attrib&A_LIGHT) && (b_over[xy].kind&OVER_VISIBLE) && (board_info.flag&BF_OVERLAY)) {
      if(memory[MEM_LIGHT]<65486 && maxstat && stats->count) {
        q=by-stats->xy->y-scroll_y;
        if(q>-25 && q<25) {
          f=memory[memory[MEM_LIGHT]+q+24];
          q=128+bx-stats->xy->x-scroll_x;
          if(q>=(f>>8) && q<=(f&0xFF)) goto light;
        }
      }
      o=b_over[xy].kind;
    }
    light:
    v_font[at]=board_info.flag&((o&(OVER_VISIBLE|OVER_BG_THRU))==OVER_VISIBLE?BF_OVER_ALT_MODE:BF_MAIN_ALT_MODE)?VF_ALTERNATE:0;
    if(o&OVER_VISIBLE) {
      v_char[at]=b_over[xy].param;
      v_color[at]=b_over[xy].color;
      if(o&OVER_BG_THRU) v_color[at]|=t->color&0xF0;
    } else {
      if((e->app[0]&0x3F)==AP_UNDER) {
        t=b_under+xy;
        e=elem_def+t->kind;
      }
      switch(e->app[0]&0x3F) {
        case AP_FIXED: v_char[at]=e->app[1]; break;
        case AP_PARAM: v_char[at]=e->app[1]+t->param; break;
        case AP_OVER: v_char[at]=b_over[xy].param; break;
        case AP_UNDER: v_char[at]=e->app[1]; break;
        case AP_SCREEN: v_char[at]=cur_screen.parameter[at]; break;
        case AP_MISC1: v_char[at]=(t->stat && t->stat<=maxstat)?stats[t->stat-1].misc1:e->app[1]; break;
        case AP_MISC2: v_char[at]=(t->stat && t->stat<=maxstat)?stats[t->stat-1].misc2:e->app[1]; break;
        case AP_MISC3: v_char[at]=(t->stat && t->stat<=maxstat)?stats[t->stat-1].misc3:e->app[1]; break;
        case AP_LINES:
          m=e->app[1];
          if(h&0x80) {
            z=(m&0x80?15:0);
          } else {
            z=0;
            if(m&line_class_of(bx+1,by,xy+1)) z|=1;
            if(m&line_class_of(bx,by-1,xy-board_info.width)) z|=2;
            if(m&line_class_of(bx-1,by,xy-1)) z|=4;
            if(m&line_class_of(bx,by+1,xy+board_info.width)) z|=8;
          }
          v_char[at]=appearance_mapping[(m&0x70)+z];
          break;
        case AP_ANIMATE:
          m=animation[e->app[1]&3].mode;
          z=bx*(m&3)+by*((m>>2)&3);
          if(e->app[1]&0x80) z+=memory[MEM_FRAME_COUNTER]>>(m&AM_SLOW?1:0);
          d=animation[e->app[1]&3].step[z&3];
          v_char[at]=appearance_mapping[((e->app[1]&0x7C)+d)&0x7F];
          break;
        case AP_CBRANDOM:
          f=cbrandom((cur_board_id*123456789ULL)^(bx*54321ULL)^(by*42ULL)^(e->app[1]&0x83));
          if((e->app[1]&0x80) && ((bx^by)&1)) f&=f>>8;
          z=
            "0000111122223333"
            "0000001111122233"
            "0000000111222333"
            "0000000000111223"
          [(f&15)+((e->app[1]&3)<<4)]&3;
          v_char[at]=appearance_mapping[(e->app[1]&0x7C)+z];
          break;
        default:
          d=(t->param>>(e->app[0]&7))&~(0xFF<<(((e->app[0]>>3)&3)+1));
          if(e->app[1]&0x80) {
            m=animation[e->app[1]&1].mode;
            z=memory[MEM_FRAME_COUNTER];
            if(m&AM_SLOW) z>>=1;
            z+=bx*(m&3)+by*((m>>2)&3);
            d+=animation[e->app[1]&1].step[z&3];
          }
          v_char[at]=appearance_mapping[((e->app[1]&0x7E)+d)&0x7F];
          break;
      }
      switch(e->attrib&(A_OVER_COLOR|A_UNDER_COLOR)) {
        case 0: v_color[at]=b_main[xy].color; break;
        case A_OVER_COLOR: v_color[at]=b_over[xy].color; break;
        case A_UNDER_COLOR: v_color[at]=b_under[xy].color; break;
        case A_OVER_COLOR|A_UNDER_COLOR: v_color[at]=cur_screen.color[at]; break;
      }
    }
    if(v_color[at]<16 && (e->attrib&A_UNDER_BGCOLOR)) v_color[at]|=b_under[xy].color&0xF0;
  } else if(h&2) {
    if(bx==-1 && cur_screen.border[DIR_W] && by>=0 && by<board_info.height) {
      v_char[at]=cur_screen.border[DIR_W];
      v_color[at]=cur_screen.border_color;
    } else if(bx==board_info.width && cur_screen.border[DIR_E] && by>=0 && by<board_info.height) {
      v_char[at]=cur_screen.border[DIR_E];
      v_color[at]=cur_screen.border_color;
    } else if(by==board_info.height && cur_screen.border[DIR_S] && bx>=0 && bx<board_info.width) {
      v_char[at]=cur_screen.border[DIR_S];
      v_color[at]=cur_screen.border_color;
    } else if(by==-1 && cur_screen.border[DIR_N] && bx>=0 && bx<board_info.width) {
      v_char[at]=cur_screen.border[DIR_N];
      v_color[at]=cur_screen.border_color;
    } else {
      goto noborder;
    }
  } else {
    noborder:
    if(h&1) {
      v_char[at]=cur_screen.parameter[at];
      v_color[at]=cur_screen.color[at];
    } else {
      h|=0x80;
      xy=(by<0?0:by>=board_info.height?board_info.height-1:by)*board_info.width+(bx<0?0:bx>=board_info.width?board_info.width-1:bx);
      goto tile;
    }
  }
}

static void display_item_element_cell(Uint16 i,Uint8 col,Uint8 inv,Uint16 sl) {
  ItemSlot s;
  ItemDef d;
  if(inventory[inv].count<=sl) s=(ItemSlot){}; else s=inventory[inv].item[sl];
  if(s.item && s.item<=nitemdefs) d=itemdefs[s.item-1]; else d=(ItemDef){.element=244,.color=col};
  switch(elem_def[d.element].app[0]&0x3F) {
    case AP_FIXED: v_char[i]=elem_def[d.element].app[1]; break;
    case AP_PARAM: v_char[i]=d.parameter; break;
    case AP_UNDER: v_char[i]=elem_def[d.element].app[1]; break;
    case AP_MISC1: v_char[i]=s.ext1?:elem_def[d.element].app[1]; break;
    case AP_MISC2: v_char[i]=s.ext2?:elem_def[d.element].app[1]; break;
    case AP_MISC3: v_char[i]=d.ext3?:elem_def[d.element].app[1]; break;
    case 0x20 ... 0x3F: v_char[i]=appearance_mapping[((elem_def[d.element].app[1]&0x7E)+((d.parameter>>(elem_def[d.element].app[0]&7))&((2<<((elem_def[d.element].app[0]>>3)&3))-1)))&0x7F]; break;
  }
  v_color[i]=elem_def[d.element].attrib&(A_OVER_COLOR|A_UNDER_COLOR)?col:d.color;
  if(elem_def[d.element].attrib&A_UNDER_BGCOLOR) v_color[i]=(v_color[i]&0x0F)|(col&0xF0);
}

void update_screen(void) {
  int i;
  Uint32 v,x,y;
  Uint8 cmd,col,chr;
  memset(v_font,cur_screen.flag&SF_ALT_MODE?VF_ALTERNATE:0,80*25);
  for(i=0;i<80*25;i++) {
    cmd=cur_screen.command[i];
    col=cur_screen.color[i];
    chr=cur_screen.parameter[i];
    switch(cmd&0xF0) {
      case SC_BACKGROUND:
        if(cmd&1) v_char[i]=chr;
        if(cmd&2) v_color[i]=col;
        break;
      case SC_BOARD:
        draw_tile((i%80)+scroll_x,(i/80)+scroll_y,i,cmd&0x0F);
        break;
      case SC_NUMERIC:
        v_char[i]=digit_of(status_vars[cmd&15],chr);
        v_color[i]=col;
        break;
      case SC_NUMERIC_SPECIAL:
        switch(cmd) {
          case SC_SPEC_PLAYER_X: v=stats->count?stats->xy->x:0; break;
          case SC_SPEC_PLAYER_Y: v=stats->count?stats->xy->y:0; break;
          case SC_SPEC_CAMERA_X: v=scroll_x-cur_screen.view_x; break;
          case SC_SPEC_CAMERA_Y: v=scroll_y-cur_screen.view_y; break;
          case SC_SPEC_CURRENT_BOARD: v=cur_board_id; break;
          case SC_SPEC_EXIT_E: v=board_info.exits[DIR_E]; break;
          case SC_SPEC_EXIT_N: v=board_info.exits[DIR_N]; break;
          case SC_SPEC_EXIT_W: v=board_info.exits[DIR_W]; break;
          case SC_SPEC_EXIT_S: v=board_info.exits[DIR_S]; break;
          case SC_SPEC_WIDTH: v=board_info.width; break;
          case SC_SPEC_HEIGHT: v=board_info.height; break;
          case SC_SPEC_USERDATA: v=board_info.userdata; break;
          default: continue; // not applicable in this context (e.g. some that are only for text windows), so ignore it
        }
        v_char[i]=digit_of(v,chr);
        v_color[i]=col;
        break;
      case SC_MEMORY:
        v_char[i]=memory[(col<<8)|chr];
        v_color[i]=memory[(col<<8)|chr]>>8;
        break;
      case SC_INDICATOR:
        v_color[i]=col;
        x=i%80; y=i/80;
        switch(cmd) {
          case SC_IND_USERDATA: v_char[i]=board_info.userdata?chr:0; break;
          case SC_IND_CURSOR: v_char[i]=(stats->count && (x+scroll_x==stats->xy->x || y+scroll_y==stats->xy->y))?chr:0; break;
          case SC_IND_SCROLL_Y: v_char[i]=((y<cur_screen.view_y)?(scroll_y>cur_screen.hard_edge[DIR_N]):(scroll_y+board_info.height<cur_screen.hard_edge[DIR_S]))?chr:0; break;
          case SC_IND_SCROLL_X: v_char[i]=((x<cur_screen.view_x)?(scroll_x>cur_screen.hard_edge[DIR_W]):(scroll_x+board_info.width<cur_screen.hard_edge[DIR_E]))?chr:0; break;
          case SC_IND_EXIT_E: v_char[i]=board_info.exits[DIR_E]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_E]:0; break;
          case SC_IND_EXIT_N: v_char[i]=board_info.exits[DIR_N]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_N]:0; break;
          case SC_IND_EXIT_W: v_char[i]=board_info.exits[DIR_W]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_W]:0; break;
          case SC_IND_EXIT_S: v_char[i]=board_info.exits[DIR_S]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_S]:0; break;
          case SC_IND_USER0: v_char[i]=board_info.flag&BF_USER0?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[0]:0; break;
          case SC_IND_USER1: v_char[i]=board_info.flag&BF_USER1?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[1]:0; break;
          case SC_IND_USER2: v_char[i]=board_info.flag&BF_USER2?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[2]:0; break;
          case SC_IND_USER3: v_char[i]=board_info.flag&BF_USER3?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[3]:0; break;
        }
        break;
      case SC_TEXT:
        // Used only for text windows
        break;
      case SC_ITEM:
        if(cmd==SC_ITEM_ELEMENT) display_item_element_cell(i,col,chr>>5,chr&0x1F);
        break;
      case SC_BITS_0_LO ... SC_BITS_3_HI:
        v_color[i]=col;
        v=status_vars[(cmd-SC_BITS_0_LO)>>5];
        v_char[i]=(v&(1UL<<(cmd&0x1F))?chr:32);
        break;
    }
  }
}

StatXY*add_statxy(int n) {
  Stat*s=stats+n-1;
  StatXY*r;
  if(!editor && v_status[1] && v_status[1]!=32) errx(1,"Cannot add stat instances at this time");
  if(s->count>=0xFFFE) errx(1,"Too many stat instances");
  s->xy=realloc(s->xy,++s->count*sizeof(StatXY));
  if(!s->xy) errx(1,"Allocation failed");
  r=s->xy+s->count-1;
  r->x=r->y=r->instptr=0;
  r->layer=r->delay=0;
  r->sensor=(Tile){};
  r->frame=r->extra=0;
  return r;
}

static StatXY*get_statxy(Uint32 n) {
  Stat*s;
  if(!(n&0xFFFF) || (n&0xFFFF)>maxstat) return 0;
  s=stats+(n&0xFFFF)-1;
  n>>=16;
  return (n<s->count?s->xy+n:0);
}

static StatXY*find_statxy(const Tile*at) {
  Stat*s;
  StatXY*r;
  Sint32 z=(at-b_under)/(board_info.width*board_info.height)+1;
  Sint32 x=(at-b_under)%board_info.width;
  Sint32 y=((at-b_under)/board_info.width)%board_info.height;
  Uint32 n;
  if(!at->stat || at->stat>maxstat) return 0;
  s=stats+at->stat-1;
  for(n=0;n<s->count;n++) {
    r=s->xy+n;
    if(r->x==x && r->y==y && (r->layer&0x23)==z) return r;
  }
  return 0;
}

static void step_on_sensor_stat(Uint32 at) {
  StatXY*q=find_statxy(b_main+at);
  if(q) q->layer|=0x20;
}

static void restore_sensor_stat(Uint32 at) {
  Stat*s;
  StatXY*r;
  Sint32 x=at%board_info.width;
  Sint32 y=at/board_info.width;
  Uint32 n=b_main[at].stat;
  if(!n || n>maxstat) return;
  s=stats+n-1;
  for(n=0;n<s->count;n++) {
    r=s->xy+n;
    if(r->x==x && r->y==y && (r->layer&0x23)==0x22) {
      r->layer&=0xDF;
      return;
    }
  }
}

static void move_sensor_stat(Uint8 nn,Sint32 x0,Sint32 y0,Sint32 x1,Sint32 y1) {
  Stat*s;
  StatXY*r;
  Uint32 n;
  if(!nn || nn>maxstat) return;
  s=stats+nn-1;
  for(n=0;n<s->count;n++) {
    r=s->xy+n;
    if(r->x==x0 && r->y==y0 && (r->layer&0x23)==0x22) {
      r->x=x1; r->y=y1;
      return;
    }
  }
}

static void destroy_sensor_stat(Uint8 nn,Uint16 x,Uint16 y) {
  Stat*s;
  StatXY*r;
  Uint32 n;
  if(!nn || nn>maxstat) return;
  s=stats+nn-1;
  for(n=0;n<s->count;n++) {
    r=s->xy+n;
    if(r->x==x && r->y==y && (r->layer&0x23)==0x22) {
      r->x=r->y=r->instptr=65535;
      r->layer=128; r->delay=255;
      r->sensor=(Tile){};
      return;
    }
  }
}

static void kill_stat(int ns,int nr) {
  Stat*s=stats+ns-1;
  StatXY*r=s->xy+nr;
  r->x=r->y=r->instptr=65535;
  r->layer=128;
  r->delay=255;
  // Later it is noticed and deleted from the stat XY list
}

static inline void calc_light(Uint8 sh,Sint32 r) {
  Uint16 a=memory[MEM_LIGHT];
  int i,j;
  if(a>=65486) return;
  memset(memory+a,0,49*sizeof(*memory));
  // Data format: [a+24+vertical] ((Left+128)<<8)|(Right+128)
  // where Left/Right is relative to player's X coordinate
  switch(sh) {
    case 1: // box / Chebyshev
      if(r>24) r=24;
      for(i=a+24-r;i<=a+24+r;i++) memory[i]=((128-r)<<8)|(128+r);
      break;
    case 2: // circle / Euclid
      if(r>255) r=255;
      r*=r;
      for(i=0;i<49;i++) if((j=r-(24-i)*(24-i))>0) {
        j=sqrt(j)+0.5;
        if(j>127) j=127;
        memory[a+i]=((128-j)<<8)|(128+j);
      }
      break;
    case 3: // diamond / Manhattan
      if(r>127) r=127;
      for(i=0;i<49;i++) if((j=abs(24-i))<=r) memory[a+i]=((128+j-r)<<8)|(128+r-j);
      break;
  }
}

static void do_text_op(Uint8 op,Sint32 n) {
  int i,j,k;
  char buf[81];
  const char*s=buf;
  if(op==4) ntextbuf=0,op=6;
  *buf=0;
  switch(op) {
    case 0: // Packed string
      n&=0xFFFF;
      for(i=0;i<80 && n<0x10000;n++) {
        buf[i++]=memory[n];
        buf[i++]=memory[n++]>>8;
        if(!buf[i-2] || !buf[i-1]) break;
      }
      break;
    case 1: // Board title
      if(boardnames && n>=0 && n<=maxboard && boardnames[n]) s=(char*)boardnames[n];
      break;
    case 2: // Single character
      *buf=n; buf[1]=0;
      break;
    case 3: // Decimal
      snprintf(buf,80,"%ld",(long)n);
      break;
    case 5: // Formatted
      for(i=0,j=memory[MEM_ARG_J];j<0x10000;j++) {
        for(k=(memory[j]>>4)&15;i<80;) {
          if(op=digit_of(n,((memory[j]>>4)&0xF0)+k)) {
            buf[i++]=op;
          } else {
            op=(memory[j]>>12)&7;
            if(op==1) buf[i++]=0x20;
            if(op==2) break;
            if(op==3) goto stop;
            if(op==4) buf[i++]=memory[MEM_ARG_K]?:0x20;
            if(op==5) memory[MEM_ARG_K]++;
            if(op>5) condflag=op&1;
          }
          if(k==(memory[j]&15)) break;
          if(((memory[j]>>4)&15)<(memory[j]&15)) k++; else k--;
        }
        if(memory[j]&0x8000) break;
      }
      break;
    case 6: // Global
      if(n>0 && n<ngtext) s=(char*)gtext[n];
      if(n<0 && n>=-ndynastr) s=(char*)dynastr[~n].text;
      if(n<-255 && n>=-271) s=(char*)namedflag[-255-n].name;
      break;
    case 7: // Hexadecimal
      snprintf(buf,80,"%08lX",(unsigned long)n);
      break;
  }
  stop:
  n=snprintf(textbuf+ntextbuf,81-ntextbuf,"%s",s);
  if(n+ntextbuf<80) ntextbuf+=n; else ntextbuf=80;
}

static Sint32 xop_special(Sint32 so,Uint16 ex) {
  switch(ex&0x0FF0) {
    case XOP_S_EVENT: return elem_def[so&255].event[ex&15];
    case XOP_S_STATUS_PLUS: return status_vars[ex&15]+so;
    case XOP_S_STATUS_MINUS: return status_vars[ex&15]-so;
    case XOP_S_WIDTH: return board_info.width+so+(ex&15)-8;
    case XOP_S_HEIGHT: return board_info.height+so+(ex&15)-8;
    case XOP_S_PLAYER_X: return stats->count?stats->xy->x+(ex&15)-8-so:0;
    case XOP_S_PLAYER_Y: return stats->count?stats->xy->y+(ex&15)-8-so:0;
    case XOP_S_BOARD_ID: return cur_board_id;
    case XOP_S_SCROLL_X: return scroll_x+cur_screen.view_x+so+(ex&15)-8;
    case XOP_S_SCROLL_Y: return scroll_y+cur_screen.view_y+so+(ex&15)-8;
    case XOP_S_IF_TRUE: return condflag?so:(ex&8?regs[ex&7]:(ex&7)-3);
    case XOP_S_IF_FALSE: return condflag?(ex&8?regs[ex&7]:(ex&7)-3):so;
    default: return 0;
  }
}

static void save_registers(Uint16 f,Uint16 s) {
  int n;
  f++;
  if(f>8 || s+f+f>0x10000) return;
  for(n=0;n<f;n++) {
    regs[n]=memory[s++]<<16;
    regs[n]|=memory[s++];
  }
}

static void load_registers(Uint16 f,Uint16 s) {
  int n;
  f++;
  if(f>8 || s+f+f>0x10000) return;
  for(n=0;n<f;n++) {
    memory[s++]=regs[n]>>16;
    memory[s++]=regs[n];
  }
}

static Sint32 convxy(Sint32 xy,Sint32 x,Sint32 y) {
  if(!xy) {
    if(x<0 || x>=board_info.width || y<0 || y>=board_info.height) return -1;
    return y*board_info.width+x;
  }
  if(xy>0 && xy<=board_info.width*board_info.height) return xy-1;
  return -1;
}

static Uint32 statxy_index_at(Sint32 xy,Uint8 lay,const Tile*ti) {
  Stat*s;
  Uint32 n;
  Sint32 x,y;
  if(xy==-1) return 0;
  ti+=xy;
  if(!ti->stat || ti->stat>maxstat) return 0;
  x=xy%board_info.width;
  y=xy/board_info.width;
  s=stats+ti->stat-1;
  for(n=0;n<s->count;n++) if(s->xy[n].x==x && s->xy[n].y==y && (s->xy[n].layer&0x23)==lay) return ti->stat+(n<<16);
  return 0;
}

static Sint32 scan_board(Sint32 xy,Uint8 k,Sint32 x,Sint32 y) {
  Uint32 z;
  condflag=1;
  if(z=xy) {
    if(xy<0) z=0; else if(z>=board_info.width*board_info.height) z=condflag=0;
  } else {
    if(x<0) x=0; else x++;
    if(x>=board_info.width) y++,x=0;
    if(y<0) x=y=0;
    if(y>=board_info.height) x=y=0,condflag=0;
    z=y*board_info.width+x;
  }
  again:
  while(z<board_info.width*board_info.height) if(b_main[z++].kind==k) return z;
  if(condflag) {
    z=condflag=0;
    goto again;
  }
  return 0;
}

static void do_change_stat(Uint16 f,Uint8 os,Uint8 lay,Uint32 x,Uint32 y) {
  Uint32 i;
  Stat*s;
  StatXY*u;
  StatXY r={.x=x,.y=y};
  if(os && os<=maxstat) {
    s=stats+os-1;
    for(i=0;i<s->count;i++) {
      u=s->xy+i;
      if((u->layer&0x23)==lay && u->x==x && u->y==y) {
        r=*u;
        kill_stat(os,i);
        break;
      }
    }
  }
  if(i=(lay==1?b_under:lay==2?b_main:b_over)[y*board_info.width+x].stat) {
    if(i!=os) r.instptr=r.frame=0;
    os=i;
    if(os>maxstat) return;
    if(f&0x8000) r.delay=(f>>8)&0x7F;
    if(f&0xC0) r.layer=lay|(r.layer&0xC0&~f); else r.layer=lay|(r.layer&0xFC);
    *add_statxy(os)=r;
  }
}

static Sint32 do_change(Uint8 how,Uint8 b,Uint32 a) {
  Sint32 n=0;
  Uint32 x,y,z;
  Uint16 f=memory[a&0xFFFF];
  Tile m,mm,r,rm;
  int i,j;
  for(i=0;i<4;i++) m.values[i]=memory[(a+i+1)&0xFFFF],mm.values[i]=memory[(a+i+1)&0xFFFF]>>8;
  if(how) for(i=0;i<4;i++) r.values[i]=memory[(a+i+5)&0xFFFF],rm.values[i]=memory[(a+i+5)&0xFFFF]>>8;
  i=(f>>3)&7;
  if(i&3) {
    if(i&4) r.values[i&3]+=b; else m.values[i&3]+=b;
  } else if(i==4) {
    how+=3;
  }
  for(i=0;i<4;i++) m.values[i]|=mm.values[i];
  if(how==2) for(i=0;i<4;i++) r.values[i]|=rm.values[i];
  if(how && r.kind==245 && !rm.kind && !rm.param && (f&7)!=4) how+=12;
  if(m.kind==245 && !mm.kind && !mm.param && (f&7)!=4) how+=6;
  z=board_info.width*board_info.height;
  switch(f&7) {
    case 0: if(how!=19 && how!=6) return 0;
    case 1: a=0; break;
    case 2: a=z; z+=z; break;
    case 3: a=0; z+=z; break;
    case 4: a=z+z; z+=a; break;
    case 5: memory[a&0xFFFF]=f-1; do_change(how,b,a); memory[a&0xFFFF]=f; a=0; break;
    case 6: a=z; z+=z+z; break;
    case 7: a=0; z+=z+z; break;
  }
  switch(how) {
    case 0: for(;a<z;a++) {
      for(i=0;i<4;i++) if((b_under[a].values[i]|mm.values[i])!=m.values[i]) goto skip0;
      n++;
      skip0: ;
    } break;
    case 1: for(;a<z;a++) {
      for(i=0;i<4;i++) if((b_under[a].values[i]|mm.values[i])!=m.values[i]) goto skip1;
      j=b_under[a].stat;
      for(i=0;i<4;i++) b_under[a].values[i]=r.values[i]^(b_under[a].values[i]&rm.values[i]);
      if(j || r.stat) do_change_stat(f,j,a/(board_info.width*board_info.height)+1,a%board_info.width,(a/board_info.width)%board_info.height);
      skip1: ;
    } break;
    case 2: for(;a<z;a++) {
      for(i=0;i<4;i++) if((b_under[a].values[i]^m.values[i])&~mm.values[i]) goto skip2a;
      j=b_under[a].stat;
      for(i=0;i<4;i++) b_under[a].values[i]=(b_under[a].values[i]&rm.values[i])|(b_under[a].values[i]&~rm.values[i]);
      if(j || r.stat) do_change_stat(f,j,a/(board_info.width*board_info.height)+1,a%board_info.width,(a/board_info.width)%board_info.height);
      continue;
      skip2a:
      for(i=0;i<4;i++) if((b_under[a].values[i]^r.values[i])&~rm.values[i]) goto skip2b;
      j=b_under[a].stat;
      for(i=0;i<4;i++) b_under[a].values[i]=(b_under[a].values[i]&mm.values[i])|(b_under[a].values[i]&~mm.values[i]);
      if(j || m.stat) do_change_stat(f,j,a/(board_info.width*board_info.height)+1,a%board_info.width,(a/board_info.width)%board_info.height);
      skip2b: ;
    } break;
    case 3: for(;a<z;a++) {
      if(((elem_def[b_under[a].kind].attrib>>24)|mm.kind)!=m.kind) goto skip3;
      for(i=1;i<4;i++) if((b_under[a].values[i]|mm.values[i])!=m.values[i]) goto skip3;
      n++;
      skip3: ;
    } break;
    case 4: for(;a<z;a++) {
      if(((elem_def[b_under[a].kind].attrib>>24)|mm.kind)!=m.kind) goto skip4;
      for(i=1;i<4;i++) if((b_under[a].values[i]|mm.values[i])!=m.values[i]) goto skip4;
      j=b_under[a].stat;
      for(i=0;i<4;i++) b_under[a].values[i]=r.values[i]^(b_under[a].values[i]&rm.values[i]);
      if(j || r.stat) do_change_stat(f,j,a/(board_info.width*board_info.height)+1,a%board_info.width,(a/board_info.width)%board_info.height);
      skip4: ;
    } break;
    case 6: for(y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++) {
      if((b_under[a].color|mm.color)==m.color && (b_under[a].stat|mm.stat)==m.stat && in_zone(x,y,m.param)) n++;
      a++;
    } break;
    case 7: for(y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++) {
      if((elem_def[b_under[a].kind].attrib&A_PERMANENT) && b_under[a].kind!=(r.kind^b_under[a].kind&rm.kind)) goto skip7;
      if((b_under[a].color|mm.color)!=m.color || (b_under[a].stat|mm.stat)!=m.stat || !in_zone(x,y,m.param)) goto skip7;
      j=b_under[a].stat;
      for(i=0;i<4;i++) b_under[a].values[i]=r.values[i]^(b_under[a].values[i]&rm.values[i]);
      if(r.stat==255 && rm.stat==255) goto skip7;
      if(j || r.stat) do_change_stat(f,j,a/(board_info.width*board_info.height)+1,a%board_info.width,(a/board_info.width)%board_info.height);
      skip7: a++;
    } break;
    case 13: for(y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++) {
      for(i=0;i<4;i++) if((b_under[a].values[i]|mm.values[i])!=m.values[i]) goto skip13;
      zone_add(x,y,r.param,1);
      skip13: a++;
    } break;
    case 19: for(y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++) {
      if((b_under[a].color|mm.color)==m.color && (b_under[a].stat|mm.stat)==m.stat && in_zone(x,y,m.param)) zone_add(x,y,r.param,1);
      a++;
    } break;
  }
  return n;
}

static inline Uint32 pack_tile(const Tile*t) {
  return t->kind|(t->color<<8)|(t->param<<16)|(t->stat<<24);
}

static Uint32 general_move(Uint8 pushing,Uint32 at,Sint32 xx,Sint32 yy,Uint16 flag,Uint16 cla,Sint32 tx,Sint32 ty) {
  // Flags:
  //   0x0001 = absolute
  //   0x0002 = overlay
  //   0x0004 = return stat
  //   0x0008 = do not move
  //   0x0010 = allow pushing
  //   0x0020 = allow direct crushing
  //   0x0040 = allow transporting
  //   0x0080 = override move classes
  //   0x0700 = Y step register
  //   0x0800 = direction instead of displacement
  //   0x7000 = X step register
  //   0x8000 = use registers instead of tx/ty
  Tile*b;
  StatXY*qq=0;
  StatXY*q;
  Uint8 sn=0;
  Uint16 sr=0;
  Sint32 rx,ry;
  Uint32 e0,e1,e2;
  Uint32 to;
  Sint32 tto=-1;
  Sint32 i;
  condflag=0;
  // Determine coordinates
  if(flag&4) {
    sn=at&0xFF;
    sr=at>>16;
    if(sn>maxstat || !sn || sr>=stats[sn-1].count || (stats[sn-1].mode&STAT_INDEPENDENT)) return 0;
    qq=stats[sn-1].xy+sr;
    i=qq->layer&3;
    if(i==3) flag|=2; else if(i==2) flag&=~2; else return 0;
    xx=qq->x;
    yy=qq->y;
    goto xy0;
  } else if(at) {
    if(at>board_info.width*board_info.height) return at;
    --at;
    xx=at%board_info.width;
    yy=at/board_info.width;
  } else {
    xy0:
    if(xx<0 || xx>=board_info.width || yy<0 || yy>=board_info.height) return 0;
    at=yy*board_info.width+xx;
  }
  // Determine direction of movement and target location
  if(flag&0x8000) tx=regs[(flag>>8)&7],ty=regs[(flag>>12)&7];
  if(flag&0x800) {
    tx&=3; ty&=3;
    if(tx==DIR_E) rx=1; else if(tx==DIR_W) rx=-1; else rx=0;
    if(ty==DIR_S) ry=1; else if(ty==DIR_N) ry=-1; else ry=0;
    tx=rx+xx; ty=ry+yy;
  } else if(flag&1) {
    rx=tx-xx; ry=ty-yy;
  } else {
    rx=tx; ry=ty;
    tx+=xx; ty+=yy;
  }
  if(tx<0 || tx>=board_info.width || ty<0 || ty>=board_info.height) goto end;
  if(!rx && !ry) goto end;
  to=ty*board_info.width+tx;
  try_again:
  // Determine attributes
  if(flag&2) {
    b=b_over;
    e0=elem_def[240].attrib&~(A_SENSOR|A_TRANSPORTABLE);
    e1=(b[to].kind&OVER_SOLID)?(e0&~(A_FLOOR|A_PUSH_EW|A_PUSH_NS)):(e0|A_FLOOR);
  } else {
    b=b_main;
    e0=elem_def[b[at].kind].attrib; e1=elem_def[b[to].kind].attrib;
    if(!(flag&0x80)) cla=(cla&0xFF00)|((e0/A_MOVE_C0)&0xFF);
  }
  // Check classes
  if(!(cla&(1<<(e1&15)))) goto end;
  // Check push direction
  if(pushing) {
    if(rx && !(e0&A_PUSH_EW)) goto end;
    if(ry && !(e0&A_PUSH_NS)) goto end;
  }
  // Find stat record if necessary
  if(b[at].stat && !sn && (qq=find_statxy(b+at))) sr=qq-stats[(sn=b[at].stat)-1].xy;
  if(sn && (stats[sn-1].mode&STAT_ZONERESTRICT) && !in_zone(tx,ty,stats[sn-1].zone)) goto end;
  // Sensors
  if(qq && (e1&A_SENSOR&~e0)) {
    if(qq->sensor.kind) {
      // Trying to move from one sensor to another sensor
      if(!pushing && run_program(elem_def[b[to].kind].event[EV_SENSOR],sn|(sr<<16),tx,ty,(flag&0xF8)|(((Uint32)cla)<<16)|(b[at].kind<<8)|0x01)) {
        Tile tt=b[at];
        b[at]=qq->sensor;
        if(qq->sensor.stat) restore_sensor_stat(at);
        qq->sensor=b[to];
        if(qq->sensor.stat) step_on_sensor_stat(to);
        b[to]=tt;
        goto sensorok;
      }
    } else if(run_program(elem_def[b[to].kind].event[EV_SENSOR],sn|(sr<<16),tx,ty,(flag&0xF8)|(((Uint32)cla)<<16)|(b[at].kind<<8))) {
      if(!(flag&8)) {
        qq->sensor=b[to];
        if(qq->sensor.stat) step_on_sensor_stat(to);
        b[to]=b[at];
        if(b_under[at].stat && (q=find_statxy(b_under+at))) q->layer++;
        b[at]=b_under[at];
        b_under[at]=(Tile){};
        sensorok:
        qq->x=tx;
        qq->y=ty;
      }
      condflag=1;
      return (flag&4)?(sn|(sr<<16)):(to+1);
    }
    condflag=0;
  }
  // Transporting
  if((flag&0x40) && (e0&A_TRANSPORTABLE) && (elem_def[b[to].kind].event[EV_TRANSPORT])) {
    transport:
    condflag=(flag>>3)&1;
    if(tto=run_program(elem_def[b[to].kind].event[EV_TRANSPORT],(rx>0?DIR_E:rx<0?DIR_W:ry>0?DIR_S:DIR_N),tx,ty,tto+1)) {
      --tto;
      if(tto<0 || tto>=board_info.width*board_info.height) {
        condflag=0;
        goto end;
      }
      e2=elem_def[b[tto].kind].attrib;
      if(!(cla&(1<<(e2&15)))) goto transport;
      // Push while transporting (but cannot crush through transporters directly)
      if((flag&0x10) && (e2&(A_PUSH_EW|A_PUSH_NS))) {
        if(i=elem_def[b[tto].kind].event[EV_PUSH]) {
          condflag=(flag>>3)&1;
          if(run_program(i,(rx>0?DIR_E:rx<0?DIR_W:ry>0?DIR_S:DIR_N),tto%board_info.width,tto/board_info.width,b[tto].param+256)) goto tnopush;
        }
        general_move(1,tto+1,0,0,0x0070|flag&0x0078,cla,rx,ry);
        if(condflag && (flag&8)) e2=elem_def[b_under[tto].kind].attrib; else e2=elem_def[b[tto].kind].attrib;
        if(!(cla&(1<<(e2&15)))) goto transport;
      }
      tnopush:
      if(!(e2&A_FLOOR)) goto transport;
      // Transport is OK
      if(memory[MEM_TRANSPORT_EVENT]) {
        condflag=(flag>>3)&1;
        i=run_program(memory[MEM_TRANSPORT_EVENT],at+1,tto+1,to+1,cla);
        if(i<0) goto transport;
        if(i>0) {
          at=i-1;
          goto end;
        }
      }
      to=tto;
      tx=to%board_info.width;
      ty=to/board_info.width;
      condflag=0;
      goto bypass;
    } else {
      flag&=~0x40;
    }
    condflag=0;
  }
  // Push other objects out of the way
  if((flag&0x10) && (e1&(A_PUSH_EW|A_PUSH_NS))) {
    if(i=elem_def[flag&2?240:b[to].kind].event[EV_PUSH]) {
      condflag=(flag>>3)&1;
      i=run_program(i,(rx>0?DIR_E:rx<0?DIR_W:ry>0?DIR_S:DIR_N),tx,ty,b[to].param);
      condflag=0;
      if(i) goto nopush;
    }
    general_move(1,to+1,tx,ty,0x0070|flag&0x007A,cla,rx,ry);
    nopush:
    flag&=~0x10;
    if(condflag) {
      flag&=~0x20;
      condflag=0;
      if(flag&8) {
        if(flag&2) e1|=A_FLOOR; else b=b_under,e1=elem_def[b[to].kind].attrib;
      } else {
        goto try_again;
      }
    }
  }
  // Crushing
  if((flag&0x20) && (e1&A_CRUSH)) {
    if(flag&8) goto bypass;
    if(b[to].stat && (q=find_statxy(b+to))) {
      q->x=q->y=q->instptr=65535;
      q->layer=128;
      q->delay=255;
    }
    if(flag&2) {
      b[to].kind=0;
      b[to].stat=0;
      e1=A_FLOOR;
    } else {
      if(b_under[to].stat && (q=find_statxy(b_under+to))) q->layer++;
      b_main[to]=b_under[to];
      b_under[to]=(Tile){};
      flag|=0x80;
      goto try_again;
    }
  }
  // Check if the target location is blocked
  if(!(e1&A_FLOOR)) goto end;
  bypass:
  // Check if a stat would fall beneath the under layer
  if((flag&2) && b[at].stat && b[to].stat) goto end;
  if(!(flag&2) && b_under[to].stat) goto end;
  // Movement is OK
  if(!(flag&8)) {
    // Do movement
    if(!(flag&2)) {
      b_under[to]=b_main[to];
      if(b_under[to].stat && (q=find_statxy(b_under+to))) q->layer--;
    } else if(b[at].stat && (memory[MEM_CONTROL]&CONTROL_OVERLAY_SENSOR) && qq) {
      Tile ti=b[to];
      b[to]=b[at];
      if(memory[MEM_OVERLAY_KEEP_BITS]) {
        b[to].kind=(b[to].kind&~(memory[MEM_OVERLAY_KEEP_BITS]>>8))|(ti.kind&(memory[MEM_OVERLAY_KEEP_BITS]>>8));
        b[to].color=(b[to].color&~memory[MEM_OVERLAY_KEEP_BITS])|(ti.color&memory[MEM_OVERLAY_KEEP_BITS]);
      }
      b[at]=qq->sensor;
      qq->sensor=ti;
      goto setxy;
    }
    b[to]=b[at];
    if(flag&2) {
      b[to].stat|=b[at].stat;
      b[at].kind=(b[at].kind&OVER_BG_THRU)|(memory[MEM_DEFAULT_OVERLAY]?OVER_VISIBLE:0);
      b[at].color=memory[MEM_DEFAULT_OVERLAY]>>8;
      b[at].param=memory[MEM_DEFAULT_OVERLAY];
      b[at].stat=0;
    } else if(qq && qq->sensor.kind && !pushing) {
      b_main[at]=qq->sensor;
      if(qq->sensor.stat) restore_sensor_stat(at);
      qq->sensor=(Tile){};
    } else {
      if(b_under[at].stat && (q=find_statxy(b_under+at))) q->layer++;
      b_main[at]=b_under[at];
      b_under[at]=(Tile){};
    }
    if(qq) {
      if(qq->sensor.stat && qq->sensor.kind) move_sensor_stat(qq->sensor.stat,qq->x,qq->y,tx,ty);
      setxy: qq->x=tx; qq->y=ty;
    }
    at=to;
  }
  condflag=1;
  end: return (flag&4)?(sn|(sr<<16)):(at+1);
}

static void break_tile(Sint32 at,Uint8 lay,Uint16 sn,Uint16 sr,Uint8 f) {
  StatXY*q=0;
  StatXY*z;
  Tile*t;
  if(sn) {
    if(sn>maxstat || sr>=stats[sn-1].count) return;
    q=stats[sn-1].xy+sr;
    if(stats[sn-1].mode&STAT_INDEPENDENT) goto die;
    if(q->layer&0x20) goto die;
    lay=q->layer&3;
    if(!lay) goto die;
    at=convxy(0,q->x,q->y);
    if(at==-1) goto die;
  }
  if(lay==1) {
    t=b_under+at;
    if(!sn && (sn=t->stat)) q=find_statxy(t);
    if(!f) *t=(Tile){};
  } else if(lay==2) {
    t=b_main+at;
    if(!sn && (sn=t->stat)) q=find_statxy(t);
    if(q && q->sensor.kind) {
      if(!f) {
        b_main[at]=q->sensor;
        if(b_main[at].stat) restore_sensor_stat(at);
      } else if(q->sensor.stat) {
        destroy_sensor_stat(q->sensor.stat,q->x,q->y);
      }
    } else if(!f) {
      if(b_under[at].stat && (z=find_statxy(b_under+at))) z->layer++;
      *t=b_under[at];
      b_under[at]=(Tile){};
    }
  } else if(lay==3) {
    t=b_over+at;
    if(!sn && (sn=t->stat)) q=find_statxy(t);
    if(!f) {
      t->kind&=OVER_BG_THRU;
      if(memory[MEM_DEFAULT_OVERLAY]) t->kind|=OVER_VISIBLE;
      t->color=memory[MEM_DEFAULT_OVERLAY]>>8;
      t->param=memory[MEM_DEFAULT_OVERLAY];
    }
  }
  if(q) {
    if(f && sn==t->stat) t->stat=0;
    die:
    q->x=q->y=q->instptr=65535;
    q->layer=128;
    q->delay=255;
    q->sensor=(Tile){};
  }
}

static int match_label(const char*v,const char*label) {
  int n=0;
  char a,b;
  for(;;) {
    a=v[n+1];
    b=label[n];
    if(!b || b=='\n' || b==' ' || b=='\r' || b==';' || b=='(' || b==')' || b==':') {
      if(!a || a=='\n' || a==' ' || a=='\r' || a==';' || a=='(' || a==')' || a=='*' || a=='=') {
        end_of_label=v+(++n);
        while(v[n] && v[n]!='\n') n++;
        if(v[n]=='\n') n++;
        return n;
      }
      return 0;
    }
    if(a>='a') a+='A'-'a';
    if(b>='a') b+='A'-'a';
    if(a!=b) return 0;
    n++;
  }
}

static int match_name(const char*v,const char*name) {
  int n=0;
  char a,b;
  if(*v++!='@') return 0;
  for(;;) {
    a=v[n];
    b=name[n];
    if(!b || b==':' || b=='=' || b==' ' || b=='<' || b=='\n' || b=='\r') {
      return (!a || a==':' || a=='=' || a==' ' || a=='<' || a=='\n' || a=='\r');
    }
    if(a>='a') a+='A'-'a';
    if(b>='a') b+='A'-'a';
    if(a!=b) return 0;
    n++;
  }
}

static Sint32 find_label(Stat*s,const char*label) {
  const char*v=(char*)s->text;
  int n;
  if(*v!=':') v=strstr(v,"\n:"),v+=(v?1:0);
  while(v) {
    if(n=match_label(v,label)) return (v-(const char*)s->text)+n;
    v=strstr(v,"\n:"),v+=(v?1:0);
  }
  return -1;
}

static Sint32 find_unzapped_label(Stat*s,const char*label) {
  const char*v=(char*)s->text;
  int n;
  if(*label=='!') {
    if(*v!='!') v=strstr(v,"\n!"),v+=(v?1:0);
    while(v) {
      if(n=match_label(v,label+1)) return (v-(const char*)s->text);
      v=strstr(v,"\n!"),v+=(v?1:0);
    }
  } else {
    if(*v!=':') v=strstr(v,"\n:"),v+=(v?1:0);
    while(v) {
      if(n=match_label(v,label)) return (v-(const char*)s->text);
      v=strstr(v,"\n:"),v+=(v?1:0);
    }
  }
  return -1;
}

static Sint32 find_zapped_label(Stat*s,const char*label) {
  const char*v=(char*)s->text;
  int n;
  if(*label=='!') {
    if(*v!='&') v=strstr(v,"\n&"),v+=(v?1:0);
    while(v) {
      if(n=match_label(v,label+1)) return (v-(const char*)s->text);
      v=strstr(v,"\n&"),v+=(v?1:0);
    }
  } else {
    if(*v!='\'') v=strstr(v,"\n'"),v+=(v?1:0);
    while(v) {
      if(n=match_label(v,label)) return (v-(const char*)s->text);
      v=strstr(v,"\n'"),v+=(v?1:0);
    }
  }
  return -1;
}

static void frame_push(Stat*s,StatXY*r,Uint8 c,Uint16 p) {
  /*
    Frame format:
    - '.' means free space.
    - 'C' means continue execution.
    - 'E' means end execution.
    - 'U' means unlock.
    - 'X' means stop (this is the end of this object's stack).
    - Small-endian 21-bit number, high bit set of each byte. High 5-bits=command, low 16-bits=parameter.
    0 = Return to address
    1 = Restore label
    2 = Go to frame
    3 = Set delay
    4 = Extra value
  */
  static const Uint8 dots[]="................";
  Uint8*t=s->text;
  Sint32 k;
  if(!s->frame) {
    allocate:
    end_of_label=0; // because this points into the script text, it is not used.
    if(s->length+16>=0xFFFD) errx(1,"Out of memory for script frames");
    t=realloc(t,s->length+17);
    if(!t) err(1,"Allocation failed");
    if(global_text==s->text) global_text=t,global_length=s->length+16;
    s->text=t;
    memcpy(t+s->length,s->frame?dots:(const Uint8*)"\n''.............",17);
    if(!s->frame) s->frame=s->length+3;
    s->length+=16;
  }
  if(!r->frame) r->frame=s->frame;
  if(!t[r->frame] || (c!='X' && r->frame==s->frame) || memcmp(t+r->frame,dots,c>31?3:1)) {
    // Find a free space
    for(k=s->frame;k<s->length;k++) if(!memcmp(t+k,dots,c>31?6:4)) goto found;
    r->frame=s->length;
    goto allocate;
    found:
    if(c!='X') {
      if(r->frame==s->frame) {
        t[k++]='X';
      } else {
        t[k++]=r->frame|0x80;
        t[k++]=(r->frame>>7)|0x80;
        t[k++]=((r->frame+0x20000)>>14)|0x80;
      }
    }
    r->frame=k;
  }
  if(c>31) {
    t[r->frame++]=c;
  } else {
    k=p+(c<<16);
    t[r->frame++]=k|0x80;
    t[r->frame++]=(k>>7)|0x80;
    t[r->frame++]=(k>>14)|0x80;
  }
}

static int frame_return(Stat*s,StatXY*r,int d) {
  // Return: 1=continue, 0=delay
  Uint8*t=s->text;
  Sint32 k;
  again:
  if(!r->frame || r->frame==s->frame) return d;
  if(r->frame>s->length || r->frame<s->frame || !s->frame) errx(1,"Improper script frame");
  if(t[r->frame-1]&0x80) {
    if(r->frame<s->frame+3 || !(t[r->frame-2]&t[r->frame-3]&0x80)) errx(1,"Improper script frame");
    k=(t[r->frame-1]<<14)+(t[r->frame-2]<<7)+t[r->frame-3]-0x204080;
    t[r->frame-1]=t[r->frame-2]=t[r->frame-3]='.';
    r->frame-=3;
    switch(k>>16) {
      case 0: r->instptr=k; break;
      case 1: k&=0xFFFF; if(k<s->length) t[k]=':'; break;
      case 2: r->frame=k&0xFFFF; break;
      case 3:
        if(d) return d;
        if(memory[MEM_RETURN_EVENT]) {
          k=run_program(memory[MEM_RETURN_EVENT],(s+1-stats)+(r-s->xy),0,0,k&0xFFFF);
          if(k<0) return 1;
        }
        r->delay=k&0xFFFF;
        return 0;
      case 4: r->extra=k&0xFFFF; break;
      default: errx(1,"Improper script frame");
    }
  } else {
    switch(t[r->frame-1]) {
      case 'C': t[--r->frame]='.'; return 1;
      case 'E': t[--r->frame]='.'; r->instptr=65535; return d;
      case 'U': t[--r->frame]='.'; r->layer&=0x7F; break;
      case 'X': t[r->frame-1]='.'; r->frame=0; return d;
      default: errx(1,"Improper script frame");
    }
  }
  goto again;
}

static Uint8 frame_options(void) {
  Uint8 h;
  const char*e=end_of_label;
  if(!e || *e++!='*') return 0;
  h=1;
  while(*e && *e!='\n' && *e!='=') switch(*e++) {
    case 'e': case 'E': h|=0x08; break;
    case 'l': case 'L': h|=0x02; break;
    case 'p': case 'P': h|=0x10; break;
    case 's': case 'S': h|=0x20; break;
    case 'z': case 'Z': h|=0x04; break;
  }
  return h;
}

static Uint8 send_message_to(Stat*s,StatXY*r,Sint32 f,Uint8 h) {
  Sint32 k;
  if(h) {
    if(r->instptr==65535) {
      if(r->frame && r->frame!=s->frame) frame_push(s,r,'E',0);
    } else {
      if(r->delay) frame_push(s,r,3,r->delay); else frame_push(s,r,'C',0);
      frame_push(s,r,0,r->instptr);
    }
    if(h&0x02) {
      if(!(r->layer&0x80)) frame_push(s,r,'U',0);
      r->layer|=0x80;
    }
    if(h&0x04) {
      for(k=f-1;k>=0 && s->text[k]!=':';k--);
      if(k>=0) {
        s->text[k]='\'';
        frame_push(s,r,1,k);
      }
    }
    if(h&0x08) frame_push(s,r,4,r->extra);
    r->delay=0;
  } else if(memory[MEM_CONTROL]&CONTROL_DELAY0_SEND) {
    r->delay=0;
  }
  r->instptr=f;
  memory[MEM_CONTROL]|=CONTROL_SENT;
  return h&4;
}

static void send_message(Uint32 n,const char*label,Uint8 ignlock) {
  Uint8 h=0;
  const char*p;
  const char*q=strchr(label,':');
  StatXY*r;
  Stat*s;
  Sint32 f;
  int m;
  if(q || !n) {
    if(q) p=label,label=q+1; else p=0;
    for(n=0;n<maxstat;n++) if(stats[n].length) {
      s=stats+n;
      if(p && !match_name(s->text,p)) continue;
      f=find_label(s=stats+n,label);
      if(f!=-1) {
        h=frame_options();
        for(m=0;m<s->count;m++) if(!(s->xy[m].layer&(h&0x10?0x20:0xA0)) && send_message_to(s,s->xy+m,f,h) && ((h&0x20) || (f=find_label(s,label))==-1)) break;
      }
    }
  } else if(r=get_statxy(n)) {
    if(r->layer&0x20) return;
    if(*label=='*') ignlock=0,++label; else if((r->layer&0x80) && !ignlock) return;
    f=find_label(s=stats+(n&0xFF)-1,label);
    if(f!=-1) send_message_to(s,r,f,frame_options());
  }
}

static void send_message_at(Uint8 lay,Uint32 x,Uint32 y,const char*label,Uint8 ignlock) {
  StatXY*o;
  Sint32 f;
  Tile*t;
  if(!lay || x>=board_info.width || y>=board_info.height || !label) return;
  t=(lay==1?b_under:lay==2?b_main:b_over)+y*board_info.width+x;
  if(!t->stat || t->stat>maxstat || !stats[t->stat].text) return;
  if(o=find_statxy(t)) {
    if(ignlock && (o->layer&0x80)) return;
    f=find_label(stats+t->stat-1,label);
    if(f!=-1) send_message_to(stats+t->stat-1,o,f,frame_options());
  }
}

static void script_error(Uint16 m,StatXY*xy,const char*text) {
  fprintf(stderr,"Script error in stat %d at offset %d: %s\n",m,xy->instptr,text);
  xy->instptr=65535;
}

static void add_message_text(void) {
  if(!config.message_scrollback) return;
  if(!scrback) {
    scrback=calloc(config.message_scrollback,sizeof(MessageScrollback));
    if(!scrback) return;
  }
  memcpy(scrback[nscrback].text,vtextbuf,nvtextbuf+1);
  if(++nscrback==config.message_scrollback) nscrback=0;
}

static char load_help_file(const Uint8*name) {
  FILE*fp;
  char buf[82];
  int i,c;
  free(textfile_text);
  textfile=0;
  textfile_text=0;
  textfile_size=0;
  for(i=0;i<8;i++) {
    if(name[i]=='.' || name[i]==';' || name[i]<39) break;
    buf[i]=name[i];
  }
  buf[i]='.'; buf[i+1]='H'; buf[i+2]='L'; buf[i+3]='P'; buf[i+4]=0;
  fp=open_lump(buf,"r");
  if(!fp) return 0;
  textfile=open_memstream(&textfile_text,&textfile_size);
  if(!textfile) err(1,"Cannot open memory stream for text file");
  c=fgetc(fp);
  if(c=='@') {
    for(i=0;i<79;i++) {
      c=fgetc(fp);
      if(c=='\n' || c==EOF) break;
      textbuf[i]=c;
    }
    textbuf[ntextbuf=i]=0;
  } else {
    ungetc(c,fp);
  }
  for(i=0;;) {
    c=fgetc(fp);
    if(c=='\n' || (c==EOF && i)) {
      buf[i]=0;
      fputc(i,textfile);
      fwrite(buf,1,80,textfile);
      i=0;
    }
    if(c==EOF) break;
    if(c!='\n' && i<79) buf[i++]=c;
  }
  fclose(fp);
  fputc(0,textfile);
  fclose(textfile);
  if(!textfile_text) errx(1,"Allocation failed");
  tnlines=textfile_size/TEXTREC;
  return 1;
}

static void update_text_window(const WindowInfo*wind) {
  int i,j;
  Uint8 top=cur_screen.hard_edge[DIR_N];
  Uint8 mid=cur_screen.view_y;
  Uint8 bot=cur_screen.hard_edge[DIR_S];
  Uint32 v,x,y;
  Uint8 cmd,col,chr;
  int linkline=-1;
  int linktext=-1;
  for(i=0;i<80*25;i++) {
    cmd=cur_screen.command[i];
    col=cur_screen.color[i];
    chr=cur_screen.parameter[i];
    if(cmd) v_font[i]=(cur_screen.flag&SF_ALT_MODE?VF_ALTERNATE:0);
    switch(cmd&0xF0) {
      case SC_BACKGROUND:
        if(cmd&1) v_char[i]=chr;
        if(cmd&2) v_color[i]=col;
        break;
      case SC_NUMERIC:
        v_char[i]=digit_of(status_vars[cmd&15],chr);
        v_color[i]=col;
        break;
      case SC_NUMERIC_SPECIAL:
        switch(cmd) {
          case SC_SPEC_PLAYER_X: v=stats->count?stats->xy->x:0; break;
          case SC_SPEC_PLAYER_Y: v=stats->count?stats->xy->y:0; break;
          case SC_SPEC_TEXT_SCROLL_PERCENT: v=(100L*(tcursor+(wind->flag&WF_ZERO_BASED?0:1)))/tnlines; break;
          case SC_SPEC_TEXT_LINE_NUMBER: v=tcursor+(wind->flag&WF_ZERO_BASED?0:1); break;
          case SC_SPEC_TEXT_LINE_COUNT: v=tnlines; break;
          case SC_SPEC_CURRENT_BOARD: v=cur_board_id; break;
          case SC_SPEC_EXIT_E: v=board_info.exits[DIR_E]; break;
          case SC_SPEC_EXIT_N: v=board_info.exits[DIR_N]; break;
          case SC_SPEC_EXIT_W: v=board_info.exits[DIR_W]; break;
          case SC_SPEC_EXIT_S: v=board_info.exits[DIR_S]; break;
          case SC_SPEC_WIDTH: v=board_info.width; break;
          case SC_SPEC_HEIGHT: v=board_info.height; break;
          case SC_SPEC_USERDATA: v=board_info.userdata; break;
          default: continue; // not applicable in this context, so ignore it
        }
        v_char[i]=digit_of(v,chr);
        v_color[i]=col;
        break;
      case SC_MEMORY:
        v_char[i]=memory[(col<<8)|chr];
        v_color[i]=memory[(col<<8)|chr]>>8;
        break;
      case SC_INDICATOR:
        v_color[i]=col;
        x=i%80; y=i/80;
        switch(cmd) {
          case SC_IND_USERDATA: v_char[i]=board_info.userdata?chr:0; break;
          case SC_IND_CURSOR: v_char[i]=(cur_screen.flag&SF_NO_SCROLL?(y-top-tscroll==tcursor):(y==mid))?chr:0; break;
          case SC_IND_SCROLL_Y: v_char[i]=(y>cur_screen.view_y?(tscroll>mid-top):(tscroll<tnlines+mid-bot))
           ?chr:(cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[y>cur_screen.view_y?DIR_S:DIR_N]:0); break;
          case SC_IND_EXIT_E: v_char[i]=board_info.exits[DIR_E]?chr:0; break;
          case SC_IND_EXIT_N: v_char[i]=board_info.exits[DIR_N]?chr:0; break;
          case SC_IND_EXIT_W: v_char[i]=board_info.exits[DIR_W]?chr:0; break;
          case SC_IND_EXIT_S: v_char[i]=board_info.exits[DIR_S]?chr:0; break;
          case SC_IND_USER0: v_char[i]=board_info.flag&BF_USER0?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[0]:0; break;
          case SC_IND_USER1: v_char[i]=board_info.flag&BF_USER1?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[1]:0; break;
          case SC_IND_USER2: v_char[i]=board_info.flag&BF_USER2?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[2]:0; break;
          case SC_IND_USER3: v_char[i]=board_info.flag&BF_USER3?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[3]:0; break;
        }
        break;
      case SC_TEXT:
        x=i%80; y=i/80;
        v_char[i]=0;
        v_color[i]=col;
        if(y+tscroll-mid<0 || y+tscroll-mid>=tnlines) {
          if(y+tscroll-mid==-1 || y+tscroll-mid==tnlines || !(wind->flag&WF_SINGLE_ENDS)) {
            v_char[i]=chr;
            if(cur_screen.border_color) v_color[i]=cur_screen.border_color;
          }
        } else {
          y+=tscroll-mid;
          v=textfile_text[y*TEXTREC];
          if(!v) break;
          j=textfile_text[y*TEXTREC+1];
          if(j=='!' && v>1) {
            if(linkline!=y) {
              linkline=y;
              linktext=0;
              for(j=1;j<v;j++) if(textfile_text[y*TEXTREC+j+1]==';') {
                linktext=j+1;
                break;
              }
            }
            switch(wind->command[x]) {
              case 'A': case 'L': indicator:
                if(wind->color[x]!=0x11) v_color[i]=(wind->color[x]==0x22?(col&0xF0)|(cmd&0x0F):wind->color[x]);
                v_char[i]=wind->parameter[x]?:chr;
                break;
              case 'C':
                if(tcursor==y) goto indicator;
                break;
              case 'K':
                if(v>3 && textfile_text[y*TEXTREC+2]=='<' && textfile_text[y*TEXTREC+4]=='>') {
                  if(wind->color[x]!=0x11) v_color[i]=(wind->color[x]==0x22?(col&0xF0)|(cmd&0x0F):wind->color[x]);
                  v_char[i]=textfile_text[y*TEXTREC+3];
                } else if(wind->parameter[x]!=255) {
                  goto indicator;
                }
                break;
              case 'P':
                if(v>3 && textfile_text[y*TEXTREC+2]=='<' && textfile_text[y*TEXTREC+4]=='>') goto indicator;
                break;
              case 'T':
                if(x>=cur_screen.soft_edge[DIR_W] && x<cur_screen.soft_edge[DIR_W]+v-linktext) {
                  if(wind->color[x]!=0x11) v_color[i]=(wind->color[x]==0x22?(col&0xF0)|(cmd&0x0F):wind->color[x]);
                  v_char[i]=textfile_text[y*TEXTREC+linktext+1+x-cur_screen.soft_edge[DIR_W]];
                } else {
                  v_char[i]=wind->parameter[x]?:chr;
                }
                break;
              default:
                if(x>=cur_screen.soft_edge[DIR_W] && x<=cur_screen.soft_edge[DIR_E] && x<cur_screen.soft_edge[DIR_W]+v-linktext) {
                  v_color[i]=wind->wcolor[WC_LINK_TEXT]?:((col&0xF0)|(cmd&0x0F));
                  v_char[i]=textfile_text[y*TEXTREC+linktext+1+x-cur_screen.soft_edge[DIR_W]];
                }
                break;
            }
          } else if(j=='$') {
            v_color[i]=(col&0xF0)|(cmd&0x0F);
            j=cur_screen.view_x-(v-1)/2;
            if(x<j || x-j>=v-1) {
              if(wind->command[x]=='A' || wind->command[x]=='S') {
                v_char[i]=wind->parameter[x]?:chr;
                if(wind->color[x]!=0x22) v_color[i]=(wind->color[x]==0x11?col:wind->color[x]);
              }
              break;
            } else if(wind->wcolor[WC_CENTER_TEXT]) {
              v_color[i]=wind->wcolor[WC_CENTER_TEXT];
            }
            v_char[i]=textfile_text[y*TEXTREC+x+2-j];
          } else if(j==':' && v>1) {
            if(linkline!=y) {
              linkline=y;
              linktext=0;
              for(j=1;j<v;j++) if(textfile_text[y*TEXTREC+j+1]==';') {
                linktext=j+1;
                break;
              }
            }
            v_color[i]=wind->wcolor[WC_LABEL_TEXT]?:((col&0xF0)|(cmd&0x0F));
            x-=cur_screen.hard_edge[DIR_W];
            if(x<0 || x>=v-linktext) goto outer;
            v_char[i]=textfile_text[y*TEXTREC+x+1+linktext];
          } else {
            x-=cur_screen.hard_edge[DIR_W];
            if(x<0 || x>=v) {
              outer:
              if(wind->command[x=i%80]=='A') {
                v_char[i]=wind->parameter[x]?:chr;
                if(wind->color[x]!=0x11) v_color[i]=(wind->color[x]==0x22?(col&0xF0)|(cmd&0x0F):wind->color[x]);
              }
              break;
            } else if(wind->wcolor[WC_NORMAL_TEXT]) {
              v_color[i]=wind->wcolor[WC_NORMAL_TEXT];
            }
            v_char[i]=textfile_text[y*TEXTREC+x+1];
          }
        }
        break;
      case SC_ITEM:
        if(cmd==SC_ITEM_ELEMENT) display_item_element_cell(i,col,chr>>5,chr&0x1F);
        break;
      case SC_BITS_0_LO ... SC_BITS_3_HI:
        v_color[i]=col;
        v=status_vars[(cmd-SC_BITS_0_LO)>>5];
        v_char[i]=(v&(1UL<<(cmd&0x1F))?chr:32);
        break;
    }
  }
  if(cur_screen.message_y<25) {
    if((cur_screen.flag&SF_LEFT_ALIGN_MESSAGE) || cur_screen.message_x-ntextbuf/2<cur_screen.message_l) x=cur_screen.message_l;
    else x=cur_screen.message_x-ntextbuf/2;
    for(i=0;i<ntextbuf && x<=cur_screen.message_r && x<80;i++,x++) {
      y=cur_screen.message_y*80+x;
      v_char[y]=textbuf[i];
      v_color[y]=cur_screen.color[y];
    }
  }
}

static char show_text_window(Uint32 xyn,char help) {
  Sint32 rs[4]={regs[0],regs[1],regs[2],regs[3]};
  WindowInfo wind={};
  FILE*fp;
  const char*e;
  int a,b,c;
  Uint8 scl;
  char r=0;
  if(!help) {
    if(!textfile) return 0;
    fputc(0,textfile);
    fclose(textfile);
  }
  if(!textfile_text) errx(1,"Allocation failed");
  tnlines=textfile_size/TEXTREC;
  if(tnlines==1) {
    memcpy(vtextbuf,textfile_text+1,TEXTREC);
    nvtextbuf=*textfile_text;
    if(vtexttime=(nvtextbuf?config.message_timer:0)) add_message_text();
  } else if(tnlines>1) {
    if(v_status[1] && v_status[1]!=32) errx(1,"Cannot display multiple non-main screens");
    set_timer(0);
    if((a=xyn&0xFFFF) && a<=maxstat && stats[a-1].length && stats[a-1].text[0]=='@') {
      for(b=0;b<80 && b<stats[a-1].length;b++) {
        textbuf[b]=c=stats[a-1].text[b+1];
        if(c=='=' || c=='\n' || !c) break;
      }
      textbuf[ntextbuf=b]=0;
    } else if(!help) {
      ntextbuf=*textbuf=0;
    }
    tcursor=0;
    fp=open_lump_by_number(cur_screen_id=memory[MEM_TEXT_SCREEN],"SCR","r");
    if(!fp) err(1,"Cannot open %04X.SCR",cur_screen_id);
    if(e=load_screen(fp)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
    fclose(fp);
    if(fp=open_lump_by_number(cur_screen_id,"WIN","r")) {
      if(e=load_window(fp,&wind)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
      fclose(fp);
    }
    scl=cur_screen.hard_edge[DIR_S]-cur_screen.hard_edge[DIR_N];
    if(cur_screen.flag&SF_NO_SCROLL) {
      a=cur_screen.hard_edge[DIR_N];
      b=cur_screen.view_y-tnlines/2;
      tscroll=(b<a?cur_screen.view_y-a:b-a);
    } else {
      tscroll=0;
    }
    v_status[1]=232;
    work_varproperties(&cur_screen.varprop);
    open:
    if(memory[MEM_CONTROL]&CONTROL_WIN_STOP_KEY_REPEAT) stop_key_repeat();
    for(;;) {
      update_text_window(&wind);
      redisplay();
      if(!next_event()) errx(0,"No events available.");
      if(event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP) {
        switch(c=do_joystick(JL_TEXT_WINDOW)) {
          case 9: goto tab;
          case 13: goto select;
          case 24: goto up;
          case 25: goto down;
          case 33 ... 126: goto asciikey;
          case 'Z'+0x100: goto close;
          case '<'+0x100: goto prevpage;
          case '>'+0x100: goto nextpage;
          case '['+0x100: goto top;
          case ']'+0x100: goto bottom;
        }
      }
      if(event.type!=SDL_KEYDOWN) continue;
      if(wind.flag&WF_KEY_EVENT) {
        switch(event.key.keysym.sym) {
          case SDLK_ESCAPE: case SDLK_F2: case SDLK_F11: goto key;
          case SDLK_UP: c=(event.key.keysym.mod&KMOD_SHIFT)?30:24; break;
          case SDLK_DOWN: c=(event.key.keysym.mod&KMOD_SHIFT)?31:25; break;
          case SDLK_LEFT: c=(event.key.keysym.mod&KMOD_SHIFT)?17:27; break;
          case SDLK_RIGHT: c=(event.key.keysym.mod&KMOD_SHIFT)?16:26; break;
          default:
            c=event.key.keysym.unicode;
            if(c==27) goto close;
            if((c<32 || c>126) && !(c==8 || c==9 || c==13)) goto key;
        }
        memory[MEM_RETURNED_PC]=memory[MEM_WINDOW_KEY_EVENT];
        rekey:
        memory[MEM_ARG_J]=tnlines; memory[MEM_ARG_K]=tscroll;
        a=tcursor*TEXTREC;
        if(textfile_text[a]) {
          if((b=textfile_text[a+1])=='!') {
            if(textfile_text[a+2]=='<' && textfile_text[a+4]=='>') a=textfile_text[a+3]; else a=0;
          } else {
            a=0; if(b!='$' && b!=':') b=0;
          }
        }
        c=run_program(memory[MEM_RETURNED_PC],c,a,tcursor,b);
        if(c<0) {
          switch((-c)%100) {
            case 1: goto close;
            case 2: tcursor=memory[MEM_ARG_J]; if(tcursor>=tnlines) tcursor=tnlines?tnlines-1:0; break;
            case 3: tscroll=memory[MEM_ARG_K]; if(tscroll>=tnlines) tscroll=tnlines?tnlines-1:0; break;
            case 4: wind.wcolor[memory[MEM_ARG_J]&15]=memory[MEM_ARG_K]; break;
            case 50:
              for(a=tcursor+1;a<tnlines;a++) if(textfile_text[a*TEXTREC] && textfile_text[a*TEXTREC+1]==memory[MEM_ARG_J]) break;
              if(a!=tnlines) tcursor=a,condflag=1; else condflag=0;
              break;
            case 51: r=1; goto close;
          }
          if(c<-99) goto rekey;
        } else {
          switch(c) {
            case 9: goto tab;
            case 13: goto select;
            case 24: goto up;
            case 25: goto down;
          }
        }
        goto asciikey;
      }
      key: switch(event.key.keysym.sym) {
        case SDLK_ESCAPE: goto close;
        case SDLK_END: case SDLK_KP1: bottom: tcursor=tnlines-1; break;
        case SDLK_DOWN: case SDLK_KP2: down: if(tcursor!=tnlines-1) ++tcursor; break;
        case SDLK_PAGEDOWN: case SDLK_KP3: nextpage: if(!(cur_screen.flag&SF_NO_SCROLL)) tcursor=(tcursor+scl>=tnlines?tnlines-1:tcursor+scl); break;
        case SDLK_HOME: case SDLK_KP7: top: tcursor=0; break;
        case SDLK_UP: case SDLK_KP8: up: if(tcursor) --tcursor; break;
        case SDLK_PAGEUP: case SDLK_KP9: prevpage: if(!(cur_screen.flag&SF_NO_SCROLL)) tcursor=(tcursor-scl>=0?tcursor-scl:0); break;
        case SDLK_TAB: tab:
          for(a=tcursor+1;a<tnlines;a++) if(textfile_text[a*TEXTREC] && textfile_text[a*TEXTREC+1]=='!') break;
          if(a==tnlines) {
            for(a=0;a<tcursor;a++) if(textfile_text[a*TEXTREC] && textfile_text[a*TEXTREC+1]=='!') break;
          }
          if(a!=tnlines) tcursor=a;
          break;
        case SDLK_RETURN: select:
          a=tcursor*TEXTREC;
          if(textfile_text[a] && textfile_text[a+1]=='!') {
            b=2;
            if(textfile_text[a+b]=='<' && textfile_text[a+b+2]=='>') b=5;
            for(ntextbuf=0;ntextbuf+b<textfile_text[a] && textfile_text[a+b+ntextbuf]!=';' && ntextbuf<80;ntextbuf++);
            memcpy(textbuf,textfile_text+a+b,ntextbuf);
            textbuf[ntextbuf]=0;
            if(help) {
              for(tcursor=0;tcursor<tnlines;tcursor++) {
                a=tcursor*TEXTREC;
                if(textfile_text[a]>2 && textfile_text[a+1]==':' && match_label(textfile_text+a+1,textbuf)) {
                  if(!(cur_screen.flag&SF_NO_SCROLL)) tscroll=tcursor;
                  goto open;
                }
              }
            }
            r=1;
          } else if(!config.return_cancels) {
            break;
          }
          goto close;
        case SDLK_F2: a=audio_get_volume(); audio_set_volume(a&0xFFFF,(a>>16)^1); soundon=(audio_get_volume()<0x10000?1:0); audio_set_sfx("@0ZCX"); break;
        case SDLK_F11:
          lpt_document() {
            if(ntextbuf) lpt_title(textbuf,ntextbuf);
            for(a=0;a<tnlines;a++) lpt_script(textfile_text+a*TEXTREC+1,textfile_text[a*TEXTREC]);
          }
          break;
        default:
          c=event.key.keysym.unicode;
          asciikey: if(c>32 && c<127) {
            for(a=0;a<tnlines;a++) if(textfile_text[b=a*TEXTREC]>4 && textfile_text[b+1]=='!' && textfile_text[b+2]=='<' && textfile_text[b+4]=='>') {
              if(c==textfile_text[b+3] || (c>='a' && c<='z' && c+'A'-'a'==textfile_text[b+3]) || (c>='A' && c<='Z' && c+'a'-'A'==textfile_text[b+3])) {
                tcursor=a;
                goto select;
              }
            }
          }
          break;
      }
      if(!(cur_screen.flag&SF_NO_SCROLL)) tscroll=tcursor;
    }
    close:
    memcpy(regs,rs,4*sizeof(Sint32));
    if(r && ntextbuf && *textbuf=='-') {
      if(load_help_file(textbuf+1)) {
        tcursor=0;
        if(!(cur_screen.flag&SF_NO_SCROLL)) tscroll=0;
        help=1;
        r=0;
        goto open;
      }
    }
    v_status[1]=32;
    if(help!=2) set_timer(playstate==PLAYSTATE_FAST?config.speed_fast:playstate==PLAYSTATE_NORMAL?config.speed:0);
    fp=open_lump_by_number(cur_screen_id=board_info.screen,"SCR","r");
    if(!fp || load_screen(fp)) errx(1,"Error restoring screen");
    fclose(fp);
    work_varproperties(&cur_screen.varprop);
    if(help!=2) work_varproperties(&board_info.varprop);
    if(memory[MEM_CONTROL]&CONTROL_WIN_STOP_KEY_REPEAT) stop_key_repeat();
  }
  free(textfile_text);
  textfile=0;
  textfile_text=0;
  textfile_size=0;
  repeating=0;
  return r;
}

void show_help_file(void) {
  if(load_help_file(textbuf)) show_text_window(0,2);
}

typedef struct {
  Uint32 name;
  Uint16 slot,ext;
} ItemMenuSlot;

typedef struct {
  WindowInfo*wi;
  ItemMenuSlot*list;
  Uint8*nam;
  Sint32 move;
  Uint8 ncol;
} ItemMenuInfo;

static inline void update_item_window(const ItemMenuInfo*inf) {
  Inventory*inv=inventory+(memory[MEM_INVENTORY]&7);
  const Uint8*p;
  const Uint8*tp=0;
  const Uint8*desc=0;
  Uint8 descy=0;
  Uint8 top=cur_screen.hard_edge[DIR_N];
  Sint32 n=(tscroll-cur_screen.view_y)*(Sint32)inf->ncol;
  Sint32 m;
  Uint8 inn=0;
  int i,j;
  Uint32 v,x,y,z,zz,fv;
  Uint8 cmd,col,chr,col1;
  Uint8 rv=0;
  Uint8 rf=0;
  Uint8 rn=0;
  Uint8 on=0;
  if(!cur_screen.hard_edge[DIR_N]) inn=1;
  if(tcursor<tnlines && !(inf->list[tcursor].ext&0x8000)) {
    if(m=inv->item[inf->list[tcursor].slot].item) desc=itemnames+itemdefs[m-1].desc;
  }
  for(i=x=y=0;i<80*25;i++,x++) {
    if(x==80) {
      y++; n++; x=rf=rn=rv=0; tp=0;
      if(y>=cur_screen.hard_edge[DIR_N] && y<=cur_screen.hard_edge[DIR_S]) inn=1; else inn=0;
    } else if(x) {
      if((cur_screen.command[i]^cur_screen.command[i-1])&0xF0) tp=0;
    }
    if(inf->wi->command[x]=='Z') rf=rn=rv=0,n++,tp=0;
    cmd=cur_screen.command[i];
    col=cur_screen.color[i];
    chr=cur_screen.parameter[i];
    if(cmd) v_font[i]=(cur_screen.flag&SF_ALT_MODE?VF_ALTERNATE:0);
    switch(cmd&0xF0) {
      case SC_BACKGROUND:
        if(cmd&1) v_char[i]=chr;
        if(cmd&2) v_color[i]=col;
        break;
      case SC_NUMERIC:
        v_char[i]=digit_of(status_vars[cmd&15],chr);
        v_color[i]=col;
        break;
      case SC_NUMERIC_SPECIAL:
        switch(cmd) {
          case SC_SPEC_PLAYER_X: v=stats->count?stats->xy->x:0; break;
          case SC_SPEC_PLAYER_Y: v=stats->count?stats->xy->y:0; break;
          case SC_SPEC_TEXT_SCROLL_PERCENT: v=(100L*(tscroll+(inf->wi->flag&WF_ZERO_BASED?0:1)))/tnlines; break;
          case SC_SPEC_TEXT_LINE_NUMBER: v=tcursor+(inf->wi->flag&WF_ZERO_BASED?0:1); break;
          case SC_SPEC_TEXT_LINE_COUNT: v=tnlines; break;
          case SC_SPEC_CURRENT_BOARD: v=cur_board_id; break;
          case SC_SPEC_EXIT_E: v=board_info.exits[DIR_E]; break;
          case SC_SPEC_EXIT_N: v=board_info.exits[DIR_N]; break;
          case SC_SPEC_EXIT_W: v=board_info.exits[DIR_W]; break;
          case SC_SPEC_EXIT_S: v=board_info.exits[DIR_S]; break;
          case SC_SPEC_WIDTH: v=board_info.width; break;
          case SC_SPEC_HEIGHT: v=board_info.height; break;
          case SC_SPEC_USERDATA: v=board_info.userdata; break;
          case SC_SPEC_CONTEXT_SPECIFIC: if(on) v=fv; else {chr=0; goto plain;} break;
        }
        v_char[i]=digit_of(v,chr);
        v_color[i]=col;
        break;
      case SC_INDICATOR:
        v_color[i]=col;
        switch(cmd) {
          case SC_IND_USERDATA: v_char[i]=board_info.userdata?chr:0; break;
          case SC_IND_CURSOR: v_char[i]=(n==tcursor?chr:0); break;
          case SC_IND_SCROLL_Y: /* TODO */ break;
          case SC_IND_SCROLL_X: /* TODO: horizontal scrolling */ break;
          case SC_IND_EXIT_E: v_char[i]=board_info.exits[DIR_E]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_E]:0; break;
          case SC_IND_EXIT_N: v_char[i]=board_info.exits[DIR_N]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_N]:0; break;
          case SC_IND_EXIT_W: v_char[i]=board_info.exits[DIR_W]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_W]:0; break;
          case SC_IND_EXIT_S: v_char[i]=board_info.exits[DIR_S]?chr:cur_screen.flag&SF_EXIT_BORDER?cur_screen.border[DIR_S]:0; break;
          case SC_IND_USER0: v_char[i]=board_info.flag&BF_USER0?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[0]:0; break;
          case SC_IND_USER1: v_char[i]=board_info.flag&BF_USER1?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[1]:0; break;
          case SC_IND_USER2: v_char[i]=board_info.flag&BF_USER2?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[2]:0; break;
          case SC_IND_USER3: v_char[i]=board_info.flag&BF_USER3?chr:cur_screen.flag&SF_USER_BORDER?cur_screen.border[3]:0; break;
        }
        break;
      case SC_TEXT:
        // Show item name or description
        if(tcursor>=tnlines || (inf->list[tcursor].ext&0x8000)) {
          plain: v_char[i]=chr; v_color[i]=col;
        } else if(y==cur_screen.message_y) {
          if(!tp) { if(inf->list[tcursor].ext&0x4000) tp=inf->nam+inf->list[tcursor].name; else tp=itemnames+inf->list[tcursor].name; }
          if(!*tp) goto plain;
          v_char[i]=*tp++; v_color[i]=inf->wi->wcolor[WC_LINK_TEXT]?:col;
        } else {
          if(!desc || !*desc) goto plain;
          if(*desc=='\n') {
            if(descy!=y) descy=y,++desc;
            goto plain;
          } else if(*desc!='\n') {
            v_char[i]=*desc++; v_color[i]=inf->wi->wcolor[WC_NORMAL_TEXT]?:col;
          }
        }
        break;
      case SC_ITEM:
        m=(inn?n:tcursor);
        if(inn && (n<0 || n>=tnlines)) {
          hide: v_color[i]=(col>>4)*0x11; v_char[i]=32; break;
        }
        j=1; z=m<0?0:m<tnlines?inf->list[m].slot:0; col1=col;
        if(cmd==SC_ITEM_PLACEHOLDER) {
          if(!inn) goto plain; // This is not supposed to happen, but check in case it does anyways
          if(m<0 || m>=tnlines || (inf->list[m].ext&0x8000)) {
            if(v=inf->wi->wcolor[WC_VACANT_ITEM]) col=v,j=0;
          } else if(
              ((inf->list[m].ext&0x0100) && (v=inf->list[m].ext))
           || ((inv->item[z].flag&ISF_HILIGHT) && (v=inf->wi->wcolor[WC_HILIGHT_ITEM]))
           || ((inv->item[z].flag&ISF_FIXED) && (v=inf->wi->wcolor[WC_FIXED_ITEM]))
           || (inv->item[z].item && inv->item[z].item<=nitemdefs && (itemdefs[inv->item[z].item-1].flag&(IDF_NO_DISCARD|IDF_HIDE_QUANTITY)) && (v=inf->wi->wcolor[WC_KEY_ITEM]))
           || (v=inf->wi->wcolor[WC_NORMAL_ITEM])
          ) {
            col=v,j=0;
          }
        }
        if(n==tcursor && (v=inf->wi->wcolor[WC_SELECTED_ITEM])) {
          if(v==0xFF) col=(col<<4)|(col>>4); else if(j) col=v; else if(inf->wi->flag&WF_XOR_COLOR) col^=v; else col|=v;
          if(v==0xFF) col1=(col1<<4)|(col1>>4); else if(j) col1=v; else if(inf->wi->flag&WF_XOR_COLOR) col1^=v; else col1|=v;
        }
        if(n==inf->move && (v=inf->wi->wcolor[WC_MOVE_ITEM])) {
          if(v==0xFF) col=(col<<4)|(col>>4); else if(j && col==cur_screen.color[i]) col=v; else if(inf->wi->flag&WF_XOR_COLOR) col^=v; else col|=v;
          if(v==0xFF) col1=(col1<<4)|(col1>>4); else if(j) col1=v; else if(inf->wi->flag&WF_XOR_COLOR) col1^=v; else col1|=v;
        }
        if(cmd==SC_ITEM_PLACEHOLDER) {
          if(m<0 || m>=tnlines || (inf->list[m].ext&0x8000)) goto plain;
          switch(inf->wi->command[x]) {
            case 'F': case 'G': if(inf->wi->color[x]!=0x11) j=1; break;
            case 'Q':
              zz=inv->item[z].item;
              if(zz>0 && zz<=nitemdefs && (itemdefs[zz-1].flag&IDF_HIDE_QUANTITY)) j=0; else j=1;
              break;
            case 'X': break;
            default: j=0;
          }
          if(j && inf->wi->color[x]!=0x22) {
            col=(inf->wi->color[x]==0x11?cur_screen.color[i]:inf->wi->color[x]);
            if(n==tcursor && (v=inf->wi->wcolor[WC_SELECTED_ITEM])) {
              if(v==0xFF) col=(col<<4)|(col>>4); else if(col==cur_screen.color[i]) col=v; else if(inf->wi->flag&WF_XOR_COLOR) col^=v; else col|=v;
            }
            if(n==inf->move && (v=inf->wi->wcolor[WC_MOVE_ITEM])) {
              if(v==0xFF) col=(col<<4)|(col>>4); else if(col==cur_screen.color[i]) col=v; else if(inf->wi->flag&WF_XOR_COLOR) col^=v; else col|=v;
            }
          }
          switch(inf->wi->command[x]) {
            case 'F':
              rf=1;
              do ++rv; while(rv<=cur_screen.varprop.count && (cur_screen.varprop.item[rv-1].type<0x32 || cur_screen.varprop.item[rv-1].type>0x3F));
              if(rv>cur_screen.varprop.count) break;
              zz=inv->item[z].item;
              if(zz<1 || zz>nitemdefs) goto hideq;
              if((cur_screen.varprop.item[rv-1].data[0]&0x80) && (itemdefs[zz-1].flag&IDF_HIDE_QUANTITY)) {
                rf=128; fv=0; goto hideq;
              }
              switch(cur_screen.varprop.item[rv-1].data[0]&0x0F) {
                case 0: fv=inv->item[z].ext0; break;
                case 1: fv=inv->item[z].ext1; break;
                case 2: fv=inv->item[z].ext2; break;
                case 3: fv=itemdefs[zz-1].ext3; break;
                case 4: fv=itemdefs[zz-1].ext4; break;
                case 5: fv=itemdefs[zz-1].ext5; break;
                case 6: fv=inf->list[m].ext&0xFF; break;
                case 7: fv=itemdefs[zz-1].price; break;
                case 8: fv=itemdefs[zz-1].parameter; break;
                case 9: fv=itemdefs[zz-1].weight; break;
                case 10: fv=1; break;
                case 11: fv=inv->strength; break;
                case 12: fv=itemdefs[zz-1].maxheap; break;
              }
              if(cur_screen.varprop.item[rv-1].data[0]&0x40) fv*=inv->item[z].quantity;
              /* fall through */
            case 'G':
              if(rv>cur_screen.varprop.count || !rv) break;
              zz=inv->item[z].item;
              if(zz<1 || zz>nitemdefs || rf>15 || rv>cur_screen.varprop.count) {
                hideq: v_color[i]=col1; v_char[i]=inf->wi->parameter[x]?:chr;
              } else {
                v_color[i]=col;
                v_char[i]=digit_of(fv,cur_screen.varprop.item[rv-1].data[rf++])?:inf->wi->parameter[x]?:chr;
              }
              break;
            case 'Q':
              if(zz>0 && zz<=nitemdefs && (itemdefs[zz-1].flag&IDF_HIDE_QUANTITY)) goto name;
              zz=inv->item[z].quantity;
              if(x && inf->wi->command[x-1]=='Q' && inf->wi->parameter[x-1]==255 && inf->wi->parameter[x]!=255) {
                v_char[i]=inf->wi->parameter[x]?:chr;
              } else if(!zz && (x==79 || inf->wi->command[x+1]!='Q')) {
                v_char[i]='0';
              } else {
                for(j=1,v=x+1;v<80 && inf->wi->command[v]=='Q' && (inf->wi->parameter[v]==255 || inf->wi->parameter[x]!=255);v++) j=(zz?1:0),zz/=10;
                v_char[i]=(zz?zz%10+'0':(j && inf->wi->parameter[x]!=255)?(inf->wi->parameter[x]?:chr):' ');
              }
              v_color[i]=col;
              break;
            case 'X': chr=inf->wi->parameter[x]?:chr; goto name;
            default: name:
              p=(inf->list[m].ext&0x4000?itemnames:inf->nam)+inf->list[m].name+rn;
              if(v_char[i]=*p) {
                rn++;
                v_color[i]=col;
              } else {
                v_color[i]=col1;
                v_char[i]=chr;
              }
          }
        } else if(cmd==SC_ITEM_ELEMENT) {
          if(inn && (n<0 || n>=tnlines || (inf->list[n].ext&0x8000))) goto hide;
          display_item_element_cell(i,col,inn?memory[MEM_INVENTORY]&7:chr>>5,inn?inf->list[n].slot:chr&0x1F);
        } else if((cmd&SC_ITEM_FLAGS)==SC_ITEM_FLAGS) {
          if(m<0 || m>=tnlines || (inf->list[m].ext&0x8000)) goto hide;
          if(inv->item[inf->list[m].slot].flag&(1<<(cmd&7))) goto plain; else goto hide;
        } else if(cmd==SC_ITEM_SELECT_FIELD) {
          v_color[i]=col;
          v_char[i]=cur_screen.border[(chr>>4)&3];
          on=0;
          if(z>=inv->count) break;
          zz=inv->item[z].item;
          if(!zz || zz>nitemdefs) break;
          if((chr&0x80) && (itemdefs[zz-1].flag&IDF_HIDE_QUANTITY)) break;
          switch(chr&0x0F) {
            case 0: fv=inv->item[z].ext0; break;
            case 1: fv=inv->item[z].ext1; break;
            case 2: fv=inv->item[z].ext2; break;
            case 3: fv=itemdefs[zz-1].ext3; break;
            case 4: fv=itemdefs[zz-1].ext4; break;
            case 5: fv=itemdefs[zz-1].ext5; break;
            case 6: fv=inf->list[m].ext&0xFF; break;
            case 7: fv=itemdefs[zz-1].price; break;
            case 8: fv=itemdefs[zz-1].parameter; break;
            case 9: fv=itemdefs[zz-1].weight; break;
            case 10: fv=1; break;
            case 11: fv=inv->strength; break;
            case 12: fv=itemdefs[zz-1].maxheap; break;
          }
          if(chr&0x40) fv*=inv->item[z].quantity;
          on=1;
        }
        break;
      case SC_BITS_0_LO ... SC_BITS_3_HI:
        v_color[i]=col;
        v=status_vars[(cmd-SC_BITS_0_LO)>>5];
        v_char[i]=(v&(1UL<<(cmd&0x1F))?chr:32);
        break;
    }
  }
}

static Uint16 show_item_window(Uint32 opt) {
  Sint32 rs[4]={regs[0],regs[1],regs[2],regs[3]};
  ItemMenuInfo inf;
  WindowInfo wind={};
  FILE*fp;
  FILE*nfp;
  char*names=0;
  size_t snames=0;
  Uint32 iname=1;
  const char*e;
  Inventory*inv=inventory+(memory[MEM_INVENTORY]&7);
  ItemDef*d;
  ItemMenuSlot c;
  ItemMenuSlot*list=0;
  size_t slist=0;
  int i,j;
  Sint32 k;
  set_timer(0);
  // Load screen
  if(v_status[1] && v_status[1]!=32) errx(1,"Cannot display multiple non-main screens");
  fp=open_lump_by_number(cur_screen_id=memory[MEM_ITEM_SCREEN],"SCR","r");
  if(!fp) err(1,"Cannot open %04X.SCR",cur_screen_id);
  if(e=load_screen(fp)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
  fclose(fp);
  if(fp=open_lump_by_number(cur_screen_id,"WIN","r")) {
    if(e=load_window(fp,&wind)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
    fclose(fp);
  }
  work_varproperties(&cur_screen.varprop);
  v_status[1]='I';
  if(memory[MEM_CONTROL]&CONTROL_WIN_STOP_KEY_REPEAT) stop_key_repeat();
  // Load names and slots
  load:
  tcursor=tnlines=0;
  fp=open_memstream((char**)&list,&slist);
  nfp=open_memstream(&names,&snames);
  if(!fp || !nfp) err(1,"Allocation failed");
  fputc(0,nfp);
  for(i=0;i<inv->count;i++) {
    if((inv->item[i].flag&ISF_HIDDEN) || inv->item[i].item>nitemdefs) continue;
    if(!inv->item[i].item) {
      vac:
      if(opt&4) continue;
      c.name=0; c.slot=i; c.ext=0x8000;
      fwrite(&c,1,sizeof(ItemMenuSlot),fp);
      if(!(opt&1) && i==inv->cursor) tcursor=tnlines;
      tnlines++;
      continue;
    }
    d=itemdefs+inv->item[i].item-1;
    if(inv->item[i].flag&ISF_SPECIAL) switch(d->special) {
      case ISPECIAL_STATUS: inv->item[i].quantity=status_vars[d->special&15]; break;
      case ISPECIAL_STATUS_NONZERO: if(!(inv->item[i].quantity=status_vars[d->special&15])) goto vac; break;
    }
    condflag=(d->flag&IDF_UNIDENTIFIED?1:0);
    *textbuf=ntextbuf=0;
    k=run_program(memory[MEM_NAME_ITEM_EVENT],inv->item[i].item|(inv->item[i].flag<<16),i,d->flag,0);
    if(k<0) continue;
    c.slot=i; c.ext=k&0xBFFF;
    if(ntextbuf) {
      fwrite(textbuf,1,ntextbuf,nfp); fputc(0,nfp);
      c.name=iname; iname+=ntextbuf+1;
    } else {
      c.name=d->name; c.ext|=0x4000;
    }
    fputc(0,nfp);
    fwrite(&c,1,sizeof(ItemMenuSlot),fp);
    iname+=ntextbuf+1;
    if(!(opt&1) && i==inv->cursor) tcursor=tnlines;
    tnlines++;
  }
  *textbuf=ntextbuf=0;
  fclose(fp); fclose(nfp);
  if(!names || !list) err(1,"Allocation failed");
  // Init display
  for(inf.ncol=i=1;i<79;i++) if(wind.command[i]=='Z') ++inf.ncol;
  inf.wi=&wind; inf.list=list; inf.nam=names; inf.move=-1;
  // Display
  tscroll=(cur_screen.flag&SF_NO_SCROLL?cur_screen.view_y-cur_screen.hard_edge[DIR_N]:tcursor/inf.ncol);
  for(;;) {
    if(!(cur_screen.flag&SF_NO_SCROLL)) {
      if(wind.flag&WF_ALT_SCROLL) {
        i=tcursor/inf.ncol; // row that should be visible
        j=tscroll-cur_screen.view_y; // current top of screen position
        if(j>i-(Sint32)cur_screen.soft_edge[DIR_N]) j=i-(Sint32)cur_screen.soft_edge[DIR_N];
        if(j<i-(Sint32)cur_screen.soft_edge[DIR_S]) j=i-(Sint32)cur_screen.soft_edge[DIR_S];
        i=(tnlines-1)/inf.ncol;
        if(j>i-(Sint32)cur_screen.hard_edge[DIR_S]) j=i-(Sint32)cur_screen.hard_edge[DIR_S];
        if(j<0-(Sint32)cur_screen.hard_edge[DIR_N]) j=0-(Sint32)cur_screen.hard_edge[DIR_N];
        j+=cur_screen.view_y;
        tscroll=(j<0?0:j);
      } else {
        tscroll=tcursor/inf.ncol;
      }
    }
    update_item_window(&inf);
    redisplay();
    if(!next_event()) errx(0,"No events available.");
    if(event.type!=SDL_KEYDOWN) continue;
    switch(event.key.keysym.sym) {
      case SDLK_ESCAPE: escape: condflag=0; goto end;
      case SDLK_F2: i=audio_get_volume(); audio_set_volume(i&0xFFFF,(i>>16)^1); soundon=(audio_get_volume()<0x10000?1:0); audio_set_sfx("@0ZCX"); break;
      case SDLK_KP8: if(wind.flag&WF_KEY_EVENT) goto defa; // else fall through
      case SDLK_UP: i=(event.key.keysym.mod&KMOD_SHIFT)?30:24; goto ascii;
      case SDLK_KP2: if(wind.flag&WF_KEY_EVENT) goto defa; // else fall through
      case SDLK_DOWN: i=(event.key.keysym.mod&KMOD_SHIFT)?31:25; goto ascii;
      case SDLK_KP4: if(wind.flag&WF_KEY_EVENT) goto defa; // else fall through
      case SDLK_LEFT: i=(event.key.keysym.mod&KMOD_SHIFT)?17:27; goto ascii;
      case SDLK_KP6: if(wind.flag&WF_KEY_EVENT) goto defa; // else fall through
      case SDLK_RIGHT: i=(event.key.keysym.mod&KMOD_SHIFT)?16:26; goto ascii;
      default: defa:
        i=event.key.keysym.unicode;
        if(i==27) goto escape;
        if((i<32 || i>126) && !(i==8 || i==9 || i==13)) break;
        ascii:
        if(wind.flag&WF_KEY_EVENT) {
          memory[MEM_RETURNED_PC]=memory[MEM_WINDOW_KEY_EVENT];
          rekey:
          memory[MEM_ARG_J]=tnlines; memory[MEM_ARG_K]=tscroll;
          i=run_program(memory[MEM_RETURNED_PC],i,tcursor<tnlines?list[tcursor].slot+(list[tcursor].ext<<16):-1,tcursor,inf.move);
          if(i<0) {
            switch((-i)%100) {
              case 1: goto escape;
              case 2: tcursor=memory[MEM_ARG_J]; if(tcursor>=tnlines) tcursor=tnlines?tnlines-1:0; break;
              case 3: tscroll=memory[MEM_ARG_K]; if(tscroll>=tnlines/inf.ncol) tscroll=tnlines/inf.ncol; break;
              case 4: wind.wcolor[memory[MEM_ARG_J]&15]=memory[MEM_ARG_K]; break;
              case 50: free(names); free(list); names=0; list=0; snames=0; slist=0; iname=1; goto load;
              case 51: for(j=0;j<tnlines;j++) if(list[j].slot==memory[MEM_ARG_J] && !(list[j].ext&0x8000)) tcursor=j; break;
              case 52:
                if(!(fp=open_lump_by_number(cur_screen_id=memory[MEM_ARG_J],"SCR","r"))) err(1,"Cannot open %04X.SCR",cur_screen_id);
                if(e=load_screen(fp)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
                fclose(fp);
                if(fp=open_lump_by_number(cur_screen_id,"WIN","r")) {
                  if(e=load_window(fp,&wind)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
                  fclose(fp);
                }
                for(inf.ncol=j=1;j<79;j++) if(wind.command[j]=='Z') ++inf.ncol;
                work_varproperties(&cur_screen.varprop);
                break;
            }
            if(i<-99) goto rekey; else continue;
          }
        }
        switch(i) {
          case 13: case 32: sel:
            if(inf.move>=0) goto mov;
            if(tcursor>=tnlines || (opt&8) || (list[tcursor].ext&0x8200)) break;
            condflag=1; goto end;
          case 'h': case 'H': case 27: if(tcursor) --tcursor; break;
          case 'l': case 'L': case 26: if(tnlines && tcursor<tnlines-1) ++tcursor; break;
          case 'j': case 'J': case 25: tcursor+=inf.ncol; if(tcursor>=tnlines) tcursor=tnlines?tnlines-1:0; break;
          case 'k': case 'K': case 24: if(tcursor>inf.ncol) tcursor-=inf.ncol; else tcursor=0; break;
          case 'q': case 'Q': goto escape;
          case 'd': case 'D':
            if(tcursor>=tnlines || (opt&0x20) || (list[tcursor].ext&0x8000)) break;
            inf.move=-1;
            i=list[tcursor].slot;
            if(i>inv->count || !inv->item[i].item || inv->item[i].item>nitemdefs) break;
            if(inv->item[i].flag&(ISF_FIXED|ISF_IN_USE)) break;
            if(itemdefs[inv->item[i].item-1].flag&IDF_NO_DISCARD) break;
            if(inv->item[i].flag&ISF_SPECIAL) {
              switch((j=itemdefs[inv->item[i].item-1].special)&0xF0) {
                case ISPECIAL_STATUS: status_vars[j&15]=0; inv->item[i].quantity=0; break;
                case ISPECIAL_STATUS_NONZERO: status_vars[j&15]=0; inv->item[i].quantity=0; goto hide;
              }
              continue;
            }
            inv->item[i]=(ItemSlot){};
            hide:
            list[tcursor].name=0;
            list[tcursor].ext=0x8000;
            if(opt&4) {
              if(tcursor!=--tnlines) memmove(list+tcursor,list+tcursor+1,(tnlines-tcursor)*sizeof(ItemMenuSlot));
              if(tcursor && tcursor==tnlines) --tcursor;
            }
            break;
          case 'm': case 'M': mov:
            if(tcursor>=tnlines || (opt&0x10)) break;
            i=list[tcursor].slot;
            if(i>inv->count || (inv->item[i].flag&ISF_FIXED)) break;
            if(inf.move>=0 && inf.move<tnlines && list[inf.move].slot<inv->count) {
              ItemMenuSlot s=list[tcursor];
              ItemSlot ss=inv->item[i];
              inv->item[i]=inv->item[list[inf.move].slot];
              inv->item[list[inf.move].slot]=ss;
              list[tcursor]=list[inf.move];
              list[inf.move]=s;
              list[inf.move].slot=list[tcursor].slot;
              list[tcursor].slot=i;
              inf.move=-1;
            } else {
              inf.move=i;
            }
            break;
          case 'n': case 'N': inf.move=-1; break;
        }
    }
  }
  // End
  end:
  if(memory[MEM_CONTROL]&CONTROL_WIN_STOP_KEY_REPEAT) stop_key_repeat();
  memcpy(regs,rs,4*sizeof(Sint32));
  if(tcursor<tnlines && !(opt&2)) inv->cursor=list[tcursor].slot;
  if(condflag && tcursor<tnlines) k=list[tcursor].slot; else k=condflag=0;
  free(names); free(list);
  v_status[1]=32;
  set_timer(playstate==PLAYSTATE_FAST?config.speed_fast:playstate==PLAYSTATE_NORMAL?config.speed:0);
  fp=open_lump_by_number(cur_screen_id=board_info.screen,"SCR","r");
  if(!fp || load_screen(fp)) errx(1,"Error restoring screen");
  fclose(fp);
  work_varproperties(&cur_screen.varprop);
  work_varproperties(&board_info.varprop);
  return k;
}

static char script_go(Uint16 m,Uint16 n,Stat*s,StatXY*xy,Uint8 dir) {
  switch(dir) {
    case 'i': case 'I': return 1;
    case 'e': case 'E': dir=DIR_E; break;
    case 'w': case 'W': dir=DIR_W; break;
    case 'n': case 'N': dir=DIR_N; break;
    case 's': case 'S': dir=DIR_S; break;
    case 'f': case 'F': dir=(xy->layer/4+0)&3; break;
    case 'b': case 'B': dir=(xy->layer/4+2)&3; break;
    case 'l': case 'L': dir=(xy->layer/4+1)&3; break;
    case 'r': case 'R': dir=(xy->layer/4+3)&3; break;
    default: script_error(m,xy,"Improper direction"); return 2;
  }
  general_move(0,(n<<16)+m,xy->x,xy->y,0x0814,1,dir,dir);
  return condflag;
}

static inline char check_blocked_at(Uint32 x,Uint32 y) {
  if(x>=board_info.width || y>=board_info.height) return 1;
  if(elem_def[b_main[y*board_info.width+x].kind].attrib&A_FLOOR) return 0;
  return 1;
}

static inline char check_pushable_at(Uint32 x,Uint32 y,Uint32 a) {
  if(x>=board_info.width || y>=board_info.height) return 0;
  if(elem_def[b_main[y*board_info.width+x].kind].attrib&a) return 1;
  return 0;
}

static Sint32 parse_direction(Stat*s,StatXY*xy,Uint16*ip) {
  char buf[10];
  Uint8 adj=0;
  int c,n;
  condflag=0;
  again:
  while(s->text[*ip]==' ') ++*ip;
  for(n=0;;) {
    if(n==9) {
      script_error(s+1-stats,xy,"Invalid direction");
      return -1;
    }
    c=s->text[n+*ip];
    if(c>='A' && c<='Z') buf[n++]=c;
    else if(c>='a' && c<='z') buf[n++]=c+'A'-'a';
    else break;
  }
  if(c=='_') ++*ip;
  *ip+=n;
  switch(*buf) {
    case 'B':
      if(n==1 || (n==8 && !memcmp(buf+1,"ACKWARD",7))) return (condflag=1),((adj+xy->layer/4+2)&3);
      goto bad;
    case 'C':
      if(n==2 && buf[1]=='W') {
        adj+=3;
        goto again;
      } else if(n==3 && buf[1]=='C' && buf[2]=='W') {
        adj++;
        goto again;
      }
      goto bad;
    case 'E':
      if(n==1 || (n==4 && !memcmp(buf+1,"AST",3))) return (condflag=1),((adj+0)&3);
      goto bad;
    case 'F':
      if(n==4 && !memcmp(buf+1,"ACE",3)) return (condflag=1),((adj+xy->layer/4)&3);
      if(n==4 && !memcmp(buf+1,"LOW",3)) return (condflag=1),(xy->layer&0x10?((adj+xy->layer/4)&3):-1);
      if(n==1 || (n==7 && !memcmp(buf+1,"ORWARD",6))) return (condflag=1),((adj+xy->layer/4)&3);
      goto bad;
    case 'I':
      if(n==1 || (n==4 && !memcmp(buf+1,"DLE",3))) return (condflag=1),-1;
      goto bad;
    case 'L':
      if(n==1 || (n==4 && !memcmp(buf+1,"EFT",3))) return (condflag=1),((adj+xy->layer/4+1)&3);
      goto bad;
    case 'N':
      if(n==1 || (n==5 && !memcmp(buf+1,"ORTH",4))) return (condflag=1),((adj+1)&3);
      goto bad;
    case 'O':
      if(n==3 && buf[1]=='P' && buf[2]=='P') {
        adj+=2;
        goto again;
      }
      goto bad;
    case 'R':
      if(n==6 && !memcmp(buf+1,"ANDOM",5)) {
        return (condflag=1),dice(4);
      } else if(n==4 && !memcmp(buf+1,"NDP",3)) {
        adj+=2*dice(2)+1;
        goto again;
      } else if(n==5 && !memcmp(buf+1,"NDNS",4)) {
        return (condflag=1),((adj+2*dice(2)+1)&3);
      } else if(n==5 && !memcmp(buf+1,"NDNE",4)) {
        return (condflag=1),((adj+dice(2))&3);
      } else if(n==1 || (n==5 && !memcmp(buf+4,"IGHT",4))) {
        return (condflag=1),((adj+xy->layer/4+3)&3);
      } else if(n==4 && !memcmp(buf+1,"NDB",4)) {
        blocked:
        condflag=1; c=0; n-=4;
        if(check_blocked_at(xy->x-1,xy->y)!=n) buf[c++]=DIR_W;
        if(check_blocked_at(xy->x+1,xy->y)!=n) buf[c++]=DIR_E;
        if(check_blocked_at(xy->x,xy->y-1)!=n) buf[c++]=DIR_N;
        if(check_blocked_at(xy->x,xy->y+1)!=n) buf[c++]=DIR_S;
        return c?(adj+buf[dice(c)])&3:-1;
      } else if(n==5 && !memcmp(buf+1,"NDNB",4)) {
        goto blocked;
      }
      goto bad;
    case 'S':
      if(n==1 || (n==5 && !memcmp(buf+1,"OUTH",4))) return (condflag=1),((adj+3)&3);
      if(n>3 && buf[1]=='E' && buf[2]=='E' && buf[3]=='K') {
        if(!xy || !stats->count || (xy->x==stats->xy->x && xy->y==stats->xy->y)) return (condflag=1),-1;
        if(n==4) {
          if(stats->xy->x==xy->x || stats->xy->y==xy->y || dice(2)) goto seek1; else goto seek2;
        } else if(n==6 && buf[4]=='N' && buf[5]=='S') {
          seek1:
          if(stats->xy->y<xy->y) adj+=DIR_N; else if(stats->xy->y>xy->y) adj+=DIR_S;
          else if(stats->xy->x<xy->x) adj+=DIR_W; else if(stats->xy->x>xy->x) adj+=DIR_E;
          return (condflag=1),(adj&3);
        } else if(n==6 && buf[4]=='E' && buf[5]=='W') {
          seek2:
          if(stats->xy->x<xy->x) adj+=DIR_W; else if(stats->xy->x>xy->x) adj+=DIR_E;
          else if(stats->xy->y<xy->y) adj+=DIR_N; else if(stats->xy->y>xy->y) adj+=DIR_S;
          return (condflag=1),(adj&3);
        }
      }
      goto bad;
    case 'W':
      if(n==1 || (n==4 && !memcmp(buf+1,"EST",3))) return (condflag=1),((adj+2)&3);
      goto bad;
    default: bad: script_error(s+1-stats,xy,"Invalid direction"); return -1;
  }
}

static Sint32 count_text_choices(void) {
  size_t s;
  Sint32 n=0;
  if(!textfile) return 0;
  fflush(textfile);
  if(!textfile_text) return 0;
  for(s=0;s<textfile_size;s+=TEXTREC) if(textfile_text[s] && textfile_text[s+1]=='!') n++;
  return n;
}

static Sint32 parse_label_number(const Uint8*s) {
  Uint8 y=*s;
  Uint8 c;
  Sint32 w=0;
  if(y=='$' || y=='+' || y=='-') s++;
  if(y=='$') {
    while(c=*s++) {
      if(c>='0' && c<='9') w=(w<<4)+c-'0';
      else if(c>='A' && c<='F') w=(w<<4)+c+10-'A';
      else if(c>='a' && c<='f') w=(w<<4)+c+10-'a';
      else break;
    }
  } else {
    while(c=*s++) {
      if(c>='0' && c<='9') w=10*w+c-'0';
      else break;
    }
  }
  if(y=='-') w=-w;
  return w;
}

static Uint8 do_numstore(Stat*s,const Uint8*name,Sint8 op,Sint32 v) {
  Sint32 lo=0;
  Sint32 hi=1;
  Sint32 u;
  Uint8 c;
  Sint32 w=find_unzapped_label(s,name);
  Uint8*p;
  Uint8*q;
  if(w<0) return (op<0);
  p=s->text+w;
  while((c=*p) && c!='=' && c!=';' && c>32) ++p;
  if(c!='=') return (op<0 || op=='|');
  q=++p;
  if(op!='=' && op!=-'=') u=parse_label_number(p);
  c=*p;
  if(c=='+' || c=='-' || c=='$') p++;
  while(*p && *p>='0' && *p<='f') p++,hi*=(c=='$'?16:10);
  hi--;
  if(c=='+' || c=='-') lo=-hi;
  switch(op) {
    case '=': clip: if(v<lo) v=lo; if(v>hi) v=hi; break;
    case '-': v=u-v; goto clip;
    case '+': v=u+v; goto clip;
    case -'-': v=u-v; break;
    case -'+': v=u+v; break;
    case '|': case -'|': v=u-v; if(v<0) return 1; break;
  }
  if(v<lo || v>hi) return (op<0);
  if(*q=='+' || *q=='-') {
    if(v<0) *q='-',v=-v; else *q='+';
    q++;
  }
  if(*q=='$') q++;
  c=(c=='$'?16:10);
  while(p>q) *--p="0123456789ABCDEF"[v%c],v/=c;
  return 0;
}

static Sint32 parse_number(Stat*s,StatXY*xy,Uint16*ip) {
  Sint32 v=0;
  Sint32 w=0;
  char op='+';
  int c,i;
  while(s->text[*ip]==' ') ++*ip;
  c=s->text[*ip];
  condflag=1;
  if(c=='$') {
    ++*ip;
    while(c=s->text[*ip]) {
      if(c>='0' && c<='9') v=(v<<4)+c-'0';
      else if(c>='A' && c<='F') v=(v<<4)+c+10-'A';
      else if(c>='a' && c<='f') v=(v<<4)+c+10-'a';
      else break;
      ++*ip;
    }
    return v;
  } else if((c>='0' && c<='9') || c=='-' || c=='+') {
    if(c=='-') w=-1; else w=1;
    if(c=='-' || c=='+') ++*ip;
    while(c=s->text[*ip]) {
      if(c>='0' && c<='9') v=10*v+c-'0';
      else break;
      ++*ip;
    }
    return v*w;
  } else if(c=='(') {
    operand:
    w=0;
    c=s->text[++*ip];
    if(c=='$') {
      ++*ip;
      while(c=s->text[*ip]) {
        if(c>='0' && c<='9') w=(w<<4)+c-'0';
        else if(c>='A' && c<='F') w=(w<<4)+c+10-'A';
        else if(c>='a' && c<='f') w=(w<<4)+c+10-'a';
        else break;
        ++*ip;
      }
    } else if(c>='0' && c<='9') {
      decimal:
      while(c=s->text[*ip]) {
        if(c>='0' && c<='9') w=10*w+c-'0';
        else break;
        ++*ip;
      }
    } else if(c=='+') {
      ++*ip;
      goto decimal;
    } else if(c=='-') {
      ++*ip;
      while(c=s->text[*ip]) {
        if(c>='0' && c<='9') w=10*w+'0'-c;
        else break;
        ++*ip;
      }
    } else if(c=='#') {
      c=s->text[++*ip];
      if(c>='A' && c<='H') w=status_vars[c-'A'];
      else if(c>='a' && c<='h') w=status_vars[c-'a'];
      else if(c>='S' && c<='Z') w=status_vars[c+8-'S'];
      else if(c>='s' && c<='z') w=status_vars[c+8-'s'];
      ++*ip;
    } else if(c=='C' || c=='c') {
      c=s->text[++*ip];
      if(c=='C' || c=='c') ++*ip,w=count_text_choices(); else goto badexp;
    } else if(c=='E' || c=='e') {
      ++*ip;
      w=xy->extra;
    } else if(c=='X' || c=='x') {
      c=s->text[++*ip];
      if(c=='X' || c=='x') ++*ip,w=stats->count?stats->xy->x:0; else w=xy->x;
    } else if(c=='Y' || c=='y') {
      c=s->text[++*ip];
      if(c=='Y' || c=='y') ++*ip,w=stats->count?stats->xy->y:0; else w=xy->y;
    } else if(c=='M' || c=='m') {
      c=s->text[++*ip];
      if(c=='1') w=s->misc1; else if(c=='2') w=s->misc2; else if(c=='3') w=s->misc3; else goto badexp;
      ++*ip;
    } else if(c=='P' || c=='p') {
      ++*ip;
      if(xy->x<board_info.width && xy->y<board_info.height && (w=xy->layer&3)) w=(w==1?b_under:w==2?b_main:b_over)[xy->y*board_info.width+xy->x].param;
    } else if(c=='@') {
      for(c=0;c<maxstat;c++) if(stats[c].text && match_name(stats[c].text,s->text+*ip+1)) {
        for(i=0;i<stats[c].count;i++) if(stats[c].xy[i].layer&3) w++;
      }
      while((c=s->text[*ip]) && c!=':' && c!='(' && c!=')' && c>32 && c<127) ++*ip;
      if(c!=':') goto badexp;
      ++*ip;
    } else if(c==':') {
      w=find_unzapped_label(s,s->text+ ++*ip);
      while((c=s->text[*ip]) && c!=':' && c!='(' && c!=')' && c>32 && c<127) ++*ip;
      if(c!=':') goto badexp;
      ++*ip;
      if(w<0) {
        w=0;
      } else {
        while((c=s->text[w]) && c!='=' && c!=';' && c>32) ++w;
        w=(c=='=')?parse_label_number(s->text+w+1):1;
      }
    } else {
      badexp:
      condflag=0;
      script_error(s+1-stats,xy,"Improper numeric expression");
      *ip=65535;
      return 0;
    }
    switch(op) {
      case '+': v+=w; break;
      case '-': v-=w; break;
      case '*': v*=w; break;
      case '/': if(!w) { script_error(s+1-stats,xy,"Division by zero"); goto badexp; } v/=w; break;
      case '%': if(w) v%=w; break;
      case '&': v&=w; break;
      case '|': v|=w; break;
      case '^': v^=w; break;
      case '<': if(w<32) v<<=w; else v=0; break;
      case '>': if(w<32) v>>=w; else v=(v<0?-1:0); break;
      case '?': if(v!=w) v+=dice(w+1-v); break;
      case 'd': case 'D': c=v; v=0; while(c-->0) v+=dice(w)+1; break;
      default: goto badexp;
    }
    op=s->text[*ip];
    if(op==')') {
      ++*ip;
      return v;
    }
    if(op<33) goto badexp;
    goto operand;
  } else {
    return condflag=0;
  }
}

static Sint32 parse_item(Stat*s,StatXY*xy,Uint16*ip) {
  Uint32 f=0;
  char op='+';
  int c,i,j;
  while(s->text[*ip]==' ') ++*ip;
  again:
  c=s->text[*ip];
  if(c=='<') {
    switch(c=s->text[++*ip]) {
      case '0' ... '7': f|=0x10000L<<(c-'0'); break;
      case 'F': case 'f': if((s->text[1+*ip]&~32)=='I' && (s->text[2+*ip]&~32)=='X') f|=ISF_FIXED<<16,*ip+=2; else goto bad; break;
      case 'H': case 'h': if((s->text[1+*ip]&~32)=='I' && (s->text[2+*ip]&~32)=='D') f|=ISF_HIDDEN<<16,*ip+=2; else goto bad; break;
      case 'I': case 'i': if((s->text[1+*ip]&~32)=='G' && (s->text[2+*ip]&~32)=='N') f|=ISF_IGNORE<<16,*ip+=2; else goto bad; break;
      case 'L': case 'l': if((s->text[1+*ip]&~32)=='I' && (s->text[2+*ip]&~32)=='T') f|=ISF_HILIGHT<<16,*ip+=2; else goto bad; break;
      case 'U': case 'u': if((s->text[1+*ip]&~32)=='S' && (s->text[2+*ip]&~32)=='E') f|=ISF_IN_USE<<16,*ip+=2; else goto bad; break;
      default: goto bad;
    }
    if(s->text[++*ip]!='>') goto bad;
    ++*ip;
    goto again;
  } else if(c==91 || c=='"') {
    if(c==91) c=93;
    for(++*ip,i=0;i<nitemdefs;i++) if(itemdefs[i].class!=255 && itemdefs[i].name && itemnames[itemdefs[i].name]==s->text[*ip]) {
      for(j=1;s->text[j+*ip]!=c && itemnames[j+itemdefs[i].name] && itemnames[j+itemdefs[i].name]==s->text[j+*ip];j++);
      if(s->text[j+*ip]==c && !itemnames[j+itemdefs[i].name]) return *ip+=j+1,condflag=1,(f|(i+1));
    }
    bad: script_error(s+1-stats,xy,"Improper item specification");
    return condflag=0;
  } else {
    return f|parse_number(s,xy,ip);
  }
}

static Sint32 parse_letter(Stat*s,StatXY*xy,Uint16*ip) {
  int c;
  while(s->text[*ip]==' ') ++*ip;
  c=s->text[*ip];
  condflag=1;
  if(c>='A' && c<='H') {
    ++*ip;
    return c-'A';
  } else if(c>='S' && c<='Z') {
    ++*ip;
    return c+8-'S';
  } else if(c>='a' && c<='h') {
    ++*ip;
    return c-'a';
  } else if(c>='s' && c<='z') {
    ++*ip;
    return c+8-'s';
  }
  script_error(s+1-stats,xy,"Improper #GIVE or #TAKE");
  return condflag=0;
}

typedef struct {
  Uint8 color,kind,param,stat;
  Uint8 cmask,kmask,pmask,smask;
  Uint8 stats[256/8];
  Uint16 label;
} ScriptKind;

static char parse_kind(Stat*s,StatXY*xy,Uint16*ip,ScriptKind*sk,char cre) {
  Uint16 bip=*ip;
  char buf[16];
  int c,n;
  while(s->text[*ip]==' ') ++*ip;
  sk->color=sk->kind=sk->param=sk->stat=0;
  sk->cmask=sk->kmask=sk->pmask=sk->smask=255;
  for(c=0;c<256/8;c++) sk->stats[c]=255;
  sk->label=0;
  if(s->text[*ip]=='<') {
    c=s->text[++*ip];
    if(c>='0' && c<='9') sk->color=(c-'0')<<4,sk->cmask&=0x0F;
    else if(c>='A' && c<='F') sk->color=(c+10-'A')<<4,sk->cmask&=0x0F;
    else if(c>='a' && c<='f') sk->color=(c+10-'a')<<4,sk->cmask&=0x0F;
    else if(c!='?') goto bad;
    c=s->text[++*ip];
    if(c>='0' && c<='9') sk->color+=(c-'0'),sk->cmask&=0xF0;
    else if(c>='A' && c<='F') sk->color+=(c+10-'A'),sk->cmask&=0xF0;
    else if(c>='a' && c<='f') sk->color+=(c+10-'a'),sk->cmask&=0xF0;
    else if(c!='?') goto bad;
    if(s->text[++*ip]!='>') goto bad;
    ++*ip;
  }
  if(s->text[*ip]=='@' && cre!=2) {
    if(!cre) for(c=0;c<256/8;c++) sk->stats[c]=0;
    for(n=0;n<maxstat;n++) if(stats[n].length && match_name(stats[n].text,s->text+*ip+1)) {
      if(cre) {
        Uint16 w;
        ScriptKind sk1;
        sk->stat=n+1;
        sk->smask=0;
        for(w=1;;w++) {
          if(w==stats[n].length || stats[n].text[w]=='\n' || stats[n].text[w]=='\r') goto bad;
          if(stats[n].text[w]=='=') break;
        }
        w++;
        if(!parse_kind(stats+n,xy,&w,&sk1,2)) goto bad;
        if(sk->cmask==255) sk->cmask=sk1.cmask,sk->color=sk1.color;
        sk->kmask=sk1.kmask,sk->kind=sk1.kind;
        sk->pmask=sk1.pmask,sk->param=sk1.param;
        if(sk->kmask) goto bad;
        break;
      } else {
        sk->stats[(n+1)/8]|=1<<((n+1)&7);
      }
    }
    if(n==maxstat && !cre) goto bad;
    while((c=s->text[*ip]) && c!=' ' && c!='=' && c!=':' && c!='<' && c!='\n' && c!='\r') ++*ip;
    if(cre && s->text[*ip]==':') {
      Sint32 b;
      if(!stats[n].text) goto bad;
      b=find_label(stats+n,s->text+*ip+1);
      if(b!=-1) sk->label=b;
      while((c=s->text[*ip]) && c!=' ' && c!='=' && c!=':' && c!='<' && c!='(' && c!=')' && c!='\n' && c!='\r') ++*ip;
    }
  } else {
    for(n=0;n<15;n++) {
      c=s->text[*ip];
      if(c>='a' && c<='z') c+='A'-'a';
      if((c<'0' || c>'9') && (c<'A' || c>'Z') && c!='_') break;
      buf[n]=c;
      ++*ip;
    }
    buf[n]=0;
    for(n=0;n<256;n++) if(elem_def[n].name[0] && !strcmp(buf,elem_def[n].name)) break;
    if(n==256) goto bad;
    sk->kind=n;
    sk->kmask=0;
  }
  if(s->text[*ip]=='<') {
    sk->param=0,sk->pmask=255;
    c=s->text[++*ip];
    if(c>='0' && c<='9') sk->param=(c-'0')<<4,sk->pmask&=0x0F;
    else if(c>='A' && c<='F') sk->param=(c+10-'A')<<4,sk->pmask&=0x0F;
    else if(c>='a' && c<='f') sk->param=(c+10-'a')<<4,sk->pmask&=0x0F;
    else if(c!='?') goto bad;
    c=s->text[++*ip];
    if(c>='0' && c<='9') sk->param+=(c-'0'),sk->pmask&=0xF0;
    else if(c>='A' && c<='F') sk->param+=(c+10-'A'),sk->pmask&=0xF0;
    else if(c>='a' && c<='f') sk->param+=(c+10-'a'),sk->pmask&=0xF0;
    else if(c!='?') goto bad;
    if(s->text[++*ip]!='>') goto bad;
    ++*ip;
  } else if(cre && !sk->stat) {
    sk->pmask=sk->param=0;
  }
  if(s->text[*ip]=='!' && !cre) {
    ++*ip;
    sk->stats[0]&=0xFE;
  }
  return 1;
  bad: *ip=bip; return 0;
}

static void change_to_script_kind(Uint32 x,Uint32 y,Uint8 lay,const ScriptKind*sk) {
  Uint8 zc,zp;
  Uint32 at=y*board_info.width+x;
  StatXY*o=0;
  if(x>=board_info.width || y>=board_info.height || sk->kmask) return;
  if(sk->kind==245 && !sk->kmask && !sk->pmask) {
    zone_add(x,y,sk->param,1);
  } else if(lay==2) {
    // Main
    if(elem_def[b_main[at].kind].attrib&A_PERMANENT) return;
    zc=b_main[at].color;
    zp=b_main[at].param;
    break_tile(at,2,0,0,0);
    if(sk->stat) {
      o=add_statxy(sk->stat);
      o->x=x; o->y=y; o->layer=lay; o->instptr=sk->label;
    }
    if(A_FLOOR&elem_def[b_main[at].kind].attrib&~elem_def[sk->kind].attrib) {
      if(b_main[at].stat) if(o=find_statxy(b_main+at)) o->layer--;
      b_under[at]=b_main[at];
    } else if(b_main[at].stat) {
      break_tile(at,2,0,0,1);
    }
    b_main[at].color=(zc&sk->cmask)|sk->color;
    b_main[at].kind=sk->kind;
    b_main[at].param=(zc&sk->pmask)|sk->param;
    b_main[at].stat=sk->stat;
  } else if(lay==1) {
    // Under
    if(elem_def[b_under[at].kind].attrib&A_PERMANENT) return;
    zc=b_under[at].color;
    zp=b_under[at].param;
    break_tile(at,1,0,0,0);
    if(sk->stat) {
      o=add_statxy(sk->stat);
      o->x=x; o->y=y; o->layer=lay; o->instptr=sk->label;
    }
    b_under[at].color=(zc&sk->cmask)|sk->color;
    b_under[at].kind=sk->kind;
    b_under[at].param=(zc&sk->pmask)|sk->param;
    b_under[at].stat=sk->stat;
  } else if(lay==3) {
    // Over; #BECOME, #CHANGE, #PUT, etc should not be used
    break_tile(at,3,0,0,1);
    b_over[at].stat=0;
  }
}

static void put_script_kind(Uint32 x,Uint32 y,const ScriptKind*sk) {
  Uint8 zc,zp;
  Uint32 at=y*board_info.width+x;
  StatXY*o=0;
  if(x>=board_info.width || y>=board_info.height || sk->kmask) return;
  if(elem_def[b_main[at].kind].attrib&A_PERMANENT) return;
  zc=b_main[at].color;
  zp=b_main[at].param;
  if(A_FLOOR&elem_def[b_main[at].kind].attrib&~elem_def[sk->kind].attrib) {
    if(b_main[at].stat) if(o=find_statxy(b_main+at)) o->layer--;
    b_under[at]=b_main[at];
  } else if(b_main[at].stat) {
    break_tile(at,2,0,0,1);
  }
  b_main[at].color=(zc&sk->cmask)|sk->color;
  b_main[at].kind=sk->kind;
  b_main[at].param=(zc&sk->pmask)|sk->param;
  b_main[at].stat=sk->stat;
}

static char match_script_kind(Uint32 at,Uint8 lay,const ScriptKind*sk) {
  // Return 1 if match or 0 if not match
  const Tile*t;
  if(lay>1 && sk->kind==245 && !sk->kmask && !sk->pmask) return in_zone(at%board_info.width,at/board_info.width,sk->param);
  if(lay==2) t=b_main+at; else if(lay==1) t=b_under+at; else return 0;
  if((t->kind|sk->kmask)!=(sk->kind|sk->kmask)) return 0;
  if((t->color|sk->cmask)!=(sk->color|sk->cmask)) return 0;
  if((t->param|sk->pmask)!=(sk->param|sk->pmask)) return 0;
  if(!(sk->stats[t->stat/8]&(1<<(t->stat&7)))) return 0;
  return 1;
}

static Uint32 count_script_kind(Uint8 lay,const ScriptKind*sk) {
  Uint32 a,n;
  Uint32 c=board_info.width*board_info.height;
  for(a=n=0;a<c;a++) n+=match_script_kind(a,lay,sk);
  return n;
}

static char parse_condition(Stat*s,StatXY*xy,Uint16*ip) {
  ScriptKind sk;
  Uint16 bip;
  char buf[128]={};
  char inv=0;
  char v=0;
  int c,n;
  Sint32 z0,z1;
  Uint32 z;
  while(s->text[*ip]==' ') ++*ip;
  if(s->text[*ip]=='!') inv=1,++*ip;
  bip=*ip;
  for(n=0;n<127;n++) {
    c=s->text[*ip];
    if(c<=32 || c==0x7D) break;
    if(c>='a' && c<='z') c+='A'-'a';
    buf[n]=c;
    ++*ip;
  }
  buf[n]=0;
  if(*buf=='#') {
    if(!strcmp(buf+1,"ALIGN")) {
      if(stats->count && (stats->xy->x==xy->x || stats->xy->y==xy->y)) v=1;
    } else if(!strncmp(buf+1,"ANY:",4)) {
      n=5; any: *ip=bip+n;
      if(!parse_kind(s,xy,ip,&sk,0)) goto bad;
      if(count_script_kind(1,&sk) || count_script_kind(2,&sk)) v=1;
    } else if(!strncmp(buf+1,"AT:",3)) {
      *ip=bip+4;
      z0=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(s->text[*ip]!=':') goto bad;
      ++*ip;
      z1=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(s->text[*ip]!=':') goto bad;
      if(!parse_kind(s,xy,ip,&sk,0)) goto bad;
      if(z0>=0 && z1>=0 && z0<board_info.width && z1<board_info.height) {
        z=z1*board_info.width+z0;
        v=(match_script_kind(z,1,&sk) || match_script_kind(z,2,&sk));
      }
    } else if(!strncmp(buf+1,"BENEATH:",8)) {
      n=9; beneath: *ip=bip+n;
      if(!parse_kind(s,xy,ip,&sk,0)) goto bad;
      if((xy->layer&2) && xy->x<board_info.width && xy->y<board_info.height) v=match_script_kind(xy->y*board_info.width+xy->x,1,&sk);
    } else if(!strncmp(buf+1,"BLOCKED:",8)) {
      *ip=bip+9;
      c=parse_direction(s,xy,ip);
      if(!condflag) goto bad;
      v=check_blocked_at(xy->x+(c==DIR_E)-(c==DIR_W),xy->y+(c==DIR_S)-(c==DIR_N));
    } else if(!strncmp(buf+1,"BLOCKEDAT:",10)) {
      *ip=bip+11;
      z0=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(s->text[*ip]!=':') goto bad;
      ++*ip;
      z1=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      v=check_blocked_at(z0,z1);
    } else if(!strcmp(buf+1,"CONTACT")) {
      if(xy->x<board_info.width && xy->y<board_info.height) {
        z=xy->y*board_info.width+xy->x;
        if(xy->x>0 && b_main[z-1].stat==1) v=1;
        if(xy->x<board_info.width-1 && b_main[z+1].stat==1) v=1;
        if(xy->y>0 && b_main[z-board_info.width].stat==1) v=1;
        if(xy->y<board_info.height-1 && b_main[z+board_info.width].stat==1) v=1;
        if(b_main[z].stat==1 || b_under[z].stat==1 || b_over[z].stat==1) v=1;
      }
    } else if(!strncmp(buf+1,"EVENT:",6)) {
      *ip=bip+7;
      z0=parse_item(s,xy,ip);
      if(!condflag) goto bad;
      if(z0>0 && z0<=nitemdefs && (itemdefs[z0-1].flag&IDF_EVENT)) v=1;
    } else if(!strcmp(buf+1,"FULL")) {
      for(v=1,n=0;n<16 && v;n++) if(!namedflag[n].name[0]) v=0;
    } else if(!strcmp(buf+1,"LOCKED")) {
      if(xy->layer&0x80) v=1;
    } else if(!strncmp(buf+1,"INZONE:",7)) {
      *ip=bip+8;
      z0=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(in_zone(xy->x,xy->y,z0)) v=1;
    } else if(!strncmp(buf+1,"MAIN:",5)) {
      n=6; main: *ip=bip+n;
      if(!parse_kind(s,xy,ip,&sk,0)) goto bad;
      if(count_script_kind(2,&sk)) v=1;
    } else if(!strcmp(buf+1,"NOSAVE")) {
      if(memory[MEM_CONTROL]&CONTROL_DISABLE_SAVING) v=1;
    } else if(!strcmp(buf+1,"NOSCROLL")) {
      if(memory[MEM_CONTROL]&CONTROL_NOSCROLL) v=1;
    } else if(!strcmp(buf+1,"OVERLAY")) {
      if(board_info.flag&BF_OVERLAY) v=1;
    } else if(!strcmp(buf+1,"PERSIST")) {
      if(board_info.flag&BF_PERSIST) v=1;
    } else if(!strncmp(buf+1,"PLAYER:",7)) {
      n=8; player: *ip=bip+n;
      if(!parse_kind(s,xy,ip,&sk,0)) goto bad;
      for(z=0;z<stats->count && !v;z++)
       if((stats->xy[z].layer&3)==2 && stats->xy[z].x<board_info.width && stats->xy[z].y<board_info.height && match_script_kind(stats->xy[z].y*board_info.width+stats->xy[z].x,1,&sk)) v=1;
    } else if(!strncmp(buf+1,"PLAYERINZONE:",13)) {
      *ip=bip+14;
      z0=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(maxstat && stats->count && in_zone(stats->xy->x,stats->xy->y,z0)) v=1;
    } else if(!strncmp(buf+1,"PUSHABLE:",9)) {
      *ip=bip+10;
      c=parse_direction(s,xy,ip);
      if(!condflag) goto bad;
      v=check_pushable_at(xy->x+(c==DIR_E)-(c==DIR_W),xy->y+(c==DIR_S)-(c==DIR_N),c&1?A_PUSH_NS:A_PUSH_EW);
    } else if(!strcmp(buf+1,"RANDOM")) {
      v=dice(2);
    } else if(!strcmp(buf+1,"SENT")) {
      if(memory[MEM_CONTROL]&CONTROL_SENT) v=1;
    } else if(!strncmp(buf+1,"UNDER:",6)) {
      n=7; under: *ip=bip+n;
      if(!parse_kind(s,xy,ip,&sk,0)) goto bad;
      if(count_script_kind(1,&sk)) v=1;
    } else if(!strcmp(buf+1,"USER")) {
      if(xy->layer&0x40) v=1;
    } else if(!strcmp(buf+1,"WALK")) {
      if(xy->layer&0x10) v=1;
    } else if(!strncmp(buf+1,"FACING:",7)) {
      *ip=bip+8;
      c=parse_direction(s,xy,ip);
      if(!condflag) goto bad;
      v=(c==((xy->layer>>2)&3)?1:0);
    } else if(!strncmp(buf+1,"WALKING:",8)) {
      *ip=bip+9;
      c=parse_direction(s,xy,ip);
      if(!condflag) goto bad;
      if(xy->layer&0x10) v=(c==((xy->layer>>2)&3)?1:0); else v=(c==-1?1:0);
    } else if(!strncmp(buf+1,"VISIBLE",7)) {
      if(xy->x>=scroll_x && xy->x<scroll_x+80 && xy->y>=scroll_y && xy->y<scroll_y+25 && xy->x<board_info.width && xy->y<board_info.height && (xy->layer&2)) {
        inv^=xy->layer&1;
        n=cur_screen.command[z=(xy->x-scroll_x)+(xy->y-scroll_y)*80];
        if((n&0xF0)==SC_BOARD) {
          if((n&4) || (elem_def[b_main[z].kind].attrib&A_LIGHT) || !(board_info.flag&BF_OVERLAY) || ((xy->layer&1) && !(b_over[z].kind&OVER_VISIBLE))) {
            if((xy->layer&1) && (elem_def[b_main[z].kind].app[0]&0x3F)==AP_OVER) v=0; else v=1;
          } else if(memory[MEM_LIGHT]<65486 && maxstat && stats->count) {
            z0=xy->y-stats->xy->y-scroll_y;
            if(z0>-25 && z0<25) {
              z1=memory[memory[MEM_LIGHT]+z0+24];
              z0=128+xy->x-stats->xy->x-scroll_x;
              if(z0>=(z1>>8) && z0<=(z1&0xFF)) v=1;
            }
          }
        }
      }
    } else {
      bad: script_error(s+1-stats,xy,"Improper condition");
    }
  } else if(*buf=='@') {
    n=1; goto any;
  } else if(*buf==':') {
    v=(find_label(s,buf+1)==-1?0:1);
  } else if(buf[1]=='@') {
    n=2;
    if(*buf=='B') goto beneath;
    if(*buf=='M') goto main;
    if(*buf=='P') goto player;
    if(*buf=='U') goto under;
    if(*buf>='0' && *buf<='9') {
      *ip=bip+1;
      z0=*buf-'0';
      goto flagpos;
    }
    goto bad;
  } else if(*buf>='A' && *buf<='Z' && buf[1]!='@' && !buf[15]) {
    for(n=0;n<16 && !v;n++) if(!strcmp(namedflag[n].name,buf)) v=1;
  } else if((*buf>='0' && *buf<='9') || *buf=='$' || *buf=='-' || *buf=='+' || *buf=='(') {
    *ip=bip;
    z0=parse_number(s,xy,ip);
    if(!condflag) goto bad;
    if(s->text[*ip]=='@') {
      flagpos:
      if(z0&~15L) goto bad;
      if(buf[n=*ip-bip]!='@') goto bad;
      *ip=bip+strlen(buf);
      if(buf[n+1]) {
        if(!strcmp(namedflag[z0].name,buf+n+1)) v=1;
      } else {
        if(namedflag[z0].name[0]) v=1;
      }
    } else {
      z=0;
      if(s->text[*ip]=='<') z|=2,++*ip;
      if(s->text[*ip]=='>') z|=4,++*ip;
      if(s->text[*ip]=='=') z|=1,++*ip;
      if(z==7 || !z) goto bad;
      z1=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(z0==z1 && (z&1)) v=1;
      if(z0<z1 && (z&2)) v=1;
      if(z0>z1 && (z&4)) v=1;
    }
  } else {
    goto bad;
  }
  return v^inv;
}

static void script_set_flag(Stat*s,StatXY*xy,Uint16*ip,char v) {
  Uint16 bip;
  char buf[128]={};
  int c,n;
  while(s->text[*ip]==' ') ++*ip;
  if(s->text[*ip]=='!') v^=1,++*ip;
  bip=*ip;
  for(n=0;n<127;n++) {
    c=s->text[*ip];
    if(c<=32) break;
    if(c>='a' && c<='z') c+='A'-'a';
    buf[n]=c;
    ++*ip;
  }
  if(!n) return;
  buf[n]=0;
  if(*buf=='#') {
    if(!strncmp(buf+1,"EVENT:",6)) {
      *ip=bip+7;
      n=parse_item(s,xy,ip);
      if(!condflag) goto bad;
      if(n>0 && n<=nitemdefs) {
        if(v) itemdefs[n-1].flag|=IDF_EVENT; else itemdefs[n-1].flag&=~IDF_EVENT;
      }
    } else if(!strncmp(buf+1,"INZONE:",7)) {
      *ip=bip+8;
      n=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(v) zone_add(xy->x,xy->y,n,1); else zone_remove(xy->x,xy->y,n);
    } else if(!strcmp(buf+1,"LOCKED")) {
      if(v) xy->layer|=0x80; else xy->layer&=0x7F;
    } else if(!strcmp(buf+1,"NOSAVE")) {
      if(v) memory[MEM_CONTROL]|=CONTROL_DISABLE_SAVING; else memory[MEM_CONTROL]&=~CONTROL_DISABLE_SAVING;
    } else if(!strcmp(buf+1,"NOSCROLL")) {
      if(v) memory[MEM_CONTROL]|=CONTROL_NOSCROLL; else memory[MEM_CONTROL]&=~CONTROL_NOSCROLL;
    } else if(!strcmp(buf+1,"OVERLAY")) {
      if(v) board_info.flag|=BF_OVERLAY; else board_info.flag&=~BF_OVERLAY;
    } else if(!strcmp(buf+1,"PERSIST")) {
      if(v) board_info.flag|=BF_PERSIST; else board_info.flag&=~BF_PERSIST;
    } else if(!strncmp(buf+1,"PLAYERINZONE:",13)) {
      *ip=bip+14;
      n=parse_number(s,xy,ip);
      if(!condflag) goto bad;
      if(maxstat && stats->count) {
        if(v) zone_add(stats->xy->x,stats->xy->y,n,1); else zone_remove(stats->xy->x,stats->xy->y,n);
      }
    } else if(!strcmp(buf+1,"SENT")) {
      if(v) memory[MEM_CONTROL]|=CONTROL_SENT; else memory[MEM_CONTROL]&=~CONTROL_SENT;
    } else if(!strcmp(buf+1,"USER")) {
      if(v) xy->layer|=0x40; else xy->layer&=0xBF;
    } else if(!strcmp(buf+1,"WALK")) {
      if(v) xy->layer|=0x10; else xy->layer&=0xEF;
    } else {
      bad: script_error(s+1-stats,xy,"Improper #SET or #CLEAR");
    }
  } else if(*buf>='A' && *buf<='Z' && buf[1]!='@' && !buf[15]) {
    c=16;
    for(n=0;n<16;n++) {
      if(!strcmp(namedflag[n].name,buf)) {
        if(!v) memset(namedflag[n].name,0,16);
        return;
      }
      if(c==16 && !namedflag[n].name[0]) c=n;
    }
    if(v && c!=16) memcpy(namedflag[c].name,buf,16);
  } else if((*buf>='0' && *buf<='9') || *buf=='$' || *buf=='-' || *buf=='+' || *buf=='(') {
    *ip=bip;
    c=parse_number(s,xy,ip);
    if((c&~15L) || s->text[*ip]!='@') goto bad;
    if(buf[n=*ip-bip]!='@') goto bad;
    *ip=bip+strlen(buf);
    if(buf[n+1]) {
      if(!v && !strcmp(namedflag[c].name,buf+n+1)) memset(namedflag[c].name,0,16);
      if(v) memcpy(namedflag[c].name,buf+n+1,16);
    } else {
      if(v) goto bad;
      memset(namedflag[c].name,0,16);
    }
  } else {
    goto bad;
  }
}

static char parse_name(Stat*s,StatXY*xy,Uint16*ip) {
  Uint8 c;
  while(s->text[*ip]==' ') ++*ip;
  for(ntextbuf=0;ntextbuf<80;ntextbuf++) {
    c=s->text[*ip];
    if(c>='a' && c<='z') textbuf[ntextbuf]=c+'A'-'a';
    else if(c>32 && c<127) textbuf[ntextbuf]=c;
    else break;
    ++*ip;
  }
  textbuf[ntextbuf]=0;
  return ntextbuf?1:0;
}

static void script_set_music(Stat*s,StatXY*xy,Uint16*ip) {
  Uint16 w;
  parse_name(s,xy,ip);
  if(!strcmp(textbuf,"STOP")) {
    audio_set_music(0,0);
  } else if(!strcmp(textbuf,"PLAY")) {
    parse_name(s,xy,ip);
    w=parse_number(s,xy,ip);
    if(w!=music_song || strcmp(music_name,textbuf)) audio_set_music(textbuf,w);
  } else if(!strcmp(textbuf,"REPLAY")) {
    parse_name(s,xy,ip);
    w=parse_number(s,xy,ip);
    audio_set_music(textbuf,w);
  } else if(!strcmp(textbuf,"SKIP")) {
    audio_set_music("",music_song);
  } else if(!strcmp(textbuf,"RESET") || !strcmp(textbuf,"SET")) {
    int m=*textbuf;
    int i;
    VarProperty*v;
    for(i=0;i<board_info.varprop.count;i++) if((board_info.varprop.item[i].type>>4)==4) break;
    if(i==board_info.varprop.count) return;
    v=board_info.varprop.item+i;
    if(ntextbuf>8) textbuf[ntextbuf=8]=0;
    if(parse_name(s,xy,ip)) {
      w=parse_number(s,xy,ip);
      if(v->type==0x40) v->data[0]=(m=='R'?0xC0:0x80);
      if(w>=63) {
        v->data[0]|=63;
        v->data[1]=w; v->data[2]=w>>8;
        i=3;
      } else {
        v->data[0]=w+(v->data[0]&0xC0);
        i=1;
      }
      v->type=ntextbuf+i+0x40;
      memcpy(v->data+i,textbuf,9);
    } else {
      if(v->type==0x40) audio_set_music(0,0);
      if(v->type<0x42 || v->type>0x4B) return;
      v->data[v->type&15]=0;
      if((w=v->data[0]&63)==63) w=(v->data[1])|(v->data[2]<<8),i=3; else i=1;
      strncpy(textbuf,v->data+i,9);
      ntextbuf=strlen(textbuf);
    }
    if(m=='R' || w!=music_song || strcmp(music_name,textbuf)) audio_set_music(textbuf,w);
  } else if(!strcmp(textbuf,"CANCEL")) {
    int i;
    audio_set_music(0,0);
    for(i=0;i<board_info.varprop.count;i++) if((board_info.varprop.item[i].type>>4)==4) board_info.varprop.item[i].type=0x40;
  }
}

static void script_do_editfont(Stat*s,StatXY*xy,Uint16*ip) {
  Uint16 w;
  Uint8 m=parse_number(s,xy,ip);
  Uint8*f=font+(m&0xFF)*14;
  Uint8 n;
  for(n=0;n<7;n++) {
    w=parse_number(s,xy,ip);
    *f++=w; *f++=w>>8;
  }
}

static void script_do_erase(const ScriptKind*sk) {
  Uint32 at;
  Uint32 m=board_info.width*board_info.height;
  for(at=0;at<m;at++) {
    if(match_script_kind(at,1,sk)) break_tile(at,1,0,0,0);
    if(match_script_kind(at,2,sk)) break_tile(at,2,0,0,0);
  }
}

static void script_do_change(const ScriptKind*sk,const ScriptKind*sk1) {
  Uint32 at;
  Uint32 m=board_info.width*board_info.height;
  for(at=0;at<m;at++) {
    if(match_script_kind(at,1,sk)) change_to_script_kind(at%board_info.width,at/board_info.width,1,sk1);
    if(match_script_kind(at,2,sk)) change_to_script_kind(at%board_info.width,at/board_info.width,2,sk1);
  }
}

static inline void dieitem(Uint16 m,Uint16 n) {
  StatXY xy=stats[m-1].xy[n];
  StatXY yx;
  StatXY*r=stats[m-1].xy+n;
  Uint32 a,b;
  if(xy.x>=board_info.width || xy.y>=board_info.height || !(xy.layer&3) || (stats[m-1].mode&STAT_INDEPENDENT)) {
    r->x=r->y=r->instptr=65535; r->layer=128; r->delay=255;
    return;
  }
  a=xy.y*board_info.width+xy.x;
  if(stats->count && (stats->xy->layer&3)) {
    yx=stats->xy[0];
    b=yx.y*board_info.width+yx.x;
    if(yx.x>=board_info.width || yx.y>=board_info.height) {
      break_tile(0,0,m,n,0);
    } else if((xy.layer&3)==3 && (yx.layer&3)==3) {
      stats->xy->x=xy.x; stats->xy->y=xy.y;
      r->x=r->y=r->instptr=65535; r->layer=128; r->delay=255;
      b_over[a]=b_over[b];
      b_over[b].kind&=OVER_BG_THRU;
      if(memory[MEM_DEFAULT_OVERLAY]) b_over[b].kind|=OVER_VISIBLE;
      b_over[b].color=memory[MEM_DEFAULT_OVERLAY]>>8;
      b_over[b].param=memory[MEM_DEFAULT_OVERLAY];
    } else if((xy.layer&3)!=3 && (yx.layer&3)!=3) {
      stats->xy->x=xy.x; stats->xy->y=xy.y;
      stats->xy->layer=(yx.layer&~3)|(xy.layer&3);
      r->x=r->y=r->instptr=65535; r->layer=128; r->delay=255;
      (xy.layer&2?b_main:b_under)[a]=(yx.layer&2?b_main:b_under)[b];
      if(yx.layer&2) {
        if(b_under[b].stat) if(r=find_statxy(b_under+b)) r->layer++;
        b_main[b]=b_under[b];
      }
      b_under[b]=(Tile){0,0,0,0};
    } else {
      break_tile(0,0,m,n,0);
    }
  } else {
    break_tile(0,0,m,n,0);
  }
}

static void mix_text_choices(void) {
  char w[TEXTREC];
  char*t;
  int i,s,m;
  fclose(textfile);
  if(!textfile_text) err(1,"Allocation failed");
  t=textfile_text;
  s=textfile_size/TEXTREC;
  textfile_text=0;
  textfile_size=0;
  textfile=open_memstream(&textfile_text,&textfile_size);
  if(!textfile) err(1,"Allocation failed");
  for(m=s-1;m>=0 && t[m*TEXTREC] && t[m*TEXTREC+1]=='!';m--);
  for(m++;m<s-1;m++) {
    i=m+dice(s-m);
    if(i!=m) {
      memcpy(w,t+i*TEXTREC,TEXTREC);
      memcpy(t+i*TEXTREC,t+m*TEXTREC,TEXTREC);
      memcpy(t+m*TEXTREC,w,TEXTREC);
    }
  }
  fwrite(t,s,TEXTREC,textfile);
  free(t);
}

static void auto_text_choice(Uint32 n) {
  char*t;
  int i,s;
  fclose(textfile);
  textfile=0;
  if(!textfile_text) err(1,"Allocation failed");
  t=textfile_text;
  s=textfile_size/TEXTREC;
  textfile_text=0;
  textfile_size=0;
  for(i=0;n>0 && i<s;i++) if(t[i*TEXTREC] && t[i*TEXTREC+1]=='!' && !--n) break;
  ntextbuf=0;
  if(i<s && !n) {
    s=2; i*=TEXTREC; if(t[i+2]=='<' && t[i+4]=='>') s=5;
    for(ntextbuf=0;ntextbuf+s<t[i] && t[i+s+ntextbuf]!=';' && ntextbuf<80;ntextbuf++);
    memcpy(textbuf,t+i+s,ntextbuf); textbuf[ntextbuf]=0;
  }
  textbuf[ntextbuf]=0;
  free(t);
}

static int append_escaped(char*buf,int len,Stat*s,StatXY*xy,Uint16 ip) {
  Sint32 u,v;
  switch(s->text[ip++]) {
    case 'B': case 'b': // board name
      if(s->text[ip]==0x7D) {
        v=cur_board_id;
      } else if(s->text[ip]=='N' || s->text[ip]=='n') {
        v=board_info.exits[DIR_N];
      } else if(s->text[ip]=='S' || s->text[ip]=='s') {
        v=board_info.exits[DIR_S];
      } else if(s->text[ip]=='E' || s->text[ip]=='e') {
        v=board_info.exits[DIR_E];
      } else if(s->text[ip]=='W' || s->text[ip]=='w') {
        v=board_info.exits[DIR_W];
      } else {
        v=parse_number(s,xy,&ip);
      }
      return (boardnames && v>0 && v<=maxboard && boardnames[v])?snprintf(buf,len+1,"%s",(char*)boardnames[v]):0;
    case 'C': case 'c': // character
      v=parse_number(s,xy,&ip);
      return (*buf=v&0xFF)?1:0;
    case 'D': case 'd': // dynamic string
      v=parse_number(s,xy,&ip)&255;
      if(!v || v>ndynastr || !dynastr[v-1].len) return 0;
      if(len>dynastr[v-1].len) len=dynastr[v-1].len;
      memcpy(buf,dynastr[v-1].text,len);
      return len;
    case 'N': case 'n': // number
      v=parse_number(s,xy,&ip);
      if(s->text[ip]!=',') return snprintf(buf,len+1,"%lld",(long long)v);
      ip++;
      u=parse_number(s,xy,&ip);
      return snprintf(buf,len+1,"%*lld",(int)u,(long long)v);
    case 0x7D: // end of condition
      return 0;
    default: bad:
      script_error(s+1-stats,xy,"Improper {} in text");
      return 0;
  }
}

static void run_script(Uint16 m,Uint16 n,Sint32 u) {
  // m=stat number, n=XY index, u=(<0 if imply #, =0 if restart, >0 if normal)
  char buf[128];
  Stat*s=stats+m-1;
  StatXY*xy=s->xy+n;
  StatXY*xy2;
  Uint16 ip=xy->instptr;
  Uint16 bip;
  Uint8 c,v;
  Uint8 esc=0;
  Uint8 stop=0;
  Uint16 w;
  Sint32 y;
  int i,j;
  ScriptKind sk,sk1;
  if(!u) xy->instptr=ip=0;
  if(!s->text) return;
  if(ip>=s->length) {
    xy->instptr=0xFFFF;
    return;
  }
  begin:
  if(u<0) while(s->text[ip]==' ') ++ip;
  switch(c=s->text[bip=ip]) {
    case 0: goto stop;
    case '\r': case '\n': ++ip; u=0; goto begin;
    case '#':
      ip++; // fall through
    command:
      if(s->text[ip]=='=') {
        ip++;
        if(s->text[ip]==' ') ip++;
        send:
        *buf='*';
        for(v=1;v<126 && ip<s->length && s->text[ip]>=0x20;v++) {
          buf[v]=s->text[ip++];
          if(buf[v]==':') *buf=0;
        }
        buf[v]=0;
        if(s->text[xy->instptr=ip]) ip++;
        send_message((n<<16)+m,buf+(*buf?0:1),0);
        ip=xy->instptr;
      } else {
        for(v=0;v<64;v++,ip++) {
          c=s->text[ip];
          if((c>='A' && c<='Z') || (c>='0' && c<='9')) buf[v]=c;
          else if(c>='a' && c<='z') buf[v]=c+'A'-'a';
          else break;
        }
        buf[v]=0;
        if(s->text[ip]==' ') ip++;
        if(w=memory[MEM_CUSTOM_COMMAND]) while(w && w<0xFFFE) {
          if(memory[w+1]>=ngtext || strcmp(buf,gtext[memory[w+1]])) {
            w=memory[w];
          } else {
            xy->instptr=ip;
            v=run_program(w+2,(n<<16)+m,xy->x,xy->y,s->speed);
            xy=s->xy+n;
            if(v&16) xy->instptr=ip=bip;
            if(v&1) stop=1;
            if(v&4) xy->instptr=ip=65535;
            if(v&8) return;
            if(v&2) {
              u=-1;
              ip=xy->instptr;
              goto begin;
            }
            if(!(v&16)) goto skip;
            u=0;
            goto begin;
          }
        }
        switch(*buf) {
          case 'A':
            if(!strcmp(buf,"AUTOCHOICE") && textfile) {
              while(s->text[ip]==' ') ip++;
              u=(s->text[ip]&~31)?parse_number(s,xy,&ip):1;
              auto_text_choice(u);
              if(*textbuf && (u=find_label(s,textbuf))>=0) {
                ip=u; u=0; goto begin;
              }
            } else goto badcommand; break;
          case 'B':
            if(!strcmp(buf,"BECOME")) {
              become:
              if(!parse_kind(s,xy,&ip,&sk,1)) {script_error(m,xy,"Improper #BECOME"); return;}
              change_to_script_kind(xy->x,xy->y,xy->layer&3,&sk);
              ip=65535; xy=s->xy+n; goto stop;
            } else if(!strcmp(buf,"BIND")) {
              for(i=0;i<maxstat;i++) if(stats[i].length && match_name(stats[i].text,s->text+ip)) {
                if(xy->x<board_info.width && xy->y<board_info.height && (j=xy->layer&3)) {
                  xy2=add_statxy(i);
                  xy=s->xy+n;
                  *xy2=*xy;
                  xy2->instptr=xy2->frame=xy2->extra=0;
                  (j==1?b_under:j==2?b_main:b_over)[xy->y*board_info.width+xy->x].stat=i;
                  kill_stat(m,n);
                }
                ip=65535; goto stop;
              }
            } else goto badcommand; break;
          case 'C':
            if(!strcmp(buf,"CHANGE")) {
              if(!parse_kind(s,xy,&ip,&sk,0) || !parse_kind(s,xy,&ip,&sk1,1)) {script_error(m,xy,"Improper #CHANGE"); return;}
              script_do_change(&sk,&sk1);
            } else if(!strcmp(buf,"CLEAR")) {
              script_set_flag(s,xy,&ip,0);
            } else if(!strcmp(buf,"CLEARALL")) {
              memset(namedflag,0,sizeof(namedflag));
            } else if(!strcmp(buf,"CLONE")) {
              Uint32 x,y,z,zz;
              i=parse_direction(s,xy,&ip);
              x=xy->x+(i==DIR_E)-(i==DIR_W);
              y=xy->y+(i==DIR_S)-(i==DIR_N);
              z=y*board_info.width+x;
              zz=xy->y*board_info.width+xy->x;
              if((s->mode&STAT_INDEPENDENT) && condflag) {
                goto clone;
              } else if(i!=-1 && condflag && xy->x<board_info.width && xy->y<board_info.height && x<board_info.width && y<board_info.height) {
                j=elem_def[b_main[z].kind].attrib;
                if((xy->layer&3)==2 && (j&A_FLOOR) && ((1<<(j&15))&(elem_def[b_main[zz].kind].attrib>>16))) {
                  if(b_main[z].stat) if(xy2=find_statxy(b_main+z)) xy2->layer--;
                  b_under[z]=b_main[z];
                  b_main[z]=b_main[zz];
                } else if((xy->layer&3)==3 && !b_over[z].stat && !(b_over[z].kind&OVER_SOLID)) {
                  b_over[z]=b_over[zz];
                } else if((xy->layer&3)==1 && !b_under[z].stat && !b_under[z].stat) {
                  b_under[z]=b_under[zz];
                } else {
                  goto skip;
                }
                clone:
                xy2=add_statxy(m);
                xy=s->xy+n;
                xy2->x=x; xy2->y=y;
                xy2->instptr=0;
                xy2->layer=xy->layer&3;
                xy2->delay=stats[m-1].speed;
                while(s->text[ip]==' ') ip++;
                for(v=0;v<126 && ip<s->length && s->text[ip]>0x20;v++) buf[v]=s->text[ip++];
                buf[v]=0;
                if(*buf) send_message(m+((stats[m-1].count-1)<<16),buf,1);
              }
            } else if(!strcmp(buf,"COLOR")) {
              if(xy->x<board_info.width && xy->y<board_info.height && (j=xy->layer&3)) {
                (j==1?b_under:j==2?b_main:b_over)[xy->y*board_info.width+xy->x].color=parse_number(s,xy,&ip);
              }
            } else if(!strcmp(buf,"CYCLE")) {
              s->speed=parse_number(s,xy,&ip);
            } else goto badcommand; break;
          case 'D':
            if(!strcmp(buf,"DIE")) {
              break_tile(0,0,m,n,0);
              ip=65535; goto stop;
            } else if(!strcmp(buf,"DIEITEM")) {
              dieitem(m,n);
              ip=65535; goto stop;
            } else goto badcommand; break;
          case 'E':
            if(!strcmp(buf,"EDITFONT")) {
              if(font) script_do_editfont(s,xy,&ip);
            } else if(!strcmp(buf,"END")) {
              ip=65535; stop=1;
            } else if(!strcmp(buf,"ERASE")) {
              if(!parse_kind(s,xy,&ip,&sk,0)) {script_error(m,xy,"Improper #ERASE"); return;}
              script_do_erase(&sk);
              xy=s->xy+n;
            } else if(!strcmp(buf,"ESCAPE")) {
              if(!textfile) {
                textfile_text=0;
                textfile_size=0;
                textfile=open_memstream(&textfile_text,&textfile_size);
                if(!textfile) err(1,"Allocation failed");
              }
              c=s->text[ip++];
              if(c!='o' && c!='O') {script_error(m,xy,"Improper #ESCAPE"); return;}
              c=s->text[ip++];
              if(c=='n' || c=='N') esc=1; else if(c=='f' || c=='F') esc=0; else {script_error(m,xy,"Improper #ESCAPE"); return;}
            } else if(!strcmp(buf,"EXTRA")) {
              xy->extra=parse_number(s,xy,&ip);
            } else goto badcommand; break;
          case 'F':
            if(!strcmp(buf,"FACE")) {
              i=parse_direction(s,xy,&ip);
              if(i!=-1 && condflag) {
                xy->layer&=0xF3;
                xy->layer|=i<<2;
              }
            } else goto badcommand; break;
          case 'G':
            if(!strcmp(buf,"GIVE")) {
              i=parse_letter(s,xy,&ip);
              if(condflag) {
                u=parse_number(s,xy,&ip);
                if(0<=(Sint32)(status_vars[i]+u)) status_vars[i]+=u;
              }
            } else if(!strcmp(buf,"GIVEITEM")) {
              if(u=parse_item(s,xy,&ip)) {
                y=parse_number(s,xy,&ip);
                if(!condflag) goto badcommand;
                if(give_item(u,y,0),!condflag) goto same;
              }
            } else if(!strcmp(buf,"GO")) {
              i=parse_direction(s,xy,&ip);
              if(i!=-1 && condflag) {
                general_move(0,(n<<16)+m,xy->x,xy->y,0x0814,1,i,i);
                if(!condflag) {
                  ip=bip;
                  goto stop;
                }
              }
              stop=1;
            } else goto badcommand; break;
          case 'H':
            if(!strcmp(buf,"HELP")) {
              while(s->text[ip]==' ') ++ip;
              if(load_help_file(s->text+ip) && show_text_window((n<<16)+m,1)) goto selection;
            } else goto badcommand; break;
          case 'I':
            if(!strcmp(buf,"IDLE")) {
              stop=1;
            } else if(!strcmp(buf,"IF")) {
              if(parse_condition(s,xy,&ip)) {
                same: u=-1; goto begin;
              }
            } else if(!strcmp(buf,"IFITEM")) {
              if(u=parse_item(s,xy,&ip)) {
                y=parse_number(s,xy,&ip);
                if(!condflag) goto badcommand;
                if(take_item(u,y,6),condflag) goto same;
              }
            } else if(!strcmp(buf,"IFNOTITEM")) {
              if(u=parse_item(s,xy,&ip)) {
                y=parse_number(s,xy,&ip);
                if(!condflag) goto badcommand;
                if(take_item(u,y,6),!condflag) goto same;
              }
            } else goto badcommand; break;
          case 'L':
            if(!strcmp(buf,"LOADFONT")) {
              while(s->text[ip]==' ') ++ip;
              load_font(s->text+ip,LOADFONT_BASE);
            } else if(!strcmp(buf,"LOADPAL")) {
              while(s->text[ip]==' ') ++ip;
              load_palette(s->text+ip,LOADFONT_BASE);
            } else if(!strcmp(buf,"LOCK")) {
              xy->layer|=0x80;
            } else goto badcommand; break;
          case 'M':
            if(!buf[5] && !memcmp(buf,"MISC",4) && buf[4]>='1' && buf[4]<='3') {
              w=parse_number(s,xy,&ip);
              if(buf[4]=='1') s->misc1=w;
              if(buf[4]=='2') s->misc2=w;
              if(buf[4]=='3') s->misc3=w;
            } else if(!strcmp(buf,"MIXCHOICES") && textfile) {
              mix_text_choices();
            } else if(!strcmp(buf,"MUSIC")) {
              script_set_music(s,xy,&ip);
            } else goto badcommand; break;
          case 'N':
            if(!strcmp(buf,"NUMSTORE")) {
              while(s->text[ip]==' ') ++ip;
              if(s->text[ip]==':') ++ip;
              for(v=0;v<64;v++,ip++) {
                c=s->text[ip];
                if(c>32 && c<127 && c!=':' && c!='=') buf[v]=c;
                else break;
              }
              buf[v]=0;
              if(s->text[ip]==':') ++ip;
              while(s->text[ip]==' ') ++ip;
              c=s->text[ip++];
              if(c!='=' && c!='-' && c!='+' && c!='|') goto badcommand;
              if(s->text[ip]=='=') ++ip;
              y=parse_number(s,xy,&ip);
              while(s->text[ip]==' ') ip++;
              if(do_numstore(s,buf,s->text[ip]&~31?-c:c,y)) {
                if(c=='|' && (!(s->text[ip]&~31) || y<=0)) {
                  ip=bip;
                  goto stop;
                }
                goto same;
              }
            } else goto badcommand; break;
          case 'P':
            if(!strcmp(buf,"PARAMETER")) {
              if(xy->x<board_info.width && xy->y<board_info.height && (j=xy->layer&3)) {
                (j==1?b_under:j==2?b_main:b_over)[xy->y*board_info.width+xy->x].param=parse_number(s,xy,&ip);
              }
            } else if(!strcmp(buf,"PLAY")) {
              if(soundon) audio_set_sfx(s->text+ip);
            } else if(!strcmp(buf,"PUT")) {
              i=parse_direction(s,xy,&ip);
              if(condflag) {
                if(i==-1) goto become;
                if(parse_kind(s,xy,&ip,&sk,1)) put_script_kind(xy->x+(i==DIR_E)-(i==DIR_W),xy->y+(i==DIR_S)-(i==DIR_N),&sk);
                xy=s->xy+n;
              }
            } else if(!strcmp(buf,"PUTAT")) {
              i=parse_number(s,xy,&ip);
              j=parse_number(s,xy,&ip);
              if(parse_kind(s,xy,&ip,&sk,1)) put_script_kind(i,j,&sk);
              xy=s->xy+n;
            } else if(!strcmp(buf,"PUTBELOW")) {
              if(xy->layer&2) {
                if(!parse_kind(s,xy,&ip,&sk,1)) {script_error(m,xy,"Improper #PUTBELOW"); return;}
                change_to_script_kind(xy->x,xy->y,1,&sk);
                xy=s->xy+n;
              }
            } else goto badcommand; break;
          case 'R':
            if(!strcmp(buf,"RESTART")) {
              ip=0; u=0; goto begin;
            } else if(!strcmp(buf,"RESTORE")) {
              while(s->text[ip]==' ') ip++;
              while((u=find_zapped_label(s,s->text+ip))!=-1) s->text[u]=(s->text[ip]=='!'?'!':':');
            } else if(!strcmp(buf,"RETURN")) {
              for(v=0;v<126 && ip<s->length && s->text[ip]>=0x20;v++) buf[v]=s->text[ip++];
              buf[v]=0;
              xy->instptr=65535;
              if(frame_return(s,xy,v)) {
                xy=s->xy+n;
                if(v) send_message((n<<16)+m,buf,1);
                ip=xy->instptr;
                if(ip!=65535) goto begin;
              }
              goto stop1;
            } else goto badcommand; break;
          case 'S':
            if(!strcmp(buf,"SEND")) {
              goto send;
            } else if(!strcmp(buf,"SENDALL")) {
              for(v=0;v<126 && ip<s->length && s->text[ip]>=0x20;v++) {
                buf[v]=s->text[ip++];
              }
              buf[v]=0;
              if(s->text[xy->instptr=ip]) ip++;
              send_message(0,buf,0);
              ip=xy->instptr;
            } else if(!strcmp(buf,"SENDDIR")) {
              i=parse_direction(s,xy,&ip);
              if(condflag) {
                while(s->text[ip]==' ') ip++;
                for(v=0;v<126 && ip<s->length && s->text[ip]>0x20;v++) buf[v]=s->text[ip++];
                buf[v]=0;
                if(*buf) send_message_at(xy->layer&3,xy->x+(i==DIR_E)-(i==DIR_W),xy->y+(i==DIR_S)-(i==DIR_N),buf,0);
              }
            } else if(!strcmp(buf,"SET")) {
              script_set_flag(s,xy,&ip,1);
            } else if(!strcmp(buf,"SHOW")) {
              while(ip<s->length && s->text[ip]!='\n') ip++;
              if(ip<s->length && s->text[ip]=='\n') ip++;
              xy->instptr=ip;
              if(textfile && show_text_window((n<<16)+m,0) && (u=find_label(s,textbuf))>=0) ip=u; else ip=xy->instptr;
              u=0; goto begin;
            } else goto badcommand; break;
          case 'T':
            if(!strcmp(buf,"TAKE")) {
              i=parse_letter(s,xy,&ip);
              if(condflag) {
                u=parse_number(s,xy,&ip);
                if(status_vars[i]>=u) {
                  status_vars[i]-=u;
                } else {
                  u=-1; goto begin;
                }
              }
            } else if(!strcmp(buf,"TAKEITEM")) {
              if(u=parse_item(s,xy,&ip)) {
                while(s->text[ip]==' ') ip++;
                if(s->text[ip]&~31) {
                  y=parse_number(s,xy,&ip);
                  if(!condflag) goto badcommand;
                  while(s->text[ip]==' ') ip++;
                  if(s->text[ip]&~31) {
                    if(take_item(u,y,0),!condflag) goto same;
                  } else {
                    take_item(u,y,1);
                  }
                } else {
                  take_item(u,0xFFFFFFFFULL,5);
                }
              }
            } else if(!strcmp(buf,"TRY")) {
              i=parse_direction(s,xy,&ip);
              if(i!=-1 && condflag) {
                general_move(0,(n<<16)+m,xy->x,xy->y,0x0814,1,i,i);
                if(!condflag) {
                  u=-1; goto begin;
                }
                stop=1;
              }
            } else goto badcommand; break;
          case 'U':
            if(!strcmp(buf,"UNLOCK")) {
              xy->layer&=0x7F;
            } else goto badcommand; break;
          case 'W':
            if(!strcmp(buf,"WALK")) {
              i=parse_direction(s,xy,&ip);
              if(condflag) {
                if(i==-1) {
                  xy->layer&=0xEF;
                } else {
                  xy->layer&=0xF3;
                  xy->layer|=(i<<2)|0x10;
                }
              }
            } else goto badcommand; break;
          case 'Z':
            if(!strcmp(buf,"ZAP")) {
              while(s->text[ip]==' ') ip++;
              if((u=find_unzapped_label(s,s->text+ip))!=-1) s->text[u]=(s->text[ip]=='!'?'&':'\'');
            } else if(!strcmp(buf,"ZAPALL")) {
              while(s->text[ip]==' ') ip++;
              while((u=find_unzapped_label(s,s->text+ip))!=-1) s->text[u]=(s->text[ip]=='!'?'&':'\'');
            } else goto badcommand; break;
          default: badcommand:
            script_error(m,xy,"Bad command");
            xy->instptr=65535;
            return;
        }
        skip:
        while(ip<s->length && s->text[ip]!='\n') ip++;
        if(ip<s->length && s->text[ip]=='\n') ip++;
        if(stop) goto stop;
      }
      break;
    case '/': case '?':
      if(!s->text[ip+1]) goto stop;
      if((v=script_go(m,n,s,xy,s->text[ip+1])) || c=='?') ip+=2;
      if(v==2) ip=65535;
      goto stop;
    case '\'': case ':': case '@': case '&':
      while(s->text[ip] && s->text[ip]!='\n') ip++;
      if(s->text[ip]) ip++;
      break;
    default:
      if(u<0 && c!='!' && c!='$' && c!='"') goto command;
      if(u<0 && c=='"') ++ip;
      if(!textfile) {
        textfile_text=0;
        textfile_size=0;
        textfile=open_memstream(&textfile_text,&textfile_size);
        if(!textfile) err(1,"Allocation failed");
        esc=0;
      }
      if(esc) {
        Uint8 lc=32,ll=0;
        for(v=0;v<80;) {
          c=buf[v]=s->text[ip];
          if(c=='\n' || !c) break;
          ip++;
          if(c==0x7B) {
            if(s->text[ip]=='T' || s->text[ip]=='t') {
              ip++;
              w=parse_number(s,xy,&ip);
              if(s->text[ip]==',') {
                ip++;
                c=parse_number(s,xy,&ip)?:32;
              } else {
                c=' ';
              }
              while(v<w && v<80) buf[v++]=c;
            } else if(s->text[ip]=='?') {
              ip++;
              if(!parse_condition(s,xy,&ip)) {
                while(s->text[ip] && s->text[ip]!='\n') {
                  if(s->text[ip]==0x7B && (s->text[ip+1]==0x7D || s->text[ip+1]=='?')) break;
                  ip++;
                }
                continue;
              }
            } else if(s->text[ip]=='l' || s->text[ip]=='L') {
              ip++;
              lc=parse_number(s,xy,&ip)?:lc;
              ll=v;
            } else if(s->text[ip]=='r' || s->text[ip]=='R') {
              ip++;
              w=parse_number(s,xy,&ip);
              if(w>80) break;
              if(w>v && w>ll) {
                memmove(buf+w+ll-v,buf+ll,v-ll);
                memset(buf+ll,lc,w-v);
                v=w;
              }
            } else {
              v+=append_escaped(buf+v,80-v,s,xy,ip);
            }
            while(s->text[ip] && s->text[ip]!=0x7D && s->text[ip]!='\n') ip++;
            if(s->text[ip]==0x7D) ip++;
          } else {
            v++;
          }
        }
      } else {
        for(v=0;v<80;v++,ip++) {
          c=buf[v]=s->text[ip];
          if(c=='\n' || !c) break;
        }
      }
      buf[v]=0;
      while(s->text[ip] && s->text[ip]!='\n') ip++;
      fputc(v,textfile);
      fwrite(buf,1,80,textfile);
  }
  u=0;
  goto begin;
  stop:
  xy->instptr=ip;
  stop1:
  if(textfile && show_text_window((n<<16)+m,0) && (u=find_label(s,textbuf))>=0) {
    selection: ip=u; u=0; stop=0; goto begin;
  }
}

static Sint32 request_info(Sint32 m) {
  if(m>=0) {
    if(m>=nspecopt || specopt[m].key==SPECI_VACANT) return 0;
    if(specopt[m].flag&SPECF_VARIABLE) specopt[m].value=spec_auto_value(specopt[m].key);
    if(specopt[m].flag&SPECF_LOCKABLE) specopt[m].flag=(specopt[m].flag&~SPECF_VARIABLE)|SPECF_LOCKED|SPECF_SAVE;
    return specopt[m].value;
  } else switch(-m) {
    case 1: return cur_screen_id;
    case 2: return cur_screen.soft_edge[DIR_E]+1-cur_screen.soft_edge[DIR_W];
    case 3: return cur_screen.hard_edge[DIR_E]+1-cur_screen.hard_edge[DIR_W];
    case 4: return cur_screen.soft_edge[DIR_N]+1-cur_screen.soft_edge[DIR_S];
    case 5: return cur_screen.hard_edge[DIR_N]+1-cur_screen.hard_edge[DIR_S];
    case 6: return maxboard;
    case 7: return maxstat;
    case 8: return ntextbuf;
    default: return 0;
  }
}

static void do_dynamic_strings(Uint8 f,Sint32 s) {
  int n;
  if(s<0) s=~s; else if(s>0) s--; else if(f!=5) goto non;
  if(s>=ndynastr) {
    if(0x0D&(1<<f)) {
      dynastr=realloc(dynastr,(s+1)*sizeof(DynaString));
      if(!dynastr) err(1,"Allocation failed");
      while(ndynastr<s+1) {
        n=ndynastr++;
        dynastr[n].len=dynastr[n].text[0]=0;
      }
    } else {
      goto non;
    }
  }
  n=dynastr[s].len;
  switch(f) {
    case 0:
      if(n>=DYNASTRLEN-1 || !ntextbuf) return;
      if(ntextbuf>=DYNASTRLEN-1-dynastr[s].len) ntextbuf=DYNASTRLEN-2-dynastr[s].len;
      memcpy(dynastr[s].text+n,textbuf,ntextbuf);
      ntextbuf=*textbuf=0;
      break;
    case 1: condflag=(memory[MEM_ARG_J]=n)?1:0; break;
    case 2:
      if(n<DYNASTRLEN-1 && (n=memory[MEM_ARG_K]&0xFF)) dynastr[s].text[dynastr[s].len++]=n;
      memory[MEM_ARG_J]=dynastr[s].len;
      break;
    case 3:
      n=memory[MEM_ARG_J];
      if(memory[MEM_ARG_K]&255) while(dynastr[s].len<n && dynastr[s].len<DYNASTRLEN-1) dynastr[s].text[dynastr[s].len++]=memory[MEM_ARG_K];
      break;
    case 4: dynastr[s].len=dynastr[s].text[0]=0; break;
    case 5:
      if(s<ndynastr) return;
      dynastr=realloc(dynastr,s)?:dynastr;
      if(!s) dynastr=0;
      ndynastr=s;
      return;
    case 6: n=memory[MEM_ARG_J]; memory[MEM_ARG_K]=(n<dynastr[s].len?dynastr[s].text[n]:0); break;
    case 7: if(memory[MEM_ARG_J]<n) if(!(dynastr[s].text[memory[MEM_ARG_J]]=memory[MEM_ARG_K]&255)) dynastr[s].len=memory[MEM_ARG_J]; break;
  }
  if(s<ndynastr) dynastr[s].text[dynastr[s].len]=0;
  return;
  non:
  if(f==1) condflag=memory[MEM_ARG_J]=0;
  if(f==6) memory[MEM_ARG_K]=0;
}

static void do_camera(Uint8 f,Sint32 s,Sint32 x,Sint32 y) {
  Sint32 b,z;
  if(s && (f==0 || f==1 || f==5 || f==6)) {
    s=convxy(s,x,y);
    if(s==-1) return;
    x=s%board_info.width;
    y=s/board_info.height;
  }
  switch(f) {
    case 0: // if coordinates are visible
      condflag=0; x-=scroll_x; y-=scroll_y;
      if(x>=0 && x<80 && y>=0 && y<25 && (cur_screen.command[y*80+x]&0xF0)==SC_BOARD) condflag=1;
      break;
    case 1: // if coordinates are within bounding box
      condflag=0; x-=scroll_x; y-=scroll_y;
      if(x>=cur_screen.hard_edge[DIR_W] && x<=cur_screen.hard_edge[DIR_E] && y>=cur_screen.hard_edge[DIR_N] && y<=cur_screen.hard_edge[DIR_S]) condflag=1;
      break;
    case 2: // center on stat XY
      if((s&0xFF) && (s&0xFF)<=maxstat && ((s>>16)&0xFFFF)<=stats[(s&0xFF)-1].count) {
        scroll_x=stats[(s&0xFF)-1].xy[(s>>16)&0xFFFF].x-cur_screen.view_x;
        scroll_y=stats[(s&0xFF)-1].xy[(s>>16)&0xFFFF].y-cur_screen.view_y;
      }
      break;
    case 3: // move camera one step in direction
      s&=3;
      if(s==DIR_E) ++scroll_x;
      if(s==DIR_W) --scroll_x;
      if(s==DIR_S) ++scroll_y;
      if(s==DIR_N) --scroll_y;
      break;
    case 4: // enable scrolling
      if(s) memory[MEM_CONTROL]&=~CONTROL_NOSCROLL; else memory[MEM_CONTROL]|=CONTROL_NOSCROLL;
      break;
    case 5: // scroll toward coordinates
      condflag=0;
      if(b=memory[MEM_SCROLL_X_RATE]) {
        z=scroll_x;
        if(z<x-(Sint32)cur_screen.soft_edge[DIR_E]) z=x-cur_screen.soft_edge[DIR_E]; else if(z>x-(Sint32)cur_screen.soft_edge[DIR_W]) z=x-cur_screen.soft_edge[DIR_W];
        if(z<scroll_x-(Sint32)b) z=scroll_x-b; else if(z>scroll_x+(Sint32)b) z=scroll_x+b;
        if(z<-(Sint32)cur_screen.hard_edge[DIR_W]) z=-cur_screen.hard_edge[DIR_W]; else if(z>board_info.width-((Sint32)cur_screen.hard_edge[DIR_E])-1) z=board_info.width-cur_screen.hard_edge[DIR_E]-1;
        if(z!=scroll_x) condflag=1;
        scroll_x=z;
      }
      if(b=memory[MEM_SCROLL_Y_RATE]) {
        z=scroll_y;
        if(z<y-(Sint32)cur_screen.soft_edge[DIR_S]) z=y-cur_screen.soft_edge[DIR_S]; else if(z>y-(Sint32)cur_screen.soft_edge[DIR_N]) z=y-cur_screen.soft_edge[DIR_N];
        if(z<scroll_y-(Sint32)b) z=scroll_y-b; else if(z>scroll_y+(Sint32)b) z=scroll_y+b;
        if(z<-(Sint32)cur_screen.hard_edge[DIR_N]) z=-cur_screen.hard_edge[DIR_N]; else if(z>board_info.height-((Sint32)cur_screen.hard_edge[DIR_S])-1) z=board_info.height-cur_screen.hard_edge[DIR_S]-1;
        if(z!=scroll_y) condflag=1;
        scroll_y=z;
      }
      break;
    case 6: // go to coordinates immediately
      scroll_x=x-cur_screen.view_x;
      scroll_y=y-cur_screen.view_y;
      break;
  }
}

static void do_spin(Sint32 x,Sint32 y,Uint8 fo,Uint32 so) {
  Tile ti[8];
  Uint32 at[8];
  StatXY*si[8]={};
  StatXY*rs;
  Uint8 bwall=0;
  Uint8 bmove=0;
  Uint8 bsolid=0;
  Uint8 bfloor=0;
  Sint32 x0,y0;
  Uint32 a,b;
  int i;
  for(i=0;i<8;i++) {
    x0=x+(i&3?(i&4?-1:1):0);
    y0=y+("<<=>>>=<"[i]-'=')*(so&0x10000?1:-1);
    if(x0>=0 && x0<board_info.width && y0>=0 && y0<board_info.height) {
      ti[i]=b_main[at[i]=y0*board_info.width+x0];
      if(ti[i].stat && !(si[i]=find_statxy(b_main+at[i]))) bwall|=1<<i;
      a=elem_def[ti[i].kind].attrib;
      if((a&A_FLOOR) && (!ti[i].kind || !((1<<19)&so))) bfloor|=1<<i; else bsolid|=1<<i;
      switch(fo) {
        case 0: bmove|=1<<i; break;
        case 1: if((a^A_FLOOR)&(A_PUSH_EW|A_PUSH_NS|A_FLOOR)) bmove|=1<<i; break;
        case 2: if(a&(A_PUSH_EW|A_PUSH_NS)) bmove|=1<<i; break;
        case 3: if(a&(0x99&(1<<i)?A_PUSH_EW:A_PUSH_NS)) bmove|=1<<i; break;
      }
      if(bmove&(1<<i)) {
        a=elem_def[b_under[at[i]].kind].attrib;
        if((a&A_FLOOR) && (!b_under[at[i]].kind || !(so&(1<<19)))) bfloor|=1<<i;
      }
    } else {
      bwall|=1<<i; bsolid|=1<<i;
    }
  }
  bmove&=(bfloor*0x101)>>1;
  for(i=0;i<8;i++) if(!(bwall&(1<<i)) && b_under[at[i]].stat) bwall|=1<<i;
  for(i=0;i<8;i++) if(bmove&(1<<i)) {
    a=(elem_def[ti[i].kind].attrib>>16)&0xFF;
    if(so&(1<<17)) a&=so; else if(so&(1<<18)) a|=so; else a=so;
    b=elem_def[ti[(i+1)&7].kind].attrib;
    if(!(b&A_FLOOR) && !(bwall&(1<<((i+1)&7)))) b=elem_def[b_under[at[(i+1)&7]].kind].attrib;
    b&=15;
    if(!(a&(1<<b))) bmove^=1<<i;
  }
  for(i=0;i<9;i++) {
    bmove&=~(bwall|((bwall*0x101)>>1));
    bwall|=(((bwall|(bsolid&~bmove))*0x101)>>1)&bsolid&~bmove;
  }
  for(i=0;i<8;i++) {
    if(bmove&(1<<i)&~((bmove*0x101)>>7)) {
      b_main[at[i]]=b_under[at[i]];
      if(b_under[at[i]].stat) if(rs=find_statxy(b_under+at[i])) rs->layer++;
      b_under[at[i]]=(Tile){};
    } else if(bfloor&(1<<i)&((bmove*0x101)>>7)&~bmove) {
      b_under[at[i]]=b_main[at[i]];
      if(b_main[at[i]].stat) if(rs=find_statxy(b_main+at[i])) rs->layer--;
    }
  }
  for(i=0;i<8;i++) if(bmove&(1<<i)) {
    b_main[a=at[(i+1)&7]]=ti[i];
    if(rs=si[i]) {
      if(rs->sensor.stat) move_sensor_stat(rs->sensor.stat,rs->x,rs->y,a%board_info.width,a/board_info.width);
      rs->x=a%board_info.width;
      rs->y=a/board_info.width;
    }
  }
}

static void zone_rotation(Uint8 zn,Uint8 forw) {
  Tile*b;
  Tile t,u;
  StatXY*ts;
  StatXY*us;
  int n,q;
  Uint32 a,h,v;
  Uint8 c;
  OrdZone*o=ozone[zn];
  if(!o || !o->ncells || !(o->flag&(ZF_AFFECT_UNDER|ZF_AFFECT_MAIN|ZF_AFFECT_OVER))) return;
  if(o->flag&ZF_REVERSE) forw^=1;
  if(o->flag&0xC000) {
    for(q=0,n=forw?o->ncells-1:0;forw?(n>=0):(n<o->ncells);n+=forw?-1:1) {
      h=a; a=o->xy[n].y*board_info.width+o->xy[n].x;
      v=elem_def[b_main[a].kind].attrib;
      if(v&A_FLOOR) {
        q=1; c=v&15;
      } else if(q) {
        q=0;
        if(o->flag&0x8000) {
          if(c>8 || (c!=8 && !((A_MOVE_C0<<c)&v))) continue;
          if(o->flag&0x4000) {
            if(!(v&(((h%board_info.width==a%board_info.width?A_PUSH_NS:0)|(h/board_info.width==a/board_info.width?A_PUSH_EW:0))))) continue;
          }
        }
        if(o->flag&ZF_AFFECT_MAIN) {
          if(b_main[h].stat && (ts=find_statxy(b_main+h))) ts->layer--;
          b_under[h]=b_main[h];
          if(b_main[a].stat && (ts=find_statxy(b_main+a))) ts->x=h%board_info.width,ts->y=h/board_info.width;
          b_main[h]=b_main[a];
          if(b_under[a].stat && (ts=find_statxy(b_under+a))) ts->layer++;
          b_main[a]=b_under[a];
          b_under[a]=(Tile){};
        }
        if(o->flag&ZF_AFFECT_OVER) {
          if(b_over[a].stat && (ts=find_statxy(b_over+a))) ts->x=h%board_info.width,ts->y=h/board_info.width;
          if(b_over[h].stat) break_tile(h,3,0,0,1);
          b_over[h]=b_over[a];
          b_over[a].kind=(b_over[a].kind&OVER_BG_THRU)|(memory[MEM_DEFAULT_OVERLAY]?OVER_VISIBLE:0);
          b_over[a].color=memory[MEM_DEFAULT_OVERLAY]>>8;
          b_over[a].param=memory[MEM_DEFAULT_OVERLAY];
          b_over[a].stat=0;
        }
      }
    }
  } else {
    for(b=b_under,q=0;q<3;b+=board_info.width*board_info.height,q++) if(o->flag&(ZF_AFFECT_UNDER<<q)) {
      if(forw) {
        t=b[h=o->xy[o->ncells-1].y*board_info.width+o->xy[o->ncells-1].x];
        ts=t.stat?find_statxy(b+h):0;
        for(n=0;n<o->ncells;n++) {
          u=b[a=o->xy[n].y*board_info.width+o->xy[n].x]; us=u.stat?find_statxy(b+a):0;
          if(ts) ts->x=o->xy[n].x,ts->y=o->xy[n].y;
          b[a]=t; t=u; ts=us;
        }
      } else {
        t=b[o->xy->y*board_info.width+o->xy->x];
        ts=t.stat?find_statxy(b+h):0;
        for(n=o->ncells-1;n>=0;n--) {
          u=b[a=o->xy[n].y*board_info.width+o->xy[n].x]; us=u.stat?find_statxy(b+a):0;
          if(ts) ts->x=o->xy[n].x,ts->y=o->xy[n].y;
          b[a]=t; t=u; ts=us;
        }
      }
    }
  }
}

static void do_varproperty_op(Uint8 fo,Sint32 so) {
  VarPropertyList vp={&pvarproperty,1};
  Uint16 j=memory[MEM_ARG_J];
  Uint16 k=memory[MEM_ARG_K];
  switch(fo) {
    case 0:
      if(board_info.varprop.count++==255) errx(1,"Board has too many variable properties");
      memset(pvarproperty.data+(pvarproperty.type&15),0,15&~pvarproperty.type);
      board_info.varprop.item=realloc(board_info.varprop.item,board_info.varprop.count*sizeof(VarProperty));
      if(!board_info.varprop.item) err(1,"Allocation failed");
      if(so) {
        memmove(board_info.varprop.item+1,board_info.varprop.item,(board_info.varprop.count-1)*sizeof(VarProperty));
        board_info.varprop.item[0]=pvarproperty;
        memory[MEM_ARG_J]=1;
      } else {
        board_info.varprop.item[board_info.varprop.count-1]=pvarproperty;
        memory[MEM_ARG_J]=board_info.varprop.count;
      }
      break;
    case 1: memset(pvarproperty.data,0,15); pvarproperty.type=so; break;
    case 2: if((k&=15)!=15) pvarproperty.data[k]=so; break;
    case 3:
      if(so) {
        if(!j || j>board_info.varprop.count) break;
        if(j==1 && board_info.varprop.count==1) goto deleteall;
        if(board_info.varprop.count!=j) memmove(board_info.varprop.item+j-1,board_info.varprop.item+j,(board_info.varprop.count-j)*sizeof(VarProperty));
        board_info.varprop.item=realloc(board_info.varprop.item,--board_info.varprop.count*sizeof(VarProperty));
        if(!board_info.varprop.item) err(1,"Allocation failed");
      } else {
        deleteall: board_info.varprop.count=0;
      }
      break;
    case 4:
      if(!so) {
        memset(pvarproperty.data+(pvarproperty.type&15),0,15&~pvarproperty.type);
        if(pvarproperty.type) work_varproperties(&vp);
      } else if(so==1) {
        work_varproperties(&board_info.varprop);
      } else if(so==2) {
        work_varproperties(&cur_screen.varprop);
      }
      break;
    case 5:
      for(condflag=0;j<board_info.varprop.count;j++) {
        if(board_info.varprop.item[j].type>=(so&0xFF) && board_info.varprop.item[j].type<=((so>>8)&0xFF)) {
          condflag=1;
          memory[MEM_ARG_J]=j+1;
          memory[MEM_ARG_K]=board_info.varprop.item[j].type;
          break;
        }
      }
      break;
    case 6:
      k=pvarproperty.type&15;
      if(so>0 && so<ngtext) {
        for(j=0;k<15 && gtext[so][j];) pvarproperty.data[k++]=gtext[so][j++];
      } else if(so<0) {
        for(j=0;k<15 && j<ntextbuf;) pvarproperty.data[k++]=textbuf[j++];
      }
      memory[MEM_ARG_K]=pvarproperty.type;
      break;
    case 7:
      if(j && j<=board_info.varprop.count) {
        condflag=1;
        pvarproperty=board_info.varprop.item[j-1];
        memory[MEM_ARG_K]=pvarproperty.type;
      } else {
        condflag=0;
      }
      break;
  }
}

static void do_overlay_memory(Uint8 fo,Sint32 so) {
  char buf[32];
  FILE*f;
  Uint32 n;
  int c;
  if(fo>2) errx(1,"Invalid OVM opcode");
  if(so>=0) {
    snprintf(buf,32,"%04X.OVM",so&0xFFFF);
  } else {
    for(n=0;n<ntextbuf && n<8 && textbuf[n]>39 && textbuf[n]!='.';n++) buf[n]=textbuf[n];
    buf[n]='.'; buf[n+1]='O'; buf[n+2]='V'; buf[n+3]='M'; buf[n+4]=0;
  }
  if(fo==2) {
    revert_lump(buf);
  } else {
    f=open_lump(buf,fo?"w":"r");
    if(!f) return;
    if(fo) {
      for(n=0;n<memory[MEM_OVERLAYMEM_SIZE] && n+memory[MEM_OVERLAYMEM_ADDRESS]<0x10000;n++) {
        fputc(memory[n+memory[MEM_OVERLAYMEM_ADDRESS]],f);
        fputc(memory[n+memory[MEM_OVERLAYMEM_ADDRESS]]>>8,f);
      }
    } else {
      memory[MEM_OVERLAYMEM_SIZE]=lump_size/2;
      for(n=memory[MEM_OVERLAYMEM_ADDRESS];n<0x10000;n++) {
        c=fgetc(f);
        if(c==EOF) break;
        memory[n]=c|(fgetc(f)<<8);
      }
    }
    fclose(f);
  }
}

static void do_inventory_op(Uint8 fo,Sint32 so) {
  FILE*f;
  Inventory*inv=inventory+((so&0x800?so>>8:memory[MEM_INVENTORY])&7);
  int i;
  switch(so&0xFF) {
    case 0: regs[fo]=inv->count; break;
    case 1:
      if(v_status[1]=='I') errx(1,"Cannot use INVE 1 inside of a item menu");
      regs[fo]&=0xFFFF;
      inv->item=realloc(inv->item,regs[fo]*sizeof(ItemSlot));
      if(regs[fo] && !inv->item) err(1,"Allocation failed");
      while(inv->count<regs[fo]) inv->item[inv->count++]=(ItemSlot){};
      inv->count=regs[fo];
      break;
    case 2: regs[fo]=inv->flag; break;
    case 3: inv->flag=regs[fo]; break;
    case 4: regs[fo]=inv->strength; break;
    case 5: inv->strength=regs[fo]; break;
    case 6: regs[fo]=inv->maxheap; break;
    case 7: inv->maxheap=regs[fo]; break;
    case 8:
      if(fo==0) {
        for(i=0;i<inv->count;i++) inv->item[i]=(ItemSlot){};
      } else if(fo==1) {
        for(i=0;i<inv->count;i++) if(!(inv->item[i].flag&(ISF_IN_USE|ISF_FIXED))) inv->item[i]=(ItemSlot){};
      } else if(fo==2) {
        for(i=0;i<inv->count-1;i++) if((inv->item[i+1].item|inv->item[i+1].flag) && !(inv->item[i].item|inv->item[i].flag)) {
          inv->item[i]=inv->item[i+1];
          inv->item[i+1]=(ItemSlot){};
          if(inv->cursor==i+1) --inv->cursor;
        }
      } else if(fo==5) {
        for(i=0;i<inv->count;i++) if(inv->item[i].flag&ISF_FIXED) inv->item[i]=(ItemSlot){};
      } else if(fo==7) {
        for(i=0;i<inv->count;i++) if(inv->item[i].flag&ISF_HIDDEN) inv->item[i]=(ItemSlot){};
      }
      break;
    case 9: condflag=(inv->flag&(1<<(fo&3)))?1:0; break;
    case 10: inv->flag&=~(1<<(fo&3)); break;
    case 11: inv->flag|=(1<<(fo&3)); break;
    case 12:
      if(v_status[1]=='I') errx(1,"Cannot use INVE 12 inside of a item menu");
      if(load_inventory(f=open_lump_by_number(regs[fo]&0xFFFF,"INV","r"),inv)) errx(1,"Error with loading .INV lump");
      if(f) fclose(f);
      break;
    case 13:
      if(save_inventory(f=open_lump_by_number(regs[fo]&0xFFFF,"INV","w"),inv)) errx(1,"Error with saving .INV lump");
      if(f) fclose(f);
      break;
    case 14: revert_lump_by_number(regs[fo]&0xFFFF,"INV"); break;
    case 15: inv->cursor=0; break;
    case 16: regs[fo]=inv->cursor; break;
    case 17: inv->cursor=regs[fo]; break;
    case 18: case 19:
      i=memory[MEM_INVENTORY]&7;
      if(inv->cursor>=inv->count || inventory[i].cursor>=inventory[i].count) break;
      if(so&1) inventory[i].item[inventory[i].cursor]=inv->item[inv->cursor]; else inv->item[inv->cursor]=inventory[i].item[inventory[i].cursor];
      break;
    case 20: case 21:
      i=memory[MEM_INVENTORY]&7;
      if((regs[fo]&0xFFFF)>=inv->count || inventory[i].cursor>=inventory[i].count) break;
      if(so&1) inventory[i].item[inventory[i].cursor]=inv->item[regs[fo]&0xFFFF]; else inv->item[regs[fo]&0xFFFF]=inventory[i].item[inventory[i].cursor];
      break;
    case 22: for(i=0;i<inv->count;i++) inv->item[i].flag&=regs[fo]; break;
    default: errx(1,"Unimplemented inventory op: %d",so&0xFF);
  }
}

static Sint32 give_item(Uint32 item,Uint32 qty,Uint16 how) {
  Sint32 rs[4]={regs[0],regs[1],regs[2],regs[3]};
  ItemDef*idef;
  Inventory*inv=inventory+(memory[MEM_INVENTORY]&7);
  ItemSlot*slot;
  Uint16 nsl,re;
  Uint32 tot,tw,heap;
  Uint32 spect=0;
  int i=item&0xFFFF;
  int j;
  Uint8 step;
  if(how&0x8000) for(j=0;j<inv->count;j++) inv->item[j].flag&=~ISF_MARK;
  if(!i || i>nitemdefs || itemdefs[i-1].class==255) return condflag=0;
  idef=itemdefs+i-1;
  if((inv->flag&INV_IGNORE_WEIGHT) || !idef->weight) how|=8;
  heap=inv->maxheap;
  if(heap>idef->maxheap && !(inv->flag&INV_IGNORE_MAXHEAP)) heap=idef->maxheap;
  if((how&32) || ((inv->flag&INV_SPECIAL) && !(how&64))) {
    switch(idef->special&0xF0) {
      case ISPECIAL_STATUS: case ISPECIAL_STATUS_NONZERO:
        if((itemdefs[i-1].flag&IDF_SINGLE_HEAP) || (inv->flag&INV_SINGLE_HEAP)) {
          how|=32;
          spect=status_vars[idef->special&15];
          if(spect+qty>heap || ((Uint32)spect+qty)<spect || ((Uint32)spect+qty)<qty) {
            if(how&1) qty=heap-spect; else return memory[MEM_ARG_J]=how,condflag=0;
          }
          goto statusvarok;
        } else if(status_vars[idef->special&15]>=heap) {
          how&=~32;
        } else {
          how|=32;
          spect=heap-status_vars[idef->special&15];
          if(spect>qty) spect=qty;
          if(spect==qty) {
            statusvarok:
            if(!(how&2)) status_vars[idef->special&15]+=qty;
            condflag=1;
            memory[MEM_ARG_J]=how;
            return qty;
          }
        }
        break;
      default: how&=~32;
    }
  }
  regs[0]=regs[1]=regs[2]=regs[3]=0;
  retry:
  nsl=re=tw=0; tot=spect;
  for(i=0;i<inv->count;i++) {
    slot=inv->item+i;
    j=slot->item;
    slot->flag&=~ISF_MARK;
    if(!(how&8) && !(slot->flag&(ISF_IGNORE|ISF_SPECIAL)) && j && j<=nitemdefs) tw+=itemdefs[j-1].weight*slot->quantity;
  }
  if(!(how&8) && tw+qty*idef->weight>inv->strength) {
    if(how&1) qty=(inv->strength-tw)/idef->weight; else { condflag=0; goto done; }
  }
  for(step=(how&4)?1:0;step<3 && tot<qty;step++) {
    re|=0x10<<step;
    for(i=0;i<inv->count && tot<qty;i++) {
      slot=inv->item+i;
      if(how&64?i!=inv->cursor:(slot->flag&(ISF_IGNORE|ISF_MARK))) continue;
      if(step==2) {
        if(slot->item || slot->flag) continue;
      } else {
        if(slot->item!=(item&0xFFFF)) continue;
        if(slot->quantity>=heap) goto un;
        if(!step && slot->flag!=(item>>16)) goto un;
        if((slot->flag^(item>>16))&(memory[MEM_ITEM_MASK]|ISF_IN_USE|ISF_SPECIAL)) goto un;
      }
      memory[MEM_ARG_J]=how; memory[MEM_ARG_K]=nsl; condflag=0;
      re|=run_program(memory[MEM_GIVE_ITEM_EVENT],item,qty,i,re);
      if(re&2) goto fail;
      if(!(re&1)) {
        slot->flag|=ISF_MARK;
        if(step==2) slot->quantity=slot->ext0=slot->ext1=slot->ext2=0;
        if(heap-slot->quantity<qty-tot) tot+=heap-slot->quantity; else tot=qty;
        nsl++;
      } else {
        if(step==2) goto end;
        un: if((inv->flag&INV_SINGLE_HEAP) || (idef->flag&IDF_SINGLE_HEAP)) goto fail;
      }
      re&=~0x71;
    }
  }
  end:
  if(tot<qty) {
    if(how&1) {
      qty=tot;
    } else {
      fail:
      if(re&4) goto retry;
      condflag=0; goto done;
    }
  } else {
    how&=~1;
  }
  if((how&18)==18) for(i=0;i<inv->count;i++) if(inv->item[i].flag&ISF_MARK) inv->cursor=i;
  if(!(how&2)) for(tot=qty-spect,i=0;i<inv->count && tot;i++) {
    slot=inv->item+i;
    if(slot->flag&ISF_MARK) {
      if(slot->item && slot->item!=(item&0xFFFF)) continue;
      memory[MEM_ARG_J]=how; memory[MEM_ARG_K]=nsl; condflag=1;
      re=run_program(memory[MEM_GIVE_ITEM_EVENT],item,qty-tot,i,0);
      if(re&2) { condflag=0; goto done; }
      if((re&8) && regs[0]>0) tot=(tot>regs[0]?tot-regs[0]:0);
      if(!(re&1)) {
        if(slot->item) {
          tw=tot;
          if(slot->quantity>heap) tw=0; else if(tw>heap-slot->quantity) tw=heap-slot->quantity;
          slot->quantity+=tw;
          tot-=tw;
        } else {
          slot->item=item&0xFFFF;
          slot->flag=item>>16;
          slot->quantity=(tot<heap?tot:heap);
          tot-=slot->quantity;
        }
      }
      if(how&16) inv->cursor=i;
    }
  }
  if((how&34)==32) switch(itemdefs[(item&0xFFFF)-1].special&0xF0) {
    case ISPECIAL_STATUS: case ISPECIAL_STATUS_NONZERO: status_vars[itemdefs[(item&0xFFFF)-1].special&15]+=spect; break;
  }
  condflag=1;
  done:
  if(!(how&0x8000)) for(i=0;i<inv->count;i++) if(!inv->item[i].item && inv->item[i].flag==ISF_MARK) inv->item[i].flag=0;
  memcpy(regs,rs,4*sizeof(Sint32));
  memory[MEM_ARG_J]=how;
  return condflag?qty:0;
}

static Sint32 take_item(Uint32 item,Uint32 qty,Uint16 how) {
  Sint32 rs[4]={regs[0],regs[1],regs[2],regs[3]};
  Inventory*inv=inventory+(memory[MEM_INVENTORY]&7);
  ItemSlot*slot;
  Uint16 re=0;
  Uint32 tot=0;
  Uint16 nsl=0;
  Uint32 spect=0;
  int i=item&0xFFFF;
  int j;
  if(!i || i>nitemdefs || itemdefs[i-1].class==255) return condflag=0;
  if(!qty && !((item>>16)&ISF_FIXED)) return condflag=1,0;
  if((how&32) || ((inv->flag&INV_SPECIAL) && !(how&64))) {
    switch(itemdefs[i-1].special&0xF0) {
      case ISPECIAL_STATUS: case ISPECIAL_STATUS_NONZERO:
        spect=status_vars[itemdefs[i-1].special&15];
        if(spect>qty) spect=qty;
        if(spect) how|=32; else how&=~32;
        if(((itemdefs[i-1].flag&IDF_SINGLE_HEAP) || (inv->flag&INV_SINGLE_HEAP)) && qty>spect) {
          if(how&1) qty=spect; else return memory[MEM_ARG_J]=how|32,condflag=0;
        }
        if(qty<=status_vars[itemdefs[i-1].special&15]) {
          if(!(how&2)) status_vars[itemdefs[i-1].special&15]-=qty;
          condflag=1;
          memory[MEM_ARG_J]=how;
          return qty;
        }
        break;
      default: how&=~32;
    }
  }
  regs[0]=regs[1]=regs[2]=regs[3]=0;
  retry:
  nsl=0; tot=spect;
  for(i=0;i<inv->count;i++) inv->item[i].flag&=~ISF_MARK;
  for(i=0;i<inv->count;i++) {
    if((how&64) && i!=inv->cursor) continue;
    slot=inv->item+i;
    if(slot->flag&ISF_IGNORE) continue;
    if(!(how&4) && slot->flag!=(item>>16)) continue;
    if(slot->item==(item&0xFFFF) && !((slot->flag^(item>>16))&(memory[MEM_ITEM_MASK]|ISF_IN_USE|ISF_SPECIAL))) {
      if((slot->quantity && tot<qty) || ((slot->flag&(item>>16)&ISF_FIXED) && !nsl)) nsl++,slot->flag|=ISF_MARK;
      tot+=slot->quantity;
      if(tot && tot>=qty) break;
    }
  }
  if(tot<qty) {
    if(!(how&4)) { how|=4; goto retry; }
    if(!(how&1)) goto fail;
  } else {
    re|=0x0100;
  }
  reloop:
  for(i=0;i<inv->count;i++) {
    slot=inv->item+i;
    if(slot->flag&ISF_MARK) {
      memory[MEM_ARG_J]=how; memory[MEM_ARG_K]=nsl;
      re|=run_program(memory[MEM_TAKE_ITEM_EVENT],item,tot<qty?tot:qty,i,re);
      if(i>=inv->count) errx(1,"Improper use of item events");
      slot=inv->item+i;
      if(re&0x0001) goto fail;
      if(re&0x0800) { condflag=0; goto done; }
      if(re&0x0020) qty=regs[0],re&=~0x0020;
      if(re&0x0002) {
        re&=~0x0102;
        slot->flag&=~ISF_MARK; tot-=slot->quantity; nsl--;
        for(j=i+1;j<inv->count;j++) {
          if((how&64) && j!=inv->cursor) continue;
          slot=inv->item+j;
          if(slot->flag&(ISF_IGNORE|ISF_MARK)) continue;
          if(!(how&4) && slot->flag!=(item>>16)) continue;
          if(slot->item==(item&0xFFFF) && !((slot->flag^(item>>16))&(memory[MEM_ITEM_MASK]|ISF_IN_USE|ISF_SPECIAL))) {
            if((slot->quantity && tot<qty) || ((slot->flag&(item>>16)&ISF_FIXED) && !nsl)) nsl++,slot->flag|=ISF_MARK;
            tot+=slot->quantity;
            if(tot && tot>=qty) break;
          }
        }
        if(tot<qty) {
          if(!(how&4)) { how|=4; goto retry; }
          if(!(how&1)) goto fail;
        } else {
          re|=0x0100;
        }
      }
      if(re&0x0004) break;
    }
  }
  if(re&0x0001) {
    fail:
    condflag=(re&0x0400?1:0);
    if((re&0x0008) && !condflag) {
      re=0x0080;
      goto retry;
    }
    goto done;
  } else {
    success:
    condflag=1;
    if(re&0x0200) {
      re&=0x0558; re|=0x0400;
      goto reloop;
    }
    if(!(re&0x0010)) {
      tot=spect;
      for(i=0;i<inv->count;i++) {
        slot=inv->item+i;
        if(slot->flag&ISF_MARK) {
          j=(slot->quantity<qty?slot->quantity:qty);
          if(qty>=tot && j>qty-tot) j=qty-tot;
          tot+=j;
          if(!(how&2)) {
            slot->quantity-=j;
            slot->flag&=~ISF_MARK;
            if(!slot->quantity && !(slot->flag&ISF_FIXED)) *slot=(ItemSlot){};
          }
          if(how&16) inv->cursor=i;
          if(tot>=qty) break;
        }
      }
      qty=tot;
    }
    if((how&34)==32) switch(itemdefs[(item&0xFFFF)-1].special&0xF0) {
      case ISPECIAL_STATUS: case ISPECIAL_STATUS_NONZERO: status_vars[itemdefs[(item&0xFFFF)-1].special&15]-=spect; break;
    }
  }
  done:
  memcpy(regs,rs,4*sizeof(Sint32));
  memory[MEM_ARG_J]=how;
  return condflag?qty:0;
}

static Sint32 move_item(Uint32 item,Uint32 qty,Uint16 how) {
  Inventory*inv;
  ItemSlot*s;
  Uint32 q,m;
  Uint16 k=memory[MEM_ARG_K];
  Uint16 frn=memory[k&0x1000?MEM_ARG_K:MEM_INVENTORY];
  Uint16 ton=memory[k&0x1000?MEM_INVENTORY:MEM_ARG_K];
  Uint16 frc=inventory[frn&7].cursor;
  Uint16 toc=inventory[ton&7].cursor;
  Uint16 c;
  condflag=0;
  if((k&0x4000) && frc<inventory[frn&7].count) qty=inventory[frn&7].item[frc].quantity;
  if(k&0x8000) {
    if(frc>=inventory[frn&7].count) goto stop;
    item=inventory[frn&7].item[frc].item|(ton&0x2000?item&0xFFFF0000:((Uint32)inventory[frn&7].item[frc].flag)<<16);
  }
  if(!(k&0x0800)) {
    memory[MEM_INVENTORY]=(frn&7)|8;
    qty=take_item(item,qty,0x12|how&0xFF);
    if(!condflag || !qty) goto stop;
    c=inventory[frn&7].cursor;
    if(c<inventory[frn&7].count && qty<=inventory[frn&7].item[c].quantity) {
      if(!(k&0x2000)) item=(item&0xFFFF)|(inventory[frn&7].item[c].flag<<16);
      item&=~(ISF_MARK<<16);
    }
    memory[MEM_INVENTORY]=(ton&7)|8;
    qty=give_item(item,qty,0x8012|(how>>8));
    if(!condflag || !qty) goto stop;
  }
  if(!(how&0x0002)) {
    memory[MEM_INVENTORY]=(frn&7)|24;
    inv=inventory+(frn&7);
    q=qty;
    if(how&0x40) { c=inv->cursor; if(c<inv->count) goto take1; }
    for(c=0;c<inv->count;c++) if(inv->item[c].flag&ISF_MARK) {
      take1:
      s=inv->item+c;
      if(s->quantity<=q) {
        q-=s->quantity;
        if(s->flag&ISF_FIXED) s->quantity=0; else *s=(ItemSlot){};
        if(!q) break;
      } else {
        s->quantity-=q;
        break;
      }
      if(how&0x40) break;
    }
  }
  if(!(how&0x0200)) {
    memory[MEM_INVENTORY]=(ton&7)|24;
    inv=inventory+(ton&7);
    m=inv->maxheap;
    if(!(inv->flag&INV_IGNORE_MAXHEAP) && (q=item&0xFFFF) && q<=nitemdefs && itemdefs[q-1].maxheap<m) m=itemdefs[q-1].maxheap;
    q=qty;
    if(how&0x4000) { c=inv->cursor; if(c<inv->count) goto give1; }
    for(c=0;q && c<inv->count;c++) if(inv->item[c].flag&ISF_MARK) {
      give1:
      s=inv->item+c;
      if(s->item) {
        if(s->quantity<m) {
          if(q>m-s->quantity) q-=(m-s->quantity),s->quantity=m;
          else s->quantity+=q,q=0;
        }
      } else {
        *s=(ItemSlot){.item=item&0xFFFF,.flag=(item>>16)|ISF_MARK,.quantity=q<m?q:m};
        if(q<=m) break;
        q-=m;
      }
      if(how&0x4000) break;
    }
  }
  stop:
  memory[MEM_INVENTORY]=how&0x1000?ton:frn;
  memory[MEM_ARG_J]=how;
  memory[MEM_ARG_K]=how&0x1000?frn:ton;
  if(!(how&0x10)) inventory[frn&7].cursor=frc;
  if(!(how&0x1000)) inventory[ton&7].cursor=toc;
  return condflag?qty:0;
}

static Sint32 count_items(Sint32 t,Uint32 m) {
  Inventory*inv=inventory+(memory[MEM_INVENTORY]&7);
  ItemDef*d;
  ItemSlot*slot;
  Uint32 v;
  int i,k;
  char j;
  switch((m>>28)&15) {
    case 0 ... 3: case 5: case 7: t=0; break;
    case 4: case 6: t=0x7FFFFFFFLL; break;
  }
  for(i=0;i<inv->count;i++) {
    slot=inv->item+i;
    if(!slot->item || slot->item>nitemdefs || itemdefs[slot->item-1].class==255) d=0; else d=itemdefs+slot->item-1;
    switch((m>>16)&3) {
      case 1:
        if((slot->flag&memory[MEM_ARG_J])!=memory[MEM_ARG_K]) {
          skip1: if(m&0x400000) slot->flag&=~ISF_MARK; continue;
        }
        break;
      case 2: if(slot->flag&ISF_MARK) goto skip1; break;
      case 3: if(!(slot->flag&ISF_MARK)) continue; break;
    }
    if(m&0x400000) slot->flag&=~ISF_MARK;
    if((m&0x200000) && (slot->flag&ISF_IGNORE)) continue;
    for(j=0;j<2;j++) switch(k=(m>>(j?8:0))&0xFF) {
      case 0x00 ... 0x7F: if(d && d->class!=k) goto skip; break;
      case 0x80 ... 0x9F: if(d && (d->flag&(1L<<(m&31)))) goto skip; break;
      case 0xA0 ... 0xBF: if(d && !(d->flag&(1L<<(m&31)))) goto skip; break;
      case 0xC0 ... 0xCF: if(slot->flag&(1L<<(m&15))) goto skip; break;
      case 0xD0 ... 0xDF: if(!(slot->flag&(1L<<(m&15)))) goto skip; break;
      case 0xE0 ... 0xE7: if(slot->item!=regs[m&7]) goto skip; break;
      case 0xE8: if(slot->item || slot->flag) goto skip; break;
      case 0xE9: if(!slot->item && !slot->flag) goto skip; break;
      case 0xEA: if(i==inv->cursor) goto skip; break;
      case 0xEB: if(i!=inv->cursor) goto skip; break;
      case 0xEC: if(!slot->quantity) goto skip; break;
      case 0xED: if(i<inv->cursor) goto skip; break;
      case 0xEE: if(i<=inv->cursor) goto skip; break;
      case 0xEF: if(i>=inv->cursor) goto skip; break;
      default: errx(1,"Improper use of ICNT");
    }
    switch((m>>18)&3) {
      case 0: normal:
        if(!d) continue;
        switch((m>>24)&15) {
          case 0: v=slot->ext0; break;
          case 1: v=slot->ext1; break;
          case 2: v=slot->ext2; break;
          case 3: v=itemdefs[slot->item-1].ext3; break;
          case 4: v=itemdefs[slot->item-1].ext4; break;
          case 5: v=itemdefs[slot->item-1].ext5; break;
          case 6: v=itemdefs[slot->item-1].price; break;
          case 7: v=itemdefs[slot->item-1].weight; break;
          case 8: v=slot->flag; break;
          case 9: v=itemdefs[slot->item-1].flag; break;
          case 10: v=i; break;
          case 11: if((m&0x800000) && (slot->item || slot->flag)) slot->flag^=ISF_MARK; return i;
          default: errx(1,"Improper use of ICNT");
        }
        break;
      case 1: v=1; break;
      default: if(slot->item || slot->flag) goto normal; v=(m>>18)&1; break;
    }
    switch((m>>28)&7) {
      case 0: t+=v; break;
      case 1: t-=v; break;
      case 2: t+=v*slot->quantity; break;
      case 3: t-=v*slot->quantity; break;
      case 4: if(v<t) t=v; break;
      case 5: if(v>t) t=v; break;
      case 6: t&=v; break;
      case 7: t|=v; break;
    }
    if((m&0x800000) && (slot->item || slot->flag)) slot->flag^=ISF_MARK;
    skip: ;
  }
  return t;
}

static Uint8 zone_move(Uint32 flag,Uint8 dir) {
  UnordZone*u=0;
  OrdZone*o=0;
  StatXY*q;
  Tile t,tt;
  Uint32 i=flag&0xF0;
  Uint32 c,j,k,x,y;
  Sint32 xd=(dir==DIR_E?1:dir==DIR_W?-1:0);
  Sint32 yd=(dir==DIR_S?1:dir==DIR_N?-1:0);
  if(!i && !(u=uzone[flag&15])) return 1;
  if(i==0x10 && !(o=ozone[flag&15])) return 1;
  if(flag&0xFF0000) {
    c=memory[MEM_ARG_J];
    if(o) {
      if(flag&0x800000) {
        switch(dir) {
          case DIR_E: for(i=0;i<o->ncells;i++) if(o->xy[i].x==board_info.width-1) return 0; break;
          case DIR_N: for(i=0;i<o->ncells;i++) if(!o->xy[i].y) return 0; break;
          case DIR_W: for(i=0;i<o->ncells;i++) if(!o->xy[i].x) return 0; break;
          case DIR_S: for(i=0;i<o->ncells;i++) if(o->xy[i].y==board_info.height-1) return 0; break;
        }
      }
      if(flag&0x7F0000) {
        for(i=0;i<o->ncells;i++) if(in_zone(o->xy[i].x+xd,o->xy[i].y+yd,flag^0x80)) {
          j=o->xy[i].y*board_info.width+o->xy[i].x;
          k=(o->xy[i].y+yd)*board_info.width+(o->xy[i].x+xd);
          if((flag&0x010000) && (elem_def[b_under[k].kind].attrib&(A_FLOOR|A_PERMANENT))!=A_FLOOR) return 0;
          if(flag&0x020000) {
            if(!(elem_def[b_main[k].kind].attrib&((flag&0x4000?A_CRUSH:0)|A_FLOOR))) return 0;
            if(elem_def[b_main[k].kind].attrib&A_PERMANENT) return 0;
          }
          if((flag&0x040000) && (b_over[k].kind&OVER_SOLID)) return 0;
          if((flag&0x080000) && in_zone(o->xy[i].x+xd,o->xy[i].y+yd,memory[MEM_ARG_K])) return 0;
          if(flag&0x100000) {
            if(!(flag&0x8000)) c=(c&0xFF00)|((elem_def[b_under[j].kind].attrib>>8)&0xFF);
            if(!(c&(1UL<<(elem_def[b_under[k].kind].attrib&15)))) return 0;
          }
          if(flag&0x200000) {
            if(!(flag&0x8000)) c=(c&0xFF00)|((elem_def[b_main[j].kind].attrib>>8)&0xFF);
            if(!(c&(1UL<<(elem_def[b_main[k].kind].attrib&15)))) return 0;
          }
        }
      }
    } else {
      j=0; k=yd*board_info.width+xd;
      for(y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++,j++,k++) if(in_zone(x,y,flag)) {
        if(in_zone(x+xd,y+yd,flag^0x80)) {
          if((flag&0x010000) && (elem_def[b_under[k].kind].attrib&(A_FLOOR|A_PERMANENT))!=A_FLOOR) return 0;
          if(flag&0x020000) {
            if(!(elem_def[b_main[k].kind].attrib&((flag&0x4000?A_CRUSH:0)|A_FLOOR))) return 0;
            if(elem_def[b_main[k].kind].attrib&A_PERMANENT) return 0;
          }
          if((flag&0x040000) && (b_over[k].kind&OVER_SOLID)) return 0;
          if((flag&0x080000) && in_zone(x+xd,y+yd,memory[MEM_ARG_K])) return 0;
          if(flag&0x100000) {
            if(!(flag&0x8000)) c=(c&0xFF00)|((elem_def[b_under[j].kind].attrib>>8)&0xFF);
            if(!(c&(1UL<<(elem_def[b_under[k].kind].attrib&15)))) return 0;
          }
           if(flag&0x200000) {
           if(!(flag&0x8000)) c=(c&0xFF00)|((elem_def[b_main[j].kind].attrib>>8)&0xFF);
             if(!(c&(1UL<<(elem_def[b_main[k].kind].attrib&15)))) return 0;
          }
        }
        if(flag&0x800000) {
          switch(dir) {
            case DIR_E: if(x==board_info.width-1) return 0; break;
            case DIR_N: if(!y) return 0; break;
            case DIR_W: if(!x) return 0; break;
            case DIR_S: if(y==board_info.height-1) return 0; break;
          }
        }
      }
    }
  }
  if(flag&0x700) {
    for(y=(dir==DIR_S?board_info.height-1:0);;) {
      for(x=(dir==DIR_E?board_info.width-1:0);;) {
        if(!in_zone(x,y,flag)) goto skip;
        c=(dir==DIR_E?(x<board_info.width-1):dir==DIR_N?(y>0):dir==DIR_W?(x>0):(y<board_info.height-1));
        j=y*board_info.width+x;
        k=(y+yd)*board_info.width+x+xd;
        if(flag&0x100) {
          t=b_under[j];
          if(t.stat && (q=find_statxy(b_under+j))) {
            if(c) q->x=x+xd,q->y=y+yd; else break_tile(j,1,0,0,1);
          }
          b_under[j]=(Tile){};
          if(c) b_under[k]=t;
        }
        if(flag&0x200) {
          t=b_main[j];
          if(t.stat && (q=find_statxy(b_main+j))) {
            if(c) q->x=x+xd,q->y=y+yd; else break_tile(j,2,0,0,1);
          }
          if(!(flag&0x100)) {
            if(q=find_statxy(b_under+j)) q->layer++;
            b_main[j]=b_under[j];
          } else {
            b_main[j]=(Tile){};
          }
          if(c) {
            tt=b_main[k];
            if(!(flag&0x100) && !((flag&0x2000) && !(elem_def[tt.kind].attrib&A_FLOOR)) && !((flag&0x4000) && (elem_def[tt.kind].attrib&A_CRUSH))) {
              if(q=find_statxy(b_main+k)) q->layer--;
              b_under[k]=b_main[k];
            } else {
              break_tile(k,2,0,0,1);
            }
            b_main[k]=t;
          }
        }
        if(flag&0x400) {
          t=b_over[j];
          if(t.stat && (q=find_statxy(b_over+j))) {
            if(c) q->x=x+xd,q->y=y+yd; else break_tile(j,3,0,0,1);
          }
          if(c) b_over[k]=t;
          b_over[j]=(Tile){.kind=(t.kind&OVER_BG_THRU)|(memory[MEM_DEFAULT_OVERLAY]?OVER_VISIBLE:0),.color=memory[MEM_DEFAULT_OVERLAY]>>8,.param=memory[MEM_DEFAULT_OVERLAY],.stat=0};
        }
        skip: if(dir==DIR_E?(!x--):(++x==board_info.width)) break;
      }
      if(dir==DIR_S?(!y--):(++y==board_info.height)) break;
    }
  }
  if(flag&0x800) {
    if(o) {
      for(i=0;i<o->ncells;i++) {
        x=o->xy[i].x; y=o->xy[i].y;
        if((!x && dir==DIR_W) || (!y && dir==DIR_N) || (x==board_info.width && dir==DIR_E) || (y==board_info.height && dir==DIR_S)) {
          memmove(o->xy+i,o->xy+i+1,(o->ncells-i-1)*sizeof(OrdZoneXY));
          --o->ncells;
        } else {
          o->xy[i].x+=xd; o->xy[i].y+=yd;
        }
      }
    } else if(u) {
      i=flag&0xFF; j=((u->maxy+1L)*board_info.width+7)/8;
      //TODO: should make the north and south cases more efficient
      switch(dir) {
        case DIR_E:
          for(i=x=0;i<j;i++) {
            y=u->data[i]>>7; u->data[i]=(u->data[i]<<1)+x; x=y;
          }
          for(y=i=0;y<=u->maxy;y++,i+=board_info.width) u->data[i>>3]&=~(1<<(i&7));
          break;
        case DIR_N:
          for(y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++) {
            if(in_zone(x,y+1,i)) zone_add(x,y,i,0); else zone_remove(x,y,i);
          }
          break;
        case DIR_W:
          for(i=x=0;i<j;i++) {
            y=u->data[i]<<7; u->data[i]=(u->data[i]>>1)+x; x=y;
          }
          for(y=0,i=board_info.width-1;y<=u->maxy;y++,i+=board_info.width) u->data[i>>3]&=~(1<<(i&7));
          break;
        case DIR_S:
          for(y=board_info.height-1;y;y--) for(x=0;x<board_info.width;x++) {
            if(in_zone(x,y-1,i)) zone_add(x,y,i,0); else zone_remove(x,y,i);
          }
          for(x=0;x<board_info.width;x++) zone_remove(x,0,i);
          break;
      }
    } else if((flag&0x4F8)==0x30) {
      i=1<<(flag&7);
      for(y=(dir==DIR_S?board_info.height-1:0);;) {
        for(x=(dir==DIR_E?board_info.width-1:0);;) {
          if(dir==DIR_E?(x<board_info.width-1):dir==DIR_N?(y>0):dir==DIR_W?(x>0):(y<board_info.height-1)) {
            j=y*board_info.width+x;
            k=(y+yd)*board_info.width+x+xd;
            b_over[k].kind|=b_over[j].kind&i;
          }
          b_over[j].kind&=~i;
          if(dir==DIR_E?(!x--):(++x==board_info.width)) break;
        }
        if(dir==DIR_S?(!y--):(++y==board_info.height)) break;
      }
    }
  }
  return 1;
}

static Sint32 run_program(Uint16 pc,Sint32 w,Sint32 x,Sint32 y,Sint32 z) {
  StatXY*rs;
  Uint16 op;
  Uint8 fo;
  Sint32 so,t,u;
  Uint16 ex;
  if(pc<256) return pc;
  for(;;) {
    op=memory[pc++];
    if((op&0x180)==0x180) {
      op^=0x80;
      fo=((op>>9)&7)+8;
    } else {
      fo=(op>>9)&7;
    }
    if((op>>12)==3) {
      ex=memory[pc++];
      if((ex>>12)==2) {
        so=memory[pc++]<<16;
        so|=memory[pc++];
        goto so_done;
      }
    } else {
      ex=op&0xF000;
    }
    switch(ex>>12) {
      case 0 ... 1: so=ex>>12; break;
      case 2: so=memory[pc++]; break;
      case 3: so=memory[memory[pc++]]; break;
      case 4: so=w; break;
      case 5: so=x; break;
      case 6: so=y; break;
      case 7: so=z; break;
      case 8 ... 15: so=regs[(ex>>12)&7]; break;
    }
    so_done:
    if(ex&0x0FFF) {
      switch(ex&0x0F00) {
        case XOP_ADD: so+=ex&255; break;
        case XOP_ADD_NEG: so+=(ex&255)-256; break;
        case XOP_ADD_INDIRECT: so=memory[(so+(ex&255))&0xFFFF]; break;
        case XOP_ADD_NEG_INDIRECT: so=memory[(so+(ex&255)-256)&0xFFFF]; break;
        case XOP_LEFT_SHIFT: so<<=ex&31; break;
        case XOP_SIGNED_RIGHT_SHIFT: so>>=ex&31; break;
        case XOP_UNSIGNED_RIGHT_SHIFT: so=((Uint32)so)>>(ex&31); break;
        case XOP_EXTRACT_BITS: so=(so>>(ex&15))&~((-1ULL)<<((ex>>4)&15)); break;
        case XOP_SUBTRACT: so=(ex&255)-so; break;
        case XOP_SUBTRACT_NEG: so=(ex&255)-so-256; break;
        case XOP_XDIR: case XOP_YDIR:
          t=(ex>>4)&15;
          if(t==8) u=w; else if(t==9) u=x; else if(t==10) u=y; else if(t==1) u=z; else u=regs[t&7];
          u>>=(ex&7);
          if(ex&8) u^=2;
          if(ex&0x100) u++;
          u&=3;
          so+=(u?(u==2?-1:0):1);
          break;
        case XOP_RANDOM: so=dice((so+(ex&255))?:1); break;
        case XOP_SPECIAL: so=xop_special(so,ex); break;
      }
    }
    switch(op&0x1FF) {
      case OP_ABS: if(so<0) so=-so; goto store;
      case OP_ADD: regs[fo]+=so; break;
      case OP_AND: regs[fo]&=so; break;
      case OP_ANDN: regs[fo]&=~so; break;
      case OP_ASN1:
        if(!config.enable_asn1_op) errx(1,"ASN1 is disabled");
        t=do_asn1_operator(fo,so,so&0x40?regs[fo]:pc); if(so&0x40) memory[MEM_RETURNED_PC]=t; else pc=t; break;
      case OP_ASUB: regs[fo]=labs(regs[fo]-so); break;
      case OP_BACK: so^=2; goto forw;
      case OP_BFLG: condflag=(board_info.flag>>fo)&1; if(so>0) board_info.flag|=1<<fo; else if(so<0) board_info.flag&=~(1<<fo); break;
      case OP_BGIV: condflag=(status_vars[fo]&(1<<(so&31))?0:1); status_vars[fo]|=1<<(so&31); break;
      case OP_BIT: so=1<<(so&31); goto store;
      case OP_BLOC: switch(fo) {
        case 0 ... 7: regs[fo]=so; return pc;
        case 9: condflag=so?1:0; return pc;
        case 12: pc=run_program(pc,so,x,y,z); if(pc<256) return pc; break;
        case 13: pc=run_program(pc,w,so,y,z); if(pc<256) return pc; break;
        case 14: pc=run_program(pc,w,x,so,z); if(pc<256) return pc; break;
        case 15: pc=run_program(pc,w,x,y,so); if(pc<256) return pc; break;
      } break;
      case OP_BTAK: condflag=(status_vars[fo]&(1<<(so&31))?1:0); status_vars[fo]&=~(1<<(so&31)); break;
      case OP_BTST: condflag=((1L<<(so&31))&regs[fo])?1:0; break;
      case OP_CALL: so=run_program(so,w,x,y,z); goto store;
      case OP_CALM: if((t=convxy(so,x,y))!=-1) w=run_program(elem_def[b_main[t].kind].event[fo],w,t%board_info.width,t/board_info.width,z); break;
      case OP_CALS:
        memory[MEM_CALL_STATUS]=(so&0x1FFF)+(fo<<13);
        if(so>=256) {
          t=++memory[memory[MEM_CALL_STACK]];
          memory[(t+memory[MEM_CALL_STACK])&0xFFFF]=pc;
          pc=so;
        }
        break;
      case OP_CALU: if((t=convxy(so,x,y))!=-1) w=run_program(elem_def[b_under[t].kind].event[fo],w,t%board_info.width,t/board_info.width,z); break;
      case OP_CAM: do_camera(fo,so,x,y); break;
      case OP_CASE: so=memory[(so+regs[fo])&0xFFFF]; goto jump;
      case OP_CBC: memory[so&0xFFFF]&=~(1<<fo); break;
      case OP_CBS: memory[so&0xFFFF]|=(1<<fo); break;
      case OP_CBT: condflag=(memory[so&0xFFFF]&(1<<fo)?1:0); break;
      case OP_CHA: do_change(1,regs[fo],so); break;
      case OP_CHAX: do_change(2,regs[fo],so); break;
      case OP_CHEX: board_info.exits[so&3]=regs[fo]; break;
      case OP_CLAM: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=elem_def[b_main[t].kind].attrib&15; else condflag=0; break;
      case OP_CLAU: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=elem_def[b_under[t].kind].attrib&15; else condflag=0; break;
      case OP_CORU:
        if(so>=256) memory[MEM_COROUTINE_U_PC]=so;
        so=(memory[MEM_COROUTINE_U_W_HI]<<16)|memory[MEM_COROUTINE_U_W_LO];
        memory[MEM_COROUTINE_U_W_HI]=w>>16; memory[MEM_COROUTINE_U_W_LO]=w;
        memory[MEM_COROUTINE_U_X_HI]=x>>16; memory[MEM_COROUTINE_U_X_LO]=x;
        memory[MEM_COROUTINE_U_Y_HI]=y>>16; memory[MEM_COROUTINE_U_Y_LO]=y;
        memory[MEM_COROUTINE_U_Z_HI]=z>>16; memory[MEM_COROUTINE_U_Z_LO]=z;
        goto store;
      case OP_CORV:
        if(so>=256) memory[MEM_COROUTINE_V_PC]=so;
        so=(memory[MEM_COROUTINE_V_W_HI]<<16)|memory[MEM_COROUTINE_V_W_LO];
        memory[MEM_COROUTINE_V_W_HI]=w>>16; memory[MEM_COROUTINE_V_W_LO]=w;
        memory[MEM_COROUTINE_V_X_HI]=x>>16; memory[MEM_COROUTINE_V_X_LO]=x;
        memory[MEM_COROUTINE_V_Y_HI]=y>>16; memory[MEM_COROUTINE_V_Y_LO]=y;
        memory[MEM_COROUTINE_V_Z_HI]=z>>16; memory[MEM_COROUTINE_V_Z_LO]=z;
        goto store;
      case OP_COUN: regs[fo]=do_change(0,regs[fo],so); break;
      case OP_CWOE: t=regs[fo]&0xFF; cwoe: t=(elem_def[t].attrib); if(t&A_FLOOR) condflag=((1<<(t&15))&so)?1:0; else condflag=0; break;
      case OP_CWOT: if((t=convxy(regs[fo],x,y))!=-1) { t=b_main[t].kind; goto cwoe; } else condflag=0; break;
      case OP_DCL: condflag=(--regs[fo]<so?1:0); break;
      case OP_DEAL:
        if(memory[so&0xFFFF]) {
          condflag=1;
          t=dice(memory[so&0xFFFF])+1;
          u=memory[(so+t)&0xFFFF];
          memory[(so+t)&0xFFFF]=memory[(so+memory[so&0xFFFF])&0xFFFF];
          regs[fo]=memory[(so+memory[so&0xFFFF])&0xFFFF]=u;
          --memory[so&0xFFFF];
        } else {
          condflag=0;
        }
        break;
      case OP_DEC: --so; goto store;
      case OP_DECL: --so; goto lstore;
      case OP_DIE: if(so&0xFF) break_tile(0,0,so&0xFFFF,so>>16,fo&4); died: if(fo&=3) return fo-2; break;
      case OP_DIR:
        if(regs[fo]) t=(regs[fo]-1)%board_info.width,u=(regs[fo]-1)/board_info.width; else t=x,u=y;
        so&=3;
        if(so==DIR_N) u--; else if(so==DIR_S) u++;
        if(so==DIR_W) t--; else if(so==DIR_E) t++;
        if(t>=0 && u>=0 && t<board_info.width && u<board_info.height) {
          condflag=1;
          if(regs[fo]) regs[fo]=u*board_info.width+t+1; else x=t,y=u;
        } else {
          condflag=0;
        }
        break;
      case OP_DIV: if(so) condflag=1,regs[fo]/=so; else condflag=0; break;
      case OP_DPUT: t=++memory[so&0xFFFF]; memory[(so+t)&0xFFFF]=regs[fo]; break;
      case OP_DROP:
        condflag=0;
        if(rs=get_statxy(so)) {
          if(x<0 || x>board_info.width || y<0 || y>board_info.height || (rs->layer&3)!=2) break;
          rs->x=x; rs->y=y; rs->layer=(rs->layer&~3)+2;
          u=x+y*board_info.width;
          if(b_under[u].stat) break;
          if(elem_def[b_main[u].kind].attrib&A_SENSOR&~elem_def[regs[fo]&0xFF].attrib) {
            if(run_program(elem_def[b_main[u].kind].event[EV_SENSOR],so,rs->x,rs->y,(b_main[u].kind<<8)|0xFFFF0082)) {
              rs->sensor=b_main[u];
              if(rs->sensor.stat) step_on_sensor_stat(u);
              goto skipdrop;
            }
          } else if(elem_def[b_main[u].kind].attrib&A_PERMANENT) {
            break;
          }
          b_under[u]=b_main[u];
          if(b_main[u].stat) if(rs=find_statxy(b_main+u)) rs->layer--;
          skipdrop:
          b_main[u].stat=so;
          b_main[u].kind=regs[fo];
          b_main[u].color=regs[fo]>>8;
          b_main[u].param=regs[fo]>>16;
          condflag=1;
        }
        break;
      case OP_DTAK:
        if(memory[so&0xFFFF]) {
          t=memory[so&0xFFFF]--;
          regs[fo]=memory[(so+t)&0xFFFF];
          condflag=1;
        } else {
          condflag=0;
        }
        break;
      case OP_DYN: do_dynamic_strings(fo,so); break;
      case OP_EATT: so=elem_def[so&255].attrib; goto store;
      case OP_EJMP: so=elem_def[so&255].event[fo]; goto jump;
      case OP_EMAT: condflag=(elem_def[so&255].attrib&(0x1000000UL<<fo)?1:0); break;
      case OP_EQ: condflag=(so==regs[fo]?1:0); break;
      case OP_EXCH: t=memory[so&0xFFFF]|(regs[fo]&~0xFFFF); memory[so&0xFFFF]=regs[fo]; regs[fo]=t; break;
      case OP_EXIT: condflag=(regs[fo]=board_info.exits[so&3])?1:0; break;
      case OP_FDEC: --so; if(!condflag) goto store; break;
      case OP_FINC: ++so; if(!condflag) goto store; break;
      case OP_FLET: if(!condflag) goto store; break;
      case OP_FLOA:
        t=convxy(so,x,y);
        if(t!=-1) {
          regs[fo]=pack_tile(b_main+t);
          if(b_main[t].stat) if(rs=find_statxy(b_main+t)) rs->x=rs->y=rs->instptr=65535,rs->layer=128,rs->delay=255;
          if(b_under[t].stat) if(rs=find_statxy(b_under+t)) rs->layer++;
          b_main[t]=b_under[t];
          b_under[t]=(Tile){};
          condflag=1;
        } else {
          condflag=0;
        }
        break;
      case OP_FORW: forw:
        so&=3; t=x,u=y;
        if(so==DIR_N) u--; else if(so==DIR_S) u++;
        if(so==DIR_W) t--; else if(so==DIR_E) t++;
        if(t>=0 && u>=0 && t<board_info.width && u<board_info.height) condflag=1,x=t,y=u; else condflag=0;
        break;
      case OP_FSEN:
        regs[fo]=condflag=0;
        if((rs=get_statxy(so)) && (rs->layer&0x23)==0x02 && (u=rs->sensor.stat) && u<=maxstat) {
          for(t=0;t<stats[u-1].count;t++) if(stats[u-1].xy[t].x==rs->x && stats[u-1].xy[t].y==rs->y && (stats[u-1].xy[t].layer&0x23)==0x22) {
            regs[fo]=rs->sensor.stat|(t<<16);
            condflag=1;
            break;
          }
        }
        break;
      case OP_GBF: regs[fo]=board_info.flag&~so; break;
      case OP_GBU: regs[fo]=board_info.userdata&~so; break;
      case OP_GCOU: so&=0xFFFF; so=(so<1?-1:so>maxstat?0:stats[so-1].count); goto store;
      case OP_GEX: if(rs=get_statxy(so)) regs[fo]=rs->extra; break;
      case OP_GIP: if(rs=get_statxy(so)) regs[fo]=rs->instptr; break;
      case OP_GIVE: status_vars[fo]+=so; break;
      case OP_GM1: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc1); goto store;
      case OP_GM2: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc2); goto store;
      case OP_GM3: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc3); goto store;
      case OP_GMOD:
        so&=0xFFFF;
        if(so>0 && so<=maxstat) {
          condflag=1;
          so=stats[so-1].mode;
          if(so&STAT_ZONERESTRICT) memory[MEM_ARG_J]=stats[so-1].zone;
          goto store;
        } else {
          condflag=0;
        }
        break;
      case OP_GMOV: if(so>0xFFFC) break; so=general_move(0,regs[fo],x,y,memory[so],memory[so+1],memory[so+2],memory[so+3]); goto setxy;
      case OP_GO: t=so; so=pc; pc=t; goto store;
      case OP_GOTO: goto jump;
      case OP_GPUS: if(so>0xFFFC) break; general_move(1,regs[fo],x,y,memory[so],memory[so+1],memory[so+2],memory[so+3]); break;
      case OP_GRTR: condflag=(regs[fo]>so?1:0); break;
      case OP_GSD: if(rs=get_statxy(so)) regs[fo]=rs->delay; break;
      case OP_GSEN:
        if(rs=get_statxy(so)) {
          condflag=rs->sensor.kind?1:0;
          regs[fo]=pack_tile(&rs->sensor);
        }
        break;
      case OP_GSPD: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].speed); goto store;
      case OP_GSXY: if(rs=get_statxy(so)) x=rs->x,y=rs->y,so=rs->delay|(rs->layer<<8),condflag=1; else condflag=so=0; goto store;
      case OP_GTMC: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_main[t].color; else condflag=0; break;
      case OP_GTMK: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_main[t].kind; else condflag=0; break;
      case OP_GTMP: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_main[t].param; else condflag=0; break;
      case OP_GTMS: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_main[t].stat; else condflag=0; break;
      case OP_GTOC: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_over[t].color; else condflag=0; break;
      case OP_GTOK: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_over[t].kind; else condflag=0; break;
      case OP_GTOP: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_over[t].param; else condflag=0; break;
      case OP_GTOS: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_over[t].stat; else condflag=0; break;
      case OP_GTUC: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_under[t].color; else condflag=0; break;
      case OP_GTUK: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_under[t].kind; else condflag=0; break;
      case OP_GTUP: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_under[t].param; else condflag=0; break;
      case OP_GTUS: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=b_under[t].stat; else condflag=0; break;
      case OP_HELP:
        condflag=0;
        if(so>=0) ntextbuf=snprintf(textbuf,8,"%04X",so&0xFFFF);
        if(load_help_file(textbuf) && show_text_window(1,1)) {
          condflag=1;
          regs[fo]=(*textbuf=='$'?strtol(textbuf+1,0,16):strtol(textbuf,0,10));
        }
        break;
      case OP_ICG: condflag=(++regs[fo]>so?1:0); break;
      case OP_ICNT: regs[fo]=count_items(regs[fo],so); break;
      case OP_IGET:
        if((t=memory[MEM_ARG_J])<inventory[u=((so>>8)&15?:memory[MEM_INVENTORY])&7].count) {
          condflag=1;
          switch(so&0xFF) {
            case 0: regs[fo]=inventory[u].item[t].item|(inventory[u].item[t].flag<<16); break;
            case 1: regs[fo]=inventory[u].item[t].item; break;
            case 2: regs[fo]=inventory[u].item[t].quantity; break;
            case 3: regs[fo]=inventory[u].item[t].flag; break;
            case 4: regs[fo]=inventory[u].item[t].ext0; break;
            case 5: regs[fo]=inventory[u].item[t].ext1; break;
            case 6: regs[fo]=inventory[u].item[t].ext2; break;
            case 7: regs[fo]=t+(u<<16); break;
            default: errx(1,"Improper IGET");
          }
        } else {
          condflag=0;
        }
        break;
      case OP_IGIV: regs[fo]=give_item(so,regs[fo],memory[MEM_ARG_J]); break;
      case OP_IMNU: t=show_item_window(so); if(condflag) regs[fo]=t; break;
      case OP_IMOV: regs[fo]=move_item(so,regs[fo],memory[MEM_ARG_J]); break;
      case OP_INC: ++so; goto store;
      case OP_INCL: ++so; goto lstore;
      case OP_INEW:
        t=w&0xFF;
        if(t<=0 || t>maxstat) break;
        rs=add_statxy(t);
        rs->layer=so;
        rs->x=x;
        rs->y=y;
        so=((rs-stats[t-1].xy)<<16)|t;
        goto store;
      case OP_INFO: so=request_info(so); goto store;
      case OP_INVE: do_inventory_op(fo,so); break;
      case OP_IPUT:
        if((t=memory[MEM_ARG_J])<inventory[u=((so>>8)&15?:memory[MEM_INVENTORY])&7].count) {
          condflag=1;
          switch(so&0xFF) {
            case 0: inventory[u].item[t]=(ItemSlot){.item=regs[fo]&0xFFFF,.flag=regs[fo]>>16,.quantity=(regs[fo]&0xFFFF?1:0)}; break;
            case 1: inventory[u].item[t].item=regs[fo]; break;
            case 2: inventory[u].item[t].quantity=regs[fo]; break;
            case 3: inventory[u].item[t].flag=regs[fo]; break;
            case 4: inventory[u].item[t].ext0=regs[fo]; break;
            case 5: inventory[u].item[t].ext1=regs[fo]; break;
            case 6: inventory[u].item[t].ext2=regs[fo]; break;
            case 7: if(inventory[(regs[fo]>>16)&7].count>=(regs[fo]&0xFFFF)) inventory[u].item[t]=inventory[(regs[fo]>>16)&7].item[regs[fo]&0xFFFF]; break;
            default: errx(1,"Improper IGET");
          }
        } else {
          condflag=0;
        }
        break;
      case OP_ITAK: regs[fo]=take_item(so,regs[fo],memory[MEM_ARG_J]); break;
      case OP_ITEM:
        switch(so&15) {
          case 0 ... 7: t=regs[so&7]; break;
          case 8: case 10: t=memory[MEM_ARG_J]; break;
          case 9: case 11: t=memory[MEM_ARG_K]; break;
          case 12: t=w; break; case 13: t=x; break; case 14: t=y; break; case 15: t=z; break;
        }
        t&=0xFFFF;
        if((so&14)==10) {
          if(t>=inventory[u=memory[MEM_INVENTORY]&7].count) break;
          t=inventory[u].item[t].item;
        }
        if(!t || t>nitemdefs) break;
        t--;
        switch((so>>8)&255) {
          case 0x00: so=itemdefs[t].class; goto store;
          case 0x01: so=itemdefs[t].element; goto store;
          case 0x02: so=itemdefs[t].flag; goto store;
          case 0x03: so=itemdefs[t].maxheap; goto store;
          case 0x04: so=itemdefs[t].weight; goto store;
          case 0x05: so=strlen(itemnames+itemdefs[t].name); goto store;
          case 0x06: item06: u=(ntextbuf<80?snprintf(textbuf+ntextbuf,81-ntextbuf,"%s",itemnames+itemdefs[t].name):0); if(u+ntextbuf<80) ntextbuf+=u; else ntextbuf=80; break;
          case 0x07: so=(itemdefs[t].script?1:0); goto store;
          case 0x08: so=strlen(itemnames+itemdefs[t].appearance); goto store;
          case 0x09: u=(ntextbuf<80?snprintf(textbuf+ntextbuf,81-ntextbuf,"%s",itemnames+itemdefs[t].appearance):0); if(u+ntextbuf<80) ntextbuf+=u; else ntextbuf=80; break;
          case 0x0A: so=itemdefs[t].price; goto store;
          case 0x0B: so=itemdefs[t].ext3; goto store;
          case 0x0C: so=itemdefs[t].ext4; goto store;
          case 0x0D: so=itemdefs[t].ext5; goto store;
          case 0x0E: so=itemdefs[t].parameter; goto store;
          case 0x0F: so=itemdefs[t].color; goto store;
          case 0x10: so=itemdefs[t].element|(itemdefs[t].color<<8)|(itemdefs[t].parameter<<16); goto store;
          case 0x11: item11: u=(ntextbuf<80?snprintf(textbuf+ntextbuf,81-ntextbuf,"%s",itemnames+(itemdefs[t].appearance?:itemdefs[t].name)):0); if(u+ntextbuf<80) ntextbuf+=u; else ntextbuf=80; break;
          case 0x12: if(itemdefs[t].flag&IDF_UNIDENTIFIED) goto item11; else goto item06;
          case 0x13: so=itemdefs[t].special; goto store;
          case 0x80: itemdefs[t].flag&=~IDF_UNIDENTIFIED; break;
          case 0x81: itemdefs[t].flag|=IDF_UNIDENTIFIED; break;
          case 0x82: itemdefs[t].flag&=~IDF_EVENT; break;
          case 0x83: itemdefs[t].flag|=IDF_EVENT; break;
          default: errx(1,"Improper use of ITEM instruction");
        }
        break;
      case OP_JEV: if(!(regs[fo]&1)) goto jump; break;
      case OP_JF: if(!condflag) goto jump; break;
      case OP_JNEG: if(regs[fo]<0) goto jump; break;
      case OP_JNZ: if(regs[fo]) goto jump; break;
      case OP_JOD: if(regs[fo]&1) goto jump; break;
      case OP_JPOS: if(regs[fo]>=0) goto jump; break;
      case OP_JT: if(condflag) goto jump; break;
      case OP_JZ: if(!regs[fo]) goto jump; break;
      case OP_KEYB:
        if(fo || so) errx(1,"Improper KEYB instruction at $%X",pc-1);
        stop_key_repeat();
        break;
      case OP_KILM: if((t=convxy(so,x,y))!=-1) break_tile(t,2,0,0,fo&4); goto died;
      case OP_KILO: if((t=convxy(so,x,y))!=-1) break_tile(t,3,0,0,fo&4); goto died;
      case OP_KILU: if((t=convxy(so,x,y))!=-1) break_tile(t,1,0,0,fo&4); goto died;
      case OP_LAST:
        so&=0xFFFF;
        if(!so || so>maxstat || !stats[so-1].count) {
          condflag=0;
        } else {
          so+=(stats[so-1].count-1)<<16;
          condflag=1;
        }
        goto store;
      case OP_LAY: if(rs=get_statxy(so)) { condflag=1; so=rs->layer; goto store; } else condflag=0; break;
      case OP_LESS: condflag=(regs[fo]<so?1:0); break;
      case OP_LET: goto store;
      case OP_LETL: goto lstore;
      case OP_LITE: calc_light(fo,so); break;
      case OP_LOCK: if(rs=get_statxy(so)) rs->layer=(rs->layer&0x03)|(regs[fo]&0xFC); break;
      case OP_LOG: if(config.debug) debug_log(fo,so,w,x,y,z,pc); break;
      case OP_LOOP: if(!regs[fo]) break; --regs[fo]; goto jump;
      case OP_LSH: regs[fo]=(so&~31?0:regs[fo]<<so); break;
      case OP_M1L:
        op=so&0xFFFF;
        t=regs[fo];
        regs[fo]=memory[op];
        u=run_program(pc,w,x,y,z);
        memory[op]=regs[fo];
        regs[fo]=t;
        return u;
      case OP_M2L:
        op=so&0xFFFF;
        t=regs[fo];
        regs[fo]=(memory[op]<<16)|memory[(op+1)&0xFFFF];
        u=run_program(pc,w,x,y,z);
        memory[op]=regs[fo]>>16;
        memory[(op+1)&0xFFFF]=regs[fo];
        regs[fo]=t;
        return u;
      case OP_MAX: if(so>regs[fo]) regs[fo]=so; break;
      case OP_MEM:
        t=memory[MEM_ARG_J]; u=memory[MEM_ARG_K];
        switch(fo) {
          case 2: while(so-- && !((t|u)&~0xFFFF)) memory[u++]=memory[t++]; break;
          case 3: fo=0; while(fo<ntextbuf && so-- && !(u&~0xFFFF)) memory[u]=(memory[u]&t&0xFF00)^(t<<8)^textbuf[fo],u++,fo++; break;
          case 4: while(so-- && !((t|u)&~0xFFFF)) ex=memory[u],memory[u++]=memory[t],memory[t++]=ex; break;
          case 5: while(so-- && !(u&~0xFFFF)) memory[u++]=t; break;
        }
        break;
      case OP_MESS: do_text_op(fo,so); memcpy(vtextbuf,textbuf,nvtextbuf=ntextbuf); vtextbuf[nvtextbuf]=0; if(vtexttime=(nvtextbuf?config.message_timer:0)) add_message_text(); break;
      case OP_MIN: if(so<regs[fo]) regs[fo]=so; break;
      case OP_MNEW:
        so=convxy(so,x,y);
        if(so==-1 || !b_main[so].stat || b_main[so].stat>maxstat) break;
        rs=add_statxy(b_main[so].stat);
        rs->layer=2;
        rs->x=so%board_info.width;
        rs->y=so/board_info.width;
        so=((rs-stats[b_main[so].stat-1].xy)<<16)|b_main[so].stat;
        goto store;
      case OP_MOD: if(so) condflag=1,regs[fo]%=so; else condflag=0; break;
      case OP_MOVE: so=general_move(0,regs[fo],x,y,(so&0xF8)+0x8800+(so&7)*0x1100,(so&0xFF00)+1,0,0); goto setxy;
      case OP_MTIL: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=pack_tile(b_main+t); else condflag=0; break;
      case OP_MUL: regs[fo]*=so; break;
      case OP_NEG: so=-so; goto store;
      case OP_NOT: so=~so; goto store;
      case OP_OMOV: so=general_move(0,regs[fo],x,y,(so&0xF8)+0x8800+(so&7)*0x1100,0xFFFF,0,0); goto setxy;
      case OP_ONEW:
        so=convxy(so,x,y);
        if(so==-1 || !b_over[so].stat || b_over[so].stat>maxstat) break;
        rs=add_statxy(b_over[so].stat);
        rs->layer=3;
        rs->x=so%board_info.width;
        rs->y=so/board_info.width;
        so=((rs-stats[b_over[so].stat-1].xy)<<16)|b_over[so].stat;
        goto store;
      case OP_OPJ: case OP_OPK:
        op=((op&0x1FF)==OP_OPJ?MEM_ARG_J:MEM_ARG_K);
        switch(fo) {
          case 0: memory[op]=so; break;
          case 1: if(!condflag) memory[op]=so; break;
          case 2: if(condflag) memory[op]=so; break;
          case 3: memory[op]+=so; break;
          case 4: memory[op]-=so; break;
          case 5: memory[so&0xFFFF]=memory[op]; break;
          case 6: memory[op]=memory[so&0xFFFF]; break;
          case 7: memory[op]=memory[(so+memory[op])&0xFFFF]; break;
          case 8: condflag=(memory[op]==(so&0xFFFF)?1:0); break;
          case 9: if(memory[op]==(so&0xFFFF)) condflag=1; break;
          case 10: if(!memory[op]) goto jump; break;
          case 11: if(memory[op]) goto jump; break;
          case 13: so=memory[(so+memory[op])&0xFFFF]; goto jump;
          case 14: if(!memory[op]) break; --memory[op]; goto jump;
        }
        break;
      case OP_OR: regs[fo]|=so; break;
      case OP_OREQ: if(so==regs[fo]) condflag=1; break;
      case OP_OVM: do_overlay_memory(fo,so); break;
      case OP_PACK:
        t=convxy(0,x,y);
        if(t!=-1) {
          condflag=1;
          so=t+1;
        } else {
          condflag=0;
        }
        goto store;
      case OP_PAR:
        if((rs=get_statxy(so)) && stats[(so&0xFFFF)-1].text && rs->instptr<stats[(so&0xFFFF)-1].length) {
          regs[fo]=stats[(so&0xFFFF)-1].text[rs->instptr];
          if(regs[fo] && regs[fo]!='\n') condflag=1; else condflag=0;
          rs->instptr+=condflag;
        } else {
          condflag=regs[fo]=0;
        }
        break;
      case OP_PARC:
        if((rs=get_statxy(so)) && stats[(so&0xFFFF)-1].text && rs->instptr<stats[(so&0xFFFF)-1].length) {
          condflag=parse_condition(stats+(so&0xFFFF)-1,rs,&rs->instptr);
        }
        break;
      case OP_PARD:
        condflag=0;
        if((rs=get_statxy(so)) && stats[(so&0xFFFF)-1].text && rs->instptr<stats[(so&0xFFFF)-1].length) {
          so=parse_direction(stats+(so&0xFFFF)-1,rs,&rs->instptr);
          if(condflag) regs[fo]=so;
        }
        break;
      case OP_PARG:
        condflag=0;
        if((rs=get_statxy(so)) && stats[(so&0xFFFF)-1].text && rs->instptr<stats[(so&0xFFFF)-1].length) {
          so=parse_letter(stats+(so&0xFFFF)-1,rs,&rs->instptr);
          if(condflag) regs[fo]=so;
        }
        break;
      case OP_PARI:
        condflag=0;
        if((rs=get_statxy(so)) && stats[(so&0xFFFF)-1].text && rs->instptr<stats[(so&0xFFFF)-1].length) {
          so=parse_item(stats+(so&0xFFFF)-1,rs,&rs->instptr);
          if(condflag) regs[fo]=so;
        }
        break;
      case OP_PARN:
        condflag=0;
        if((rs=get_statxy(so)) && stats[(so&0xFFFF)-1].text && rs->instptr<stats[(so&0xFFFF)-1].length) {
          so=parse_number(stats+(so&0xFFFF)-1,rs,&rs->instptr);
          if(condflag) regs[fo]=so;
        }
        break;
      case OP_PARY:
        condflag=0;
        if((rs=get_statxy(so)) && stats[(so&0xFFFF)-1].text && rs->instptr<stats[(so&0xFFFF)-1].length) condflag=parse_name(stats+(so&0xFFFF)-1,rs,&rs->instptr);
        break;
      case OP_PBF: board_info.flag=so; break;
      case OP_PBU: board_info.userdata=so; break;
      case OP_PEEK: regs[fo]=memory[so&0xFFFF]; break;
      case OP_PEEL: so=memory[so&0xFFFF]; goto lstore;
      case OP_PEER: regs[fo]=memory[(so+regs[fo])&0xFFFF]; break;
      case OP_PEX: if(rs=get_statxy(so)) rs->extra=regs[fo]; break;
      case OP_PICK:
        condflag=0;
        if(rs=get_statxy(so)) {
          x=rs->x; y=rs->y;
          if(x<0 || x>board_info.width || y<0 || y>board_info.height || (rs->layer&3)!=2) break;
          u=x+y*board_info.width;
          regs[fo]=pack_tile(b_main+u);
          if(!rs->sensor.kind) {
            b_main[u]=b_under[u];
            if(b_main[u].stat) if(rs=find_statxy(b_under+u)) rs->layer++;
            b_under[u]=(Tile){};
          } else {
            b_main[u]=rs->sensor;
            if(rs->sensor.stat) restore_sensor_stat(u);
            rs->sensor=(Tile){};
          }
          condflag=1;
        }
        break;
      case OP_PIP: if(rs=get_statxy(so)) rs->instptr=regs[fo]; break;
      case OP_PM1: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc1=regs[fo]; break;
      case OP_PM2: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc2=regs[fo]; break;
      case OP_PM3: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc3=regs[fo]; break;
      case OP_PMOD: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].mode=regs[fo]; stats[so-1].mode=(regs[fo]&STAT_ZONERESTRICT)?memory[MEM_ARG_J]:0; break;
      case OP_POKE: memory[so&0xFFFF]=regs[fo]; break;
      case OP_PROP: do_varproperty_op(fo,so); break;
      case OP_PSD: if(rs=get_statxy(so)) rs->delay=regs[fo]; break;
      case OP_PSEN:
        if(rs=get_statxy(so)) {
          rs->sensor.kind=regs[fo]&0xFF;
          rs->sensor.color=(regs[fo]>>8)&0xFF;
          rs->sensor.param=(regs[fo]>>16)&0xFF;
          rs->sensor.stat=(regs[fo]>>24)&0xFF;
        }
        break;
      case OP_PSPD: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].speed=regs[fo]; break;
      case OP_PSXY: if(rs=get_statxy(so)) rs->x=x,rs->y=y,rs->delay=so,rs->layer=so>>8,condflag=1; else condflag=0; break;
      case OP_PTM:
        if((t=convxy(so,x,y))!=-1) {
          condflag=1;
          u=b_main[t].stat;
          if(u && u!=((regs[fo]>>24)&0xFF) && (rs=find_statxy(b_main+t))) rs->x=rs->y=rs->instptr=65535,rs->layer=128,rs->delay=255;
          b_main[t].kind=regs[fo]&0xFF;
          b_main[t].color=(regs[fo]>>8)&0xFF;
          b_main[t].param=(regs[fo]>>16)&0xFF;
          b_main[t].stat=(regs[fo]>>24)&0xFF;
          if(b_main[t].stat && u!=b_main[t].stat && b_main[t].stat<=maxstat) {
            rs=add_statxy(b_main[t].stat);
            rs->x=t%board_info.width;
            rs->y=t/board_info.width;
            rs->layer=2;
          }
        } else {
          condflag=0;
        }
        break;
      case OP_PTMC: if((t=convxy(so,x,y))!=-1) condflag=1,b_main[t].color=regs[fo]; else condflag=0; break;
      case OP_PTMK: if((t=convxy(so,x,y))!=-1) condflag=1,b_main[t].kind=regs[fo]; else condflag=0; break;
      case OP_PTMP: if((t=convxy(so,x,y))!=-1) condflag=1,b_main[t].param=regs[fo]; else condflag=0; break;
      case OP_PTMS: if((t=convxy(so,x,y))!=-1) condflag=1,b_main[t].stat=regs[fo]; else condflag=0; break;
      case OP_PTOC: if((t=convxy(so,x,y))!=-1) condflag=1,b_over[t].color=regs[fo]; else condflag=0; break;
      case OP_PTOK: if((t=convxy(so,x,y))!=-1) condflag=1,b_over[t].kind=regs[fo]; else condflag=0; break;
      case OP_PTOP: if((t=convxy(so,x,y))!=-1) condflag=1,b_over[t].param=regs[fo]; else condflag=0; break;
      case OP_PTOS: if((t=convxy(so,x,y))!=-1) condflag=1,b_over[t].stat=regs[fo]; else condflag=0; break;
      case OP_PTSC:
        if(rs=get_statxy(so)) {
          x=rs->x; y=rs->y;
          if(x<0 || x>board_info.width || y<0 || y>board_info.height || !(rs->layer&3)) break;
          u=x+y*board_info.width;
          ((rs->layer&3)==1?b_under:(rs->layer&3)==2?b_main:b_over)[u].color=regs[fo];
        } break;
      case OP_PTSP:
        if(rs=get_statxy(so)) {
          x=rs->x; y=rs->y;
          if(x<0 || x>board_info.width || y<0 || y>board_info.height || !(rs->layer&3)) break;
          u=x+y*board_info.width;
          ((rs->layer&3)==1?b_under:(rs->layer&3)==2?b_main:b_over)[u].param=regs[fo];
        } break;
      case OP_PTU:
        if((t=convxy(so,x,y))!=-1) {
          condflag=1;
          u=b_under[t].stat;
          if(u && u!=((regs[fo]>>24)&0xFF) && (rs=find_statxy(b_under+t))) rs->x=rs->y=rs->instptr=65535,rs->layer=128,rs->delay=255;
          b_under[t].kind=regs[fo]&0xFF;
          b_under[t].color=(regs[fo]>>8)&0xFF;
          b_under[t].param=(regs[fo]>>16)&0xFF;
          b_under[t].stat=(regs[fo]>>24)&0xFF;
          if(b_under[t].stat && u!=b_under[t].stat && b_under[t].stat<=maxstat) {
            rs=add_statxy(b_main[t].stat);
            rs->x=t%board_info.width;
            rs->y=t/board_info.width;
            rs->layer=1;
          }
        } else {
          condflag=0;
        }
        break;
      case OP_PTUC: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].color=regs[fo]; else condflag=0; break;
      case OP_PTUK: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].kind=regs[fo]; else condflag=0; break;
      case OP_PTUP: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].param=regs[fo]; else condflag=0; break;
      case OP_PTUS: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].stat=regs[fo]; else condflag=0; break;
      case OP_PUSH: general_move(1,regs[fo],x,y,(so&0xF8)+0x8800+(so&7)*0x1100,(so&0xFF00)+1,0,0); break;
      case OP_REGL: load_registers(fo,so); break;
      case OP_REGS: save_registers(fo,so); break;
      case OP_RETS:
        memory[MEM_CALL_STATUS]=(so&0x1FFF)+(fo<<13);
        if(t=memory[memory[MEM_CALL_STACK]]) {
          regs[fo]=so;
          memory[MEM_RETURNED_PC]=pc;
          pc=memory[(t+memory[MEM_CALL_STACK])&0xFFFF];
          --memory[memory[MEM_CALL_STACK]];
        }
        break;
      case OP_REVB: revert_lump_by_number(so,"BRD"); break;
      case OP_REWD:
        if(!w) break;
        --w;
        switch(fo) {
          case 0: regs[0]=w; break;
          case 1: regs[1]=w; break;
          case 2: --x; break;
          case 3: ++x; break;
          case 4: --y; break;
          case 5: ++y; break;
          case 6: --z; break;
          case 7: ++z; break;
        }
        goto jump;
      case OP_ROB: if(status_vars[fo]>so) status_vars[fo]-=so,condflag=1; else status_vars[fo]=0,condflag=0; break;
      case OP_RSH: regs[fo]=(so&~31?(regs[fo]<0?-1:0):regs[fo]>>so); break;
      case OP_RSUB: regs[fo]=so-regs[fo]; break;
      case OP_RUN: run_script(regs[fo]&0xFFFF,(regs[fo]>>16)&0xFFFF,so); break;
      case OP_SCAN: so=scan_board(regs[fo],so&0xFF,x,y); if(!so) break; if(regs[fo]) regs[fo]=so; else goto unpack0; break;
      case OP_SCFR:
        if(rs=get_statxy(so)) switch(fo) {
          case 0: frame_push(stats+(so&0xFF)-1,rs,0,memory[MEM_ARG_J]); break;
          case 1: frame_push(stats+(so&0xFF)-1,rs,'U',0); break;
          case 2: frame_push(stats+(so&0xFF)-1,rs,'C',0); break;
          case 3: frame_push(stats+(so&0xFF)-1,rs,3,memory[MEM_ARG_J]); break;
          case 4: frame_push(stats+(so&0xFF)-1,rs,'E',0); break;
          case 5:
            so&=0xFF;
            if(!stats[so-1].frame) break;
            for(t=stats[so-1].frame;t<stats[so-1].length;t++) stats[so-1].text[t]='.';
            for(t=0;t<stats[so-1].count;t++) stats[so-1].xy[t].frame=0;
            break;
          case 6: condflag=(frame_return(stats+(so&0xFF)-1,rs,0)?1:0); break;
          case 7: so&=0xFF; condflag=(rs->frame && stats[so-1].frame && (t=stats[so-1].text[rs->frame-1]) && t!='X' && t!='.' && t!='\'')?1:0; break;
        }
        break;
      case OP_SEEK:
        if(rs=get_statxy(so)) {
          t=rs->x-x; u=rs->y-y;
          if(t && u) {
            regs[fo]=dice(2)?(t>0?DIR_E:DIR_W):(u>0?DIR_S:DIR_N);
          } else {
            if(t>0) regs[fo]=DIR_E;
            if(t<0) regs[fo]=DIR_W;
            if(u>0) regs[fo]=DIR_S;
            if(u<0) regs[fo]=DIR_N;
          }
        }
        break;
      case OP_SEND: if(so>0 && so<=ngtext) send_message(regs[fo],gtext[so],0); else if(!so) send_message(regs[fo],textbuf,0); break;
      case OP_SEX: so=(Sint16)so; goto store;
      case OP_SFX: if(soundon && so && so<=ngtext) audio_set_sfx(so>=0?gtext[so]:textbuf); break;
      case OP_SGN: so=(so<0?-1:so>0?1:0); goto store;
      case OP_SIM: so=statxy_index_at(convxy(so,x,y),2,b_main); condflag=(so?1:0); goto store;
      case OP_SIN:
        if(!(so&0xFFFF) || (so&0xFFFF)>maxstat) {
          condflag=so=0;
        } else if(stats[(so&0xFFFF)-1].count>((so>>16)&0xFFFF)+1) {
          condflag=1;
          so+=0x10000;
        } else {
          condflag=0;
          so&=0xFFFF;
        }
        goto store;
      case OP_SINK:
        t=convxy(so,x,y);
        if(t!=-1 && !b_under[t].stat && !((elem_def[b_main[t].kind].attrib|elem_def[b_under[t].kind].attrib)&A_PERMANENT)) {
          regs[fo]=pack_tile(b_under+t);
          if(b_main[t].stat) if(rs=find_statxy(b_main+t)) rs->layer--;
          b_under[t]=b_main[t];
          b_main[t]=(Tile){};
          condflag=1;
        } else {
          condflag=0;
        }
        break;
      case OP_SIO: so=statxy_index_at(convxy(so,x,y),3,b_over); condflag=(so?1:0); goto store;
      case OP_SIP:
        if(so>=0x10000) {
          condflag=1;
          so-=0x10000;
        } else {
          condflag=0;
        }
        goto store;
      case OP_SIU: so=statxy_index_at(convxy(so,x,y),1,b_under); condflag=(so?1:0); goto store;
      case OP_SIXY: if((rs=get_statxy(so)) && (so=convxy(0,rs->x,rs->y)+1)) condflag=1; else condflag=so=0; goto store;
      case OP_SMOV: general_move(0,regs[fo],x,y,(so&0xF8)+0x8804+(so&7)*0x1100,(so&0xFF00)+1,0,0); break;
      case OP_SPIN: do_spin(x,y,fo,so); break;
      case OP_SPOK: memory[so&0xFFFF]=fo; break;
      case OP_STEX:
        condflag=0;
        t=w&0xFF;
        if(t<=0 || t>maxstat || stats[t-1].text) break;
        switch(fo) {
          case 1:
            if(so>0 && so<ngtext) load_script_library(stats+t-1,gtext[so]);
            else if(so<0 && so>=-ndynastr) load_script_library(stats+t-1,dynastr[~so].text);
            else if(so<-255 && so>=-271) load_script_library(stats+t-1,namedflag[-255-so].name);
            break;
          case 2:
            so&=0xFF;
            if(so>0 && so<=maxstat && stats[so-1].text && (stats[t-1].text=strdup(stats[so-1].text))) stats[t-1].length=stats[so-1].length;
            break;
          case 3:
            if(so>0 && so<ngtext) stats[t-1].text=strdup((char*)gtext[so]);
            else if(so<0 && so>=-ndynastr) stats[t-1].text=strdup((char*)dynastr[~so].text);
            else if(so<-255 && so>=-271) stats[t-1].text=strdup((char*)namedflag[-255-so].name);
            if(stats[t-1].text) stats[t-1].length=strlen(stats[t-1].text);
            break;
          case 4:
            if(so>0 && so<=nitemdefs && itemdefs[so].script) {
              if(itemnames[itemdefs[so].script]=='@' && itemnames[itemdefs[so].script+1]=='!') {
                load_script_library(stats+t-1,itemnames+itemdefs[so].script+2);
              } else {
                stats[t-1].text=strdup((char*)itemnames+itemdefs[so].script);
                if(!stats[t-1].text) err(1,"Allocation failed");
              }
              if(stats[t-1].text) stats[t-1].length=strlen(stats[t-1].text);
            }
            break;
        }
        if(stats[t-1].text && !stats[t-1].text[0]) free(stats[t-1].text),stats[t-1].text=0;
        if(stats[t-1].text) condflag=1;
        break;
      case OP_SUB: regs[fo]-=so; break;
      case OP_SWPA: t=regs[0]; regs[0]=so; so=t; goto store;
      case OP_SWPB: t=regs[1]; regs[1]=so; so=t; goto store;
      case OP_SWPC: t=regs[2]; regs[2]=so; so=t; goto store;
      case OP_SWPD: t=regs[3]; regs[3]=so; so=t; goto store;
      case OP_SWPE: t=regs[4]; regs[4]=so; so=t; goto store;
      case OP_SWPF: t=regs[5]; regs[5]=so; so=t; goto store;
      case OP_SWPG: t=regs[6]; regs[6]=so; so=t; goto store;
      case OP_SWPH: t=regs[7]; regs[7]=so; so=t; goto store;
      case OP_SWPJ: t=memory[MEM_ARG_J]; memory[MEM_ARG_J]=so; so=t; goto store;
      case OP_SWPK: t=memory[MEM_ARG_K]; memory[MEM_ARG_K]=so; so=t; goto store;
      case OP_SWPW: t=w; w=so; so=t; goto store;
      case OP_SWPX: t=x; x=so; so=t; goto store;
      case OP_SWPY: t=y; y=so; so=t; goto store;
      case OP_SWPZ: t=z; z=so; so=t; goto store;
      case OP_TAKE: if(status_vars[fo]>=so) status_vars[fo]-=so,condflag=1; else condflag=0; break;
      case OP_TDEC: --so; if(condflag) goto store; break;
      case OP_TELE:
        condflag=0;
        if(rs=get_statxy(regs[fo])) {
          if(stats[(regs[fo]&255)-1].mode&STAT_INDEPENDENT) {
            if(!t) rs->x=x,rs->y=y,condflag=1;
            break;
          }
          if((rs->layer&3)!=2 || rs->x>=board_info.width || rs->y>=board_info.height) break;
          t=convxy(so,x,y);
          if(t==-1) break;
          if(b_under[t].stat && !(elem_def[b_main[t].kind].attrib&A_FLOOR)) break;
          if(elem_def[b_under[t].kind].attrib&A_PERMANENT) break;
          u=rs->x+rs->y*board_info.width;
          if(rs->x>=board_info.width || rs->y>=board_info.height || !b_main[u].stat) break;
          if(t==u) {
            condflag=1;
            break;
          }
          if(rs->sensor.stat) move_sensor_stat(rs->sensor.stat,rs->x,rs->y,t&board_info.width,t/board_info.width);
          rs->x=t%board_info.width; rs->y=t/board_info.width;
          b_under[t]=b_main[t];
          if(rs=find_statxy(b_main+t)) rs->layer--;
          b_main[t]=b_main[u];
          b_main[u]=b_under[u];
          if(rs=find_statxy(b_under+u)) rs->layer++;
          b_under[u]=(Tile){};
          condflag=1;
        }
        break;
      case OP_TEXT: do_text_op(fo,so); break;
      case OP_TINC: ++so; if(condflag) goto store; break;
      case OP_TLET: if(condflag) goto store; break;
      case OP_TMAT: if((t=convxy(so,x,y))!=-1) condflag=(elem_def[b_main[t].kind].attrib&(0x1000000UL<<fo)?1:0); break;
      case OP_TSTB: condflag=((1L<<(regs[fo]&31))&so)?1:0; break;
      case OP_UMAT: if((t=convxy(so,x,y))!=-1) condflag=(elem_def[b_under[t].kind].attrib&(0x1000000UL<<fo)?1:0); break;
      case OP_UNEW:
        so=convxy(so,x,y);
        if(so==-1 || !b_under[so].stat || b_under[so].stat>maxstat) break;
        rs=add_statxy(b_under[so].stat);
        rs->layer=1;
        rs->x=so%board_info.width;
        rs->y=so/board_info.width;
        so=((rs-stats[b_under[so].stat-1].xy)<<16)|b_under[so].stat;
        goto store;
      case OP_UNPC: unpack0: if(!so--) break; x=so%board_info.width; y=so/board_info.width; break;
      case OP_UPTO: condflag=(regs[fo]<so?1:0); regs[fo]+=condflag; break;
      case OP_URSH: regs[fo]=(so&~31?0:((Uint32)regs[fo])>>so); break;
      case OP_UTIL: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=pack_tile(b_under+t); else condflag=0; break;
      case OP_VBC: memory[(so+(regs[fo]>>4))&0xFFFF]&=~(1<<(regs[fo]&15)); break;
      case OP_VBS: memory[(so+(regs[fo]>>4))&0xFFFF]|=(1<<(regs[fo]&15)); break;
      case OP_VBT: condflag=(memory[(so+(regs[fo]>>4))&0xFFFF]&(1<<(regs[fo]&15))?1:0); break;
      case OP_VGET: so=status_vars[so&15]; goto store;
      case OP_VPUT: status_vars[so&15]=regs[fo]; break;
      case OP_VSET: status_vars[fo]=so; break;
      case OP_WARP:
        memory[MEM_WARP_TO]=regs[fo];
        memory[MEM_WARP_CALL]=(so&0xFFFF?:1);
        memory[MEM_WARP_X_HI]=x>>16; memory[MEM_WARP_X_LO]=x;
        memory[MEM_WARP_Y_HI]=y>>16; memory[MEM_WARP_Y_LO]=y;
        memory[MEM_WARP_Z_HI]=z>>16; memory[MEM_WARP_Z_LO]=z;
        break;
      case OP_WEEK: regs[fo]=(memory[so&0xFFFF]<<16)|memory[(so+1)&0xFFFF]; break;
      case OP_WOKE: memory[so&0xFFFF]=so>>16; memory[(so+1)&0xFFFF]=so; break;
      case OP_XOR: regs[fo]^=so; break;
      case OP_XORN: regs[fo]^=~so; break;
      case OP_XYZ:
        condflag=in_zone(x,y,so);
        switch(fo) {
          case 1: zone_remove(x,y,so); break;
          case 2: zone_add(x,y,so,0); break;
          case 3: zone_add(x,y,so,1); break;
          case 4: zone_remove(x,y,so); zone_add(x,y,so,0); break;
          case 5: zone_remove(x,y,so); zone_add(x,y,so,1); break;
          case 6: if(!condflag) zone_add(x,y,so,0); break;
          case 7: if(!condflag) zone_add(x,y,so,1); break;
        }
        break;
      case OP_ZENU:
        condflag=0;
        if(regs[fo]<0) break;
        if((so&0x10) && (x<0 || y<0 || x>=board_info.width || y>=board_info.height)) break;
        t=zone_enum(fo,so);
        if(condflag) {
          if(so&0x10) ozone[so&15]->xy[t]=(OrdZoneXY){x,y}; else x=ozone[so&15]->xy[t].x,y=ozone[so&15]->xy[t].y;
          if(so&0x20) {
            ozone[so&15]->ncells--;
            memmove(ozone[so&15]->xy+t,ozone[so&15]->xy+t+1,(ozone[so&15]->ncells-t)*sizeof(OrdZoneXY));
          }
          regs[fo]=t+((so>>6)&1)-((so>>7)&1);
        }
        break;
      case OP_ZEX: so=(Uint16)so; goto store;
      case OP_ZINF: regs[fo]=zone_info(so,regs[fo]); break;
      case OP_ZMOV: condflag=zone_move(so,regs[fo]&3); break;
      case OP_ZROT: for(t=0;t<16;t++) if(so&(1UL<<t)) zone_rotation(t,fo>>2); break;
      case OP_ZSEN:
        if(rs=get_statxy(so)) {
          condflag=rs->sensor.kind?1:0;
          regs[fo]=pack_tile(&rs->sensor);
          rs->sensor=(Tile){};
        }
        break;
      default: errx(1,"Unimplemented opcode $%X at $%X",op&0x1FF,pc-1);
    }
    continue;
    store:
    switch(fo) {
      case 0 ... 7: regs[fo]=so; break;
      case 8: memory[MEM_RETURNED_PC]=pc; return so; break;
      case 9: condflag=(so?1:0); break;
      case 10:
        memory[MEM_COROUTINE_U_W_HI]=w>>16; memory[MEM_COROUTINE_U_W_LO]=w; w=so;
        t=(memory[MEM_COROUTINE_U_X_HI]<<16)|memory[MEM_COROUTINE_U_X_LO]; memory[MEM_COROUTINE_U_X_HI]=x>>16; memory[MEM_COROUTINE_U_X_LO]=x; x=t;
        t=(memory[MEM_COROUTINE_U_Y_HI]<<16)|memory[MEM_COROUTINE_U_Y_LO]; memory[MEM_COROUTINE_U_Y_HI]=y>>16; memory[MEM_COROUTINE_U_Y_LO]=y; y=t;
        t=(memory[MEM_COROUTINE_U_Z_HI]<<16)|memory[MEM_COROUTINE_U_Z_LO]; memory[MEM_COROUTINE_U_Z_HI]=z>>16; memory[MEM_COROUTINE_U_Z_LO]=z; z=t;
        t=pc; pc=memory[MEM_COROUTINE_U_PC]; memory[MEM_COROUTINE_U_PC]=t;
        break;
      case 11:
        memory[MEM_COROUTINE_V_W_HI]=w>>16; memory[MEM_COROUTINE_V_W_LO]=w; w=so;
        t=(memory[MEM_COROUTINE_V_X_HI]<<16)|memory[MEM_COROUTINE_V_X_LO]; memory[MEM_COROUTINE_V_X_HI]=x>>16; memory[MEM_COROUTINE_V_X_LO]=x; x=t;
        t=(memory[MEM_COROUTINE_V_Y_HI]<<16)|memory[MEM_COROUTINE_V_Y_LO]; memory[MEM_COROUTINE_V_Y_HI]=y>>16; memory[MEM_COROUTINE_V_Y_LO]=y; y=t;
        t=(memory[MEM_COROUTINE_V_Z_HI]<<16)|memory[MEM_COROUTINE_V_Z_LO]; memory[MEM_COROUTINE_V_Z_HI]=z>>16; memory[MEM_COROUTINE_V_Z_LO]=z; z=t;
        t=pc; pc=memory[MEM_COROUTINE_V_PC]; memory[MEM_COROUTINE_V_PC]=t;
        break;
      case 12: w=so; break;
      case 13: x=so; break;
      case 14: y=so; break;
      case 15: z=so; break;
    }
    continue;
    lstore:
    t=regs[fo];
    regs[fo]=so;
    u=run_program(pc,w,x,y,z);
    regs[fo]=t;
    return u;
    setxy:
    if(regs[fo]) regs[fo]=so; else so--,x=so%board_info.width,y=so/board_info.width;
    continue;
    jump:
    memory[MEM_RETURNED_PC]=pc;
    pc=so;
    if(pc<256) return pc;
  }
}

static int system_menu(void) {
  Uint32 vm=audio_get_volume();
  Uint16 vol=vm&0xFFFF;
  Uint8 mu=vm>>16;
  Uint8 x=config.menu_x;
  Uint8 y=config.menu_y;
  Uint8 z;
  Uint8 sav=allow_saving();
  char buf[16];
  set_timer(0);
  autofire=0;
  v_status[1]='F';
  redraw0:
  if(!(v_mode&VIDEO_80COLUMNS)) x=0;
  config.menu_x=x; config.menu_y=y;
  update_screen();
  if(vtexttime) display_message_text();
  draw_border(0x19,x,y,x+40,y+16);
  draw_text(x+12,y," Super ZZ Zero ",0x1B,-1);
  // 012345678901234567890123456789012345
  // =F1== Menu         =F7== Q. Restore
  // =F2== Sound: ___   =F8== Spec. Option
  // =F3== Save         =F9== Messages
  // =F4== Restore      =F10= Quit
  // =F5== Q. Save      =F11= Print
  // =F6== Debug        =F12= Speed: ____
  // =PAUSE=   Pause/resume
  // =INS=     Next frame
  // =DEL=     Clear message
  // =S=       Speed: _____
  // =-/+=     Volume: ___
  // =M=       Msg. time: _____
  // (???)
  // (???)
  // =^v<>=    Menu position
  // 012345678901234567890123456789012345
  redraw1:
  draw_text(x+2,y+1," F1  ",0x30,-1); draw_text(x+8,y+1,"Menu",0x1F,-1);
  draw_text(x+2,y+2," F2  ",0x70,-1); // Sound
  draw_text(x+2,y+3," F3  ",0x30,-1); if(sav) draw_text(x+8,y+3,"Save",0x1F,-1);
  draw_text(x+2,y+4," F4  ",0x70,-1); draw_text(x+8,y+4,"Restore",0x1F,-1);
  draw_text(x+2,y+5," F5  ",0x30,-1); if(sav) draw_text(x+8,y+5,"Q. Save",0x1F,-1);
  draw_text(x+2,y+6," F6  ",0x70,-1); if(config.debug) draw_text(x+8,y+6,"Debug",0x1F,-1);
  draw_text(x+21,y+1," F7  ",0x30,-1); draw_text(x+27,y+1,"Q. Restore",0x1F,-1);
  draw_text(x+21,y+2," F8  ",0x70,-1); if(nspecopt) draw_text(x+27,y+2,"Spec. Option",0x1F,-1);
  draw_text(x+21,y+3," F9  ",0x30,-1); draw_text(x+27,y+3,"Messages",0x1F,-1);
  draw_text(x+21,y+4," F10 ",0x70,-1); draw_text(x+27,y+4,"Quit",0x1F,-1);
  draw_text(x+21,y+5," F11 ",0x30,-1); if(config.printer_type) draw_text(x+27,y+5,"Print",0x1F,-1);
  draw_text(x+21,y+6," F12 ",0x70,-1); draw_text(x+27,y+6,"Speed:",0x1F,-1);
  draw_text(x+34,y+6,playstate==PLAYSTATE_NORMAL?"NORM":playstate==PLAYSTATE_FAST?"FAST":"STOP",0x1A,-1);
  draw_text(x+2,y+7," PAUSE ",0x30,-1); draw_text(x+12,y+7,"Pause/resume",0x1F,-1);
  draw_text(x+2,y+8," INS ",0x70,-1); draw_text(x+12,y+8,"Next frame",0x1F,-1);
  draw_text(x+2,y+9," DEL ",0x30,-1); draw_text(x+12,y+9,"Clear message",0x1F,-1);
  draw_text(x+2,y+15," \x18\x19\x1B\x1A ",0x30,-1); draw_text(x+12,y+15,"Menu position",0x1F,-1);
  draw_text(x+2,y+10,"  S  ",0x70,-1); draw_text(x+12,y+10,"Speed:",0x1F,-1);
  if(playstate==PLAYSTATE_PAUSED) draw_text(x+21,y+10," ---- ",0x1A,6);
  else draw_text(x+22,y+10,buf,0x1A,snprintf(buf,16,"%5d",playstate==PLAYSTATE_FAST?config.speed_fast:config.speed));
  if(!(vm&0x800000)) {
    draw_text(x+8,y+2,"Sound:",0x1F,-1);
    draw_text(x+15,y+2,mu?"MUTE":"ON  ",0x1A,-1);
    draw_text(x+2,y+11," -/+ ",0x30,-1); draw_text(x+12,y+11,"Volume:",0x1F,-1);
    draw_text(x+24,y+11,buf,0x1A,snprintf(buf,16,"%3d",(vol+100)/327));
  }
  draw_text(x+2,y+12,"  M  ",0x70,-1); draw_text(x+12,y+12,"Msg. time:",0x1F,-1);
  draw_text(x+22,y+12,buf,0x1A,snprintf(buf,16,"%5d",config.message_timer));
  redisplay();
  do { if(!next_event()) errx(0,"No events available."); } while(event.type!=SDL_KEYDOWN);
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: case SDLK_F1: case SDLK_SPACE: case SDLK_RETURN: return 0;
    case SDLK_F2: if(mu<2) mu^=1; audio_set_volume(vol,mu); audio_set_sfx("@0ZCX"); break;
    case SDLK_F3: case SDLK_F4: case SDLK_F5: case SDLK_F7: case SDLK_F9: case SDLK_F10: case SDLK_INSERT: return 1;
    case SDLK_F6: if(config.debug) return 1; break;
    case SDLK_F8: special_option_menu(); goto redraw0;
    case SDLK_F12: *v_status=playstate=(playstate==PLAYSTATE_NORMAL?PLAYSTATE_FAST:PLAYSTATE_NORMAL); break;
    case SDLK_PAUSE: *v_status=playstate=(playstate==PLAYSTATE_PAUSED?PLAYSTATE_NORMAL:PLAYSTATE_PAUSED); break;
    case SDLK_DELETE: vtexttime=nvtextbuf=*vtextbuf=0; goto redraw0;
    case SDLK_UP: case SDLK_KP8: if(y>1) y-=2; goto redraw0;
    case SDLK_DOWN: case SDLK_KP2: if(y<7) y+=2; goto redraw0;
    case SDLK_LEFT: case SDLK_KP4: if(x>2) x-=3; goto redraw0;
    case SDLK_RIGHT: case SDLK_KP6: if(x<37) x+=3; goto redraw0;
    case SDLK_KP_MINUS: case SDLK_MINUS: if(vol>327) vol-=327; audio_set_volume(vol,mu); audio_set_sfx("@0ZCX"); break;
    case SDLK_KP_PLUS: case SDLK_PLUS: case SDLK_EQUALS: if(vol<32400) vol+=327; audio_set_volume(vol,mu); audio_set_sfx("@0ZCX"); break;
    case SDLK_m:
      *buf=0;
      ask_text("Message time?",buf,5);
      config.message_timer=((Uint16)strtol(buf,0,10))?:config.message_timer;
      goto redraw0;
    case SDLK_s:
      *buf=0;
      if(playstate!=PLAYSTATE_PAUSED) ask_text("Speed?",buf,5);
      if(playstate==PLAYSTATE_FAST && *buf) config.speed_fast=((Uint16)strtol(buf,0,10))?:config.speed_fast;
      if(playstate==PLAYSTATE_NORMAL && *buf) config.speed=((Uint16)strtol(buf,0,10))?:config.speed;
      goto redraw0;
  }
  goto redraw1;
}

static void message_scrollback(void) {
  char buf[71];
  Uint16 n,y;
  if(!scrback) return;
  reset:
  n=(nscrback+config.message_scrollback-24)%config.message_scrollback;
  memset(v_color,0x07,80*25);
  memset(v_char,0x00,80*25);
  memset(v_color,0x30,80);
  draw_text(0,0,"<ESC> Cancel  <\x18/\x19> Scroll  <INS> Note  <F11> Print",0x30,-1);
  redraw:
  draw_text(68,0,buf,0x3B,snprintf(buf,32,"%5d/%5d",(n+24+config.message_scrollback-nscrback)%config.message_scrollback?:config.message_scrollback,config.message_scrollback));
  memset(v_char+80,0x20,80*24);
  for(y=0;y<24;y++) draw_text(0,y+1,scrback[(n+y)%config.message_scrollback].text,0x07,80);
  redisplay();
  do { if(!next_event()) errx(0,"No events available."); } while(event.type!=SDL_KEYDOWN && event.type!=SDL_JOYBUTTONDOWN && event.type!=SDL_JOYBUTTONUP);
  if(event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP) {
    switch(do_joystick(JL_TEXT_WINDOW)) {
      case 13: return;
      case 24: goto up;
      case 25: goto down;
      case 'Z'+0x100: return;
      case '<'+0x100: goto prevpage;
      case '>'+0x100: goto nextpage;
      case '['+0x100: goto bottom;
      case ']'+0x100: goto bottom;
    }
  }
  if(event.type==SDL_KEYDOWN) switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: case SDLK_RETURN: return;
    case SDLK_END: case SDLK_KP1: bottom: n=(nscrback+config.message_scrollback-24)%config.message_scrollback; break;
    case SDLK_UP: case SDLK_KP8: up: n=(n+config.message_scrollback-1)%config.message_scrollback; break;
    case SDLK_DOWN: case SDLK_KP2: down: n=(n+1)%config.message_scrollback; break;
    case SDLK_PAGEUP: case SDLK_KP9: prevpage: n=(n+config.message_scrollback-24)%config.message_scrollback; break;
    case SDLK_PAGEDOWN: case SDLK_KP3: nextpage: n=(n+24)%config.message_scrollback; break;
    case SDLK_INSERT: case SDLK_KP0:
      *buf=0;
      ask_text("Note:",buf,70);
      if(*buf) {
        memcpy(scrback[nscrback].text,buf,71);
        if(++nscrback==config.message_scrollback) nscrback=0;
      }
      goto reset;
    case SDLK_F11:
      lpt_document() {
        for(y=0;y<config.message_scrollback;y++) {
          if(scrback[(nscrback+y)%config.message_scrollback].text[0]) {
            lpt_text(scrback[(nscrback+y)%config.message_scrollback].text,strlen(scrback[(nscrback+y)%config.message_scrollback].text));
          }
        }
      }
      goto redraw;
  }
  goto redraw;
}

static void stat_list_callback(Uint16 n,int y,void*uz) {
  char buf[81];
  Stat*s;
  draw_text(1,y,buf,0x0E,snprintf(buf,4,"%3u",n));
  if(n) {
    s=stats+n-1;
    draw_text(5,y,buf,0x07,snprintf(buf,80,"(%05u,%05u,%05u) Count=%05u Speed=%03u",s->misc1,s->misc2,s->misc3,s->count,s->speed));
  }
}

static void statxy_list_callback(Uint16 n,int y,void*uz) {
  StatXY*o=((Stat*)uz)->xy+n;
  char buf[81];
  draw_text(1,y,buf,7,snprintf(buf,80,"%5u: X=%05d Y=%05d Layer=$%02X Inst=%05d Delay=%03d %c",n,o->x,o->y,o->layer,o->instptr,o->delay,o->sensor.kind?'S':'.'));
}

static void debug_menu(void) {
  Uint8 vm=v_mode;
  int i;
  char buf[81];
  v_status[1]='D';
  v_mode=VIDEO_80COLUMNS;
  win_form("Debug menu") {
    win_picture(2) {
      draw_text(0,0,buf,0x07,snprintf(buf,81,"BRD:%u  SCR:%u  Scroll:%d,%d",cur_board_id,cur_screen_id,(int)scroll_x,(int)scroll_y));
      draw_text(0,1,buf,0x07,snprintf(buf,81,"rseed=%llu",(unsigned long long)rseed));
    }
    win_command('1',"User command #1") {
      run_program(memory[MEM_KEY_EVENT],1,0,0,-1);
      break;
    }
    win_command('2',"User command #2") {
      run_program(memory[MEM_KEY_EVENT],2,0,0,-1);
      break;
    }
    win_command('3',"User command #3") {
      run_program(memory[MEM_KEY_EVENT],3,0,0,-1);
      break;
    }
    win_command('4',"User command #4") {
      run_program(memory[MEM_KEY_EVENT],4,0,0,-1);
      break;
    }
    win_command('v',"Status variables...") {
      win_form("Status variables") {
        buf[1]=':'; buf[2]=0;
        for(i=0;i<16;i++) win_numeric(*buf=i+(i&8?'S'-8:'A'),buf,status_vars[i],0,-1);
        win_blank();
        win_command_esc(0,"Cancel") break;
      }
    }
    win_command('S',"Stats...") {
      int n;
      win_form("Stats") {
        win_list(maxstat+1,0,stat_list_callback,n) {
          if(n) win_form("Stat XY list") {
            if(stats[n-1].count) win_list(stats[n-1].count,stats+n-1,statxy_list_callback,i);
            win_blank();
            win_command('E',"Edit text") {
              stats[n-1].text=text_editor(stats[n-1].text);
              stats[n-1].length=(stats[n-1].text?strlen(stats[n-1].text):0);
            }
            win_command_esc(0,"Cancel") break;
          }
        }
        win_blank();
        win_command_esc(0,"Cancel") break;
      }
    }
    win_command('R',"Registers...") {
      win_form("Registers") {
        buf[1]=':'; buf[2]=0;
        for(i=0;i<8;i++) win_numeric(*buf=i+'A',buf,regs[i],0,-1);
        win_boolean('i',"Condition flag",condflag,1);
        win_blank();
        win_command_esc(0,"Cancel") break;
      }
    }
    win_command('M',"Memory...") {
      Uint16 a=0;
      int x,y;
      memset(v_color,7,80*25);
      memset(v_char,32,80*25);
      for(;;) {
        for(x=0;x<8;x++) v_char[x*5+7]="0123456789ABCDEF"[x];
        for(y=0;y<16;y++) {
          draw_text(0,y+1,buf,0x07,snprintf(buf,81,"%04X:",(a+8*y)&0xFFFF));
          for(x=0;x<8;x++) {
            draw_text(x*5+6,y+1,buf,0x0F,snprintf(buf,81,"%04X",memory[(a+8*y+x)&0xFFFF]));
            v_char[y*80+x+x+128]=memory[(a+8*y+x)&0xFFFF];
            v_char[y*80+x+x+129]=memory[(a+8*y+x)&0xFFFF]>>8;
          }
        }
        v_color[80]=v_color[81]=v_color[82]=0x17;
        redisplay();
        if(!next_event()) errx(0,"No events available.");
        if(event.type==SDL_KEYDOWN) switch(x=event.key.keysym.unicode) {
          case 13: case 27: goto endmem;
          case '0' ... '9': a<<=4; a+=(x-'0')<<4; break;
          case 'A' ... 'F': a<<=4; a+=(x+10-'A')<<4; break;
          case 'a' ... 'f': a<<=4; a+=(x+10-'a')<<4; break;
          case '[': a-=16; break;
          case ']': a+=16; break;
          case '{': a-=128; break;
          case '}': a+=128; break;
        }
      }
      endmem:
      win_refresh();
    }
    win_command('W',"Write memory...") {
      Uint16 a=0;
      Uint16 b=0;
      win_form("Write memory") {
        win_numeric('A',"Address: ",a,0,0xFFFF);
        win_numeric('V',"Value: ",b,0,0xFFFF);
        win_command('W',"Write") memory[a]=b;
        win_command_esc(0,"Done") break;
      }
    }
    win_command('f',"Named flags...") {
      win_form("Named flags") {
        for(i=0;i<16;i++) win_text_restrict(0," ",namedflag[i].name);
        win_blank();
        win_command_esc(0,"Cancel") break;
      }
    }
    win_command('a',"Set random seed...") {
      *buf=0;
      ask_text("New random seed:",buf,32);
      if(*buf) rseed=strtoll(buf,0,0);
      if(!rseed) reseed(0);
    }
    win_command('C',"Call...") {
      Uint16 a=0;
      Uint32 w,x,y,z;
      win_form("Call") {
        win_numeric('A',"Address: ",a,0,0xFFFF);
        win_numeric('W',"W register: ",w,0,-1);
        win_numeric('X',"X register: ",x,0,-1);
        win_numeric('Y',"Y register: ",y,0,-1);
        win_numeric('Z',"Z register: ",z,0,-1);
        win_blank();
        win_command('E',"Execute") {
          snprintf(buf,80,"%lld",(long long)run_program(a,w,x,y,z));
          alert_text(buf);
          break;
        }
        win_command_esc(0,"Cancel") break;
      }
    }
    win_command('u',"Sound effect...") {
      *buf=0;
      ask_text("Sound effect:",buf,70);
      if(*buf) audio_set_sfx(buf);
    }
    win_command('i',"Video mode...") {
      *buf=0;
      ask_text("Video mode (hex):",buf,2);
      if(*buf) vm=strtol(buf,0,16);
      break;
    }
    win_command('d',"Make debug log...") {
      Uint8 a=0;
      Uint32 b=0;
      win_form("Debug log") {
        win_numeric('F',"First operand: ",a,0,7);
        win_numeric('S',"Second operand: ",b,0,0xFFFF);
        win_command_esc(0,"Done") break;
      }
      debug_log(a,b,0,0,0,0,0);
    }
    if(nspecopt) win_command('o',"Special option...") special_option_debug();
    win_command_esc(0,"Cancel") break;
  }
  repeating=0;
  v_mode=vm;
}

int run_game(void) {
  Uint8 ka=0;
  Sint8 kd=-1;
  Uint32 b,c,d,x,y;
  Sint32 a,z;
  Tile*t;
  reseed(0);
  soundon=(audio_get_volume()<0x10000?1:0);
  if(config.message_scrollback && config.message_scrollback<24) config.message_scrollback=24;
  warp_to_board(cur_board_id,1);
  if(config.pause) playstate=PLAYSTATE_PAUSED; else set_timer(config.speed);
  gameloop:
  while(a=memory[MEM_WARP_CALL]) {
    memory[MEM_WARP_CALL]=0;
    b=cur_board_id;
    warp_to_board(memory[MEM_WARP_TO],0);
    run_program(a,b,(memory[MEM_WARP_X_HI]<<16)|memory[MEM_WARP_X_LO],(memory[MEM_WARP_Y_HI]<<16)|memory[MEM_WARP_Y_LO],(memory[MEM_WARP_Z_HI]<<16)|memory[MEM_WARP_Z_LO]);
  }
  *v_status=playstate;
  if(!(cur_screen.flag&SF_NO_SCROLL) && !(memory[MEM_CONTROL]&CONTROL_NOSCROLL) && maxstat && stats->count) {
    if(b=memory[MEM_SCROLL_X_RATE]) {
      a=stats->xy->x; z=scroll_x;
      if(z<a-(Sint32)cur_screen.soft_edge[DIR_E]) z=a-cur_screen.soft_edge[DIR_E]; else if(z>a-(Sint32)cur_screen.soft_edge[DIR_W]) z=a-cur_screen.soft_edge[DIR_W];
      if(z<scroll_x-(Sint32)b) z=scroll_x-b; else if(z>scroll_x+(Sint32)b) z=scroll_x+b;
      if(z<-(Sint32)cur_screen.hard_edge[DIR_W]) z=-cur_screen.hard_edge[DIR_W]; else if(z>board_info.width-((Sint32)cur_screen.hard_edge[DIR_E])-1) z=board_info.width-cur_screen.hard_edge[DIR_E]-1;
      scroll_x=z;
    }
    if(b=memory[MEM_SCROLL_Y_RATE]) {
      a=stats->xy->y; z=scroll_y;
      if(z<a-(Sint32)cur_screen.soft_edge[DIR_S]) z=a-cur_screen.soft_edge[DIR_S]; else if(z>a-(Sint32)cur_screen.soft_edge[DIR_N]) z=a-cur_screen.soft_edge[DIR_N];
      if(z<scroll_y-(Sint32)b) z=scroll_y-b; else if(z>scroll_y+(Sint32)b) z=scroll_y+b;
      if(z<-(Sint32)cur_screen.hard_edge[DIR_N]) z=-cur_screen.hard_edge[DIR_N]; else if(z>board_info.height-((Sint32)cur_screen.hard_edge[DIR_S])-1) z=board_info.height-cur_screen.hard_edge[DIR_S]-1;
      scroll_y=z;
    }
  }
  display:
  update_screen();
  if(vtexttime) {
    display_message_text();
    if(!(memory[MEM_CONTROL]&CONTROL_VTEXT_FOREVER) && !--vtexttime) nvtextbuf=0;
  }
  redisplay();
  if(!repeating || playstate==PLAYSTATE_PAUSED) ka=0,kd=-1;
  for(;;) {
    if(!next_event()) errx(0,"No events available.");
    repeat_event:
    if(event.type==SDL_KEYDOWN) {
      if(event.key.keysym.unicode>0 && event.key.keysym.unicode<127 && !(event.key.keysym.mod&(KMOD_ALT|KMOD_META))) {
        a=event.key.keysym.unicode;
        if((a>=32 && a<127) || a==8 || a==9 || a==13) ka=a;
      } else {
        switch(event.key.keysym.sym) {
          case SDLK_UP: ka=(event.key.keysym.mod&KMOD_SHIFT)?30:24; kd=DIR_N; break;
          case SDLK_DOWN: ka=(event.key.keysym.mod&KMOD_SHIFT)?31:25; kd=DIR_S; break;
          case SDLK_LEFT: ka=(event.key.keysym.mod&KMOD_SHIFT)?17:27; kd=DIR_W; break;
          case SDLK_RIGHT: ka=(event.key.keysym.mod&KMOD_SHIFT)?16:26; kd=DIR_E; break;
          case SDLK_F1: k_f1:
            a=system_menu();
            resume:
            repeating=0; ka=0; kd=-1;
            *v_status=playstate;
            set_timer(playstate==PLAYSTATE_PAUSED?0:playstate==PLAYSTATE_NORMAL?config.speed:config.speed_fast);
            v_status[1]=0;
            update_screen();
            if(a) goto repeat_event;
            goto display;
          case SDLK_F2: k_f2:
            a=audio_get_volume();
            audio_set_volume(a&0xFFFF,(a>>16)^1);
            soundon=(audio_get_volume()<0x10000?1:0);
            audio_set_sfx("@0ZCX");
            break;
          case SDLK_F3: if(allow_saving() && ask_save_file(1)) save_state(); goto resume;
          case SDLK_F4: if(ask_save_file(0)) load_state(); goto resume;
          case SDLK_F5: k_f5: if(allow_saving()) save_state(); goto resume;
          case SDLK_F6: if(config.debug) debug_menu(); a=0; goto resume;
          case SDLK_F7: k_f7: load_state(); goto resume;
          case SDLK_F8: special_option_menu(); event.type=SDL_NOEVENT; goto resume;
          case SDLK_F9: k_f9: set_timer(0); v_status[1]=24; message_scrollback(); a=0; goto resume;
          case SDLK_F10: return 0;
          case SDLK_F12: k_f12:
            if(playstate==PLAYSTATE_NORMAL) playstate=PLAYSTATE_FAST; else playstate=PLAYSTATE_NORMAL;
            *v_status=playstate;
            set_timer(playstate==PLAYSTATE_FAST?config.speed_fast:config.speed);
            break;
          case SDLK_DELETE: k_del: vtexttime=nvtextbuf=*vtextbuf=0; update_screen(); break;
          case SDLK_INSERT: goto nextturn;
          case SDLK_PAUSE: k_pause:
            if(playstate==PLAYSTATE_PAUSED) playstate=PLAYSTATE_NORMAL; else playstate=PLAYSTATE_PAUSED;
            *v_status=playstate;
            set_timer(playstate==PLAYSTATE_PAUSED?0:config.speed);
            break;
        }
      }
      redisplay();
      if(ka && playstate==PLAYSTATE_PAUSED) goto nextturn;
    } else if(event.type==SDL_USEREVENT) {
      goto nextturn;
    } else if(event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP) {
      switch(b=do_joystick(JL_NORMAL)) {
        case 0x01 ... 0x7E:
          ka=b;
          if(b==16 || b==26) kd=DIR_E;
          if(b==30 || b==24) kd=DIR_N;
          if(b==17 || b==27) kd=DIR_W;
          if(b==31 || b==25) kd=DIR_S;
          if(autofire==ka) autofire_dir=kd;
          break;
        case '0'+0x100: goto k_pause;
        case '1'+0x100: goto k_f1;
        case '2'+0x100: goto k_f2;
        case '5'+0x100: goto k_f5;
        case '7'+0x100: goto k_f7;
        case '9'+0x100: goto k_f9;
        case 'A'+0x100: goto nextturn;
        case 'F'+0x100: saved_playstate=playstate; playstate=PLAYSTATE_NORMAL; goto k_f12;
        case 'F'+0x180: *v_status=playstate=saved_playstate; set_timer(playstate==PLAYSTATE_PAUSED?0:playstate==PLAYSTATE_FAST?config.speed_fast:config.speed); break;
        case 'T'+0x100: goto k_f12;
        case 'X'+0x100: goto k_del;
      }
#ifndef CONFIG_DISABLE_FRONT
    } else if(event.type==SDL_USEREVENT+1) {
      asn1_free(asn1reg);
      if(asn1_read_item(extern_in,asn1reg,0)) errx(1,"Error reading ASN.1 data from external program");
      if(b=run_program(memory[MEM_RECEIVED_DATA_EVENT],asn1reg->class,asn1reg->type,asn1reg->length,0)) {
        asn1_encode(extern_out,asn1reg+((b-1)&3));
        asn1_flush(extern_out);
      }
      unlock_front();
      goto display;
#endif
    }
  }
  nextturn:
  if(ka) {
    sendkey:
    if(memory[MEM_CONTROL]&CONTROL_NUMBER_KEY_DIRECTION) {
      if(ka=='2') kd=DIR_S; else if(ka=='4') kd=DIR_W; else if(ka=='6') kd=DIR_E; else if(ka=='8') kd=DIR_N;
    }
    run_program(memory[MEM_KEY_EVENT],ka,kd==DIR_E?1:kd==DIR_W?-1:0,kd==DIR_S?1:kd==DIR_N?-1:0,kd);
  } else if(autofire) {
    ka=autofire; kd=autofire_dir; goto sendkey;
  }
  while(b=memory[MEM_NEW_DYNAMIC_STAT_EVENT]) {
    memory[MEM_NEW_DYNAMIC_STAT_EVENT]=0;
    for(a=0;a<maxstat && ((STAT_DYNAMIC|STAT_VACANT)&~stats[a].mode);a++);
    if(a==255) {
      run_program(b,0,0,0,0);
    } else if(a<maxstat) {
      dynstat:
      free(stats[a].xy);
      stats[a].xy=0;
      stats[a].count=0;
      if(stats[a].text!=global_text) free(stats[a].text);
      stats[a].text=0;
      stats[a].frame=0;
      stats[a].misc1=stats[a].misc2=stats[a].misc3=0;
      stats[a].mode=STAT_DYNAMIC;
      if(b=run_program(b,a+1,0,0,0)) add_statxy(a+1)->layer=b;
    } else {
      stats=realloc(stats,(maxstat=a+1)*sizeof(Stat));
      if(!stats) err(1,"Allocation failed");
      memset(stats+maxstat-1,0,sizeof(Stat));
      goto dynstat;
    }
  }
  ++memory[MEM_FRAME_COUNTER];
  if(run_program(memory[MEM_FRAME_EVENT],0,0,0,0)) goto gameloop;
  for(a=y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++,a++) {
    t=b_main+a;
    if(b=elem_def[t->kind].event[EV_FRAME]) run_program(b,t->kind,x,y,t->param);
  }
  for(a=0;a<maxstat;a++) {
    if(stats[a].xy && (stats[a].mode&STAT_INDEPENDENT)) {
      for(b=0;b<stats[a].count;b++) {
        if(stats[a].xy[b].layer&0x20) continue;
        if(d=stats[a].xy[b].layer&3) {
          stats[a].xy[b].delay=0;
          if(!run_program(elem_def[d+240].event[EV_STAT],a+(b<<16)+1,stats[a].xy[b].x,stats[a].xy[b].y,0)) stats[a].xy[b].delay=stats[a].speed-1;
        } else {
          memmove(stats[a].xy+b,stats[a].xy+b+1,(--stats[a].count-b)*sizeof(StatXY));
          --b;
        }
      }
    } else if(stats[a].mode&STAT_VACANT) {
      if(stats[a].text && stats[a].text!=global_text) {
        free(stats[a].text);
        stats[a].text=0;
        stats[a].length=0;
        stats[a].frame=0;
      }
      if(stats[a].count) {
        free(stats[a].xy);
        stats[a].xy=0;
        stats[a].count=0;
      }
    } else if(stats[a].xy) {
      for(b=0;b<stats[a].count;b++) {
        if(stats[a].xy[b].layer&0x20) continue;
        if((d=stats[a].xy[b].layer&3) && stats[a].xy[b].x<board_info.width && stats[a].xy[b].y<board_info.height) {
          if(stats[a].speed && !stats[a].xy[b].delay--) {
            t=(d==1?b_under:d==2?b_main:b_over)+stats[a].xy[b].y*board_info.width+stats[a].xy[b].x;
            stats[a].xy[b].delay=0;
            if(!run_program(elem_def[d!=3?t->kind:240].event[EV_STAT],a+(b<<16)+1,stats[a].xy[b].x,stats[a].xy[b].y,t->param)) stats[a].xy[b].delay=stats[a].speed-1;
          }
        } else {
          // Delete this stat
          memmove(stats[a].xy+b,stats[a].xy+b+1,(--stats[a].count-b)*sizeof(StatXY));
          --b;
        }
      }
    }
  }
  goto gameloop;
}

