#if 0
gcc -g -O0 -c -std=gnu99 -Wno-unused-result -fwrapv game.c `sdl-config --cflags`
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

static uint64_t rseed;
static char soundon;

#define PLAYSTATE_NORMAL 16
#define PLAYSTATE_FAST 175
#define PLAYSTATE_PAUSED 186
static Uint8 playstate=PLAYSTATE_NORMAL;

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

static Sint32 run_program(Uint16 pc,Sint32 w,Sint32 x,Sint32 y,Sint32 z);

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
  }
  fputc('\n',f);
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
  FILE*fp=open_lump_by_number(b,"BRD","r");
  const char*e;
  if(fp) {
    e=load_board(fp);
    fclose(fp);
    return e;
  } else {
    return "Cannot open lump";
  }
}

static void warp_to_board(Uint16 b,char m) {
  FILE*fp;
  const char*e;
  Sint32 x,y;
  if((board_info.flag&BF_PERSIST) && !m) {
    for(x=0;x<maxstat;x++) {
      if(stats[x].xy) for(y=0;y<stats[x].count;y++) {
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
  if(cur_board_id!=b || !board_info.width || (!m && !(board_info.flag&BF_PERSIST))) {
    if(e=select_board(cur_board_id=b)) errx(1,"Error loading board #%d: %s",b,e);
  }
  if(m || cur_screen_id!=board_info.screen) {
    fp=open_lump_by_number(cur_screen_id=board_info.screen,"SCR","r");
    if(!fp) err(1,"Cannot open %04X.SCR",cur_screen_id);
    if(e=load_screen(fp)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
    fclose(fp);
  }
  // Handle scrolling
  if((cur_screen.flag&SF_NO_SCROLL) || !maxstat || !stats->count) {
    scroll_x=-cur_screen.hard_edge[DIR_W];
    scroll_y=-cur_screen.hard_edge[DIR_N];
  } else {
    x=stats->xy->x; y=stats->xy->y;
    scroll_x=x-cur_screen.view_x;
    scroll_y=y-cur_screen.view_y;
    if(scroll_x<-cur_screen.hard_edge[DIR_W]) scroll_x=cur_screen.hard_edge[DIR_W]; else if(scroll_x>board_info.width-cur_screen.hard_edge[DIR_E]) scroll_x=cur_screen.hard_edge[DIR_E];
    if(scroll_y<-cur_screen.hard_edge[DIR_N]) scroll_y=cur_screen.hard_edge[DIR_N]; else if(scroll_y>board_info.height-cur_screen.hard_edge[DIR_S]) scroll_y=cur_screen.hard_edge[DIR_S];
  }
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
  if(x>cur_screen.message_l) v_char[--x +y*80]=0;
  if(z<cur_screen.message_r) v_char[z++ +y*80]=0;
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
  Uint8 o=0;
  Uint8 z,m,d;
  ElementDef*e;
  Tile*t;
  if(bx>=0 && bx<board_info.width && by>=0 && by<board_info.height) {
    tile:
    t=b_main+xy;
    e=elem_def+t->kind;
    if(!(h&4) && !(e->attrib&A_LIGHT) && (b_over[xy].kind&OVER_VISIBLE) && (board_info.flag&BF_OVERLAY)) {
      //TODO: light shape
      o=b_over[xy].kind;
    }
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

void update_screen(void) {
  int i;
  Uint32 v,x,y;
  Uint8 cmd,col,chr;
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
          case SC_IND_CURSOR: v_char[i]=(stats->count && (x+scroll_x==stats->xy->x || y+scroll_y==stats->xy->y))?chr:0; break;
          case SC_IND_SCROLL: 
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
  s->xy=realloc(s->xy,++s->count*sizeof(StatXY));
  if(!s->xy) errx(1,"Allocation failed");
  r=s->xy+s->count-1;
  r->x=r->y=r->instptr=0;
  r->layer=r->delay=0;
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
    if(r->x==x && r->y==y && (r->layer&3)==z) return r;
  }
  return 0;
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
  if(a>=65488) return;
  memset(memory+a,0,49);
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
      for(i=0;i<49;i++) if((j=(24-i)*(24-i)-r)>0) {
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
  int i;
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
    case 6: // Global
      if(n>0 && n<ngtext) s=(char*)gtext[n];
      break;
    case 7: // Hexadecimal
      snprintf(buf,80,"%08lX",(unsigned long)n);
      break;
  }
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
    case XOP_S_SCREEN_ID: return cur_screen_id;
    case XOP_S_SCROLL_X: return scroll_x+cur_screen.view_x+so+(ex&15)-8;
    case XOP_S_SCROLL_Y: return scroll_y+cur_screen.view_y+so+(ex&15)-8;
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
  for(n=0;n<s->count;n++) if(s->xy[n].x==x && s->xy[n].y==y && (s->xy[n].layer&3)==lay) return ti->stat+(n<<16);
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
      if((u->layer&3)==lay && u->x==x && u->y==y) {
        r=*u;
        kill_stat(os,i);
        break;
      }
    }
  }
  if(os=(lay==1?b_under:lay==2?b_main:b_over)[y*board_info.width+x].stat) {
    if(os>maxstat) return;
    if(f&0x8000) r.delay=(f>>8)&0x7F;
    r.layer=lay|(r.layer&0xC0&~f);
    *add_statxy(os)=r;
  }
}

static Sint32 do_change(Uint8 how,Uint8 b,Uint32 a) {
  Sint32 n=0;
  Uint32 z;
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
  z=board_info.width*board_info.height;
  switch(f&7) {
    case 0: return 0;
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
    }
    case 4: for(;a<z;a++) {
      if(((elem_def[b_under[a].kind].attrib>>24)|mm.kind)!=m.kind) goto skip4;
      for(i=1;i<4;i++) if((b_under[a].values[i]|mm.values[i])!=m.values[i]) goto skip4;
      j=b_under[a].stat;
      for(i=0;i<4;i++) b_under[a].values[i]=r.values[i]^(b_under[a].values[i]&rm.values[i]);
      if(j || r.stat) do_change_stat(f,j,a/(board_info.width*board_info.height)+1,a%board_info.width,(a/board_info.width)%board_info.height);
      skip4: ;
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
    if(sn>maxstat || !sn || sr>=stats[sn-1].count) return 0;
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
    e0=(A_PUSH_NS|A_PUSH_EW);
    e1=(b[to].kind&OVER_SOLID)?(A_PUSH_NS|A_PUSH_EW):(A_FLOOR);
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
  // Transporting
  if((flag&0x40) && (e0&A_TRANSPORTABLE) && (e1&A_TRANSPORTER)) {
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
    if(!(flag&2) && (i=elem_def[b[to].kind].event[EV_PUSH])) {
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
    }
    b[to]=b[at];
    if(flag&2) {
      b[to].stat|=b[at].stat;
      b[at].kind=(b[at].kind&OVER_BG_THRU)|(memory[MEM_DEFAULT_OVERLAY]?OVER_VISIBLE:0);
      b[at].color=memory[MEM_DEFAULT_OVERLAY]>>8;
      b[at].param=memory[MEM_DEFAULT_OVERLAY];
      b[at].stat=0;
    } else {
      if(b_under[at].stat && (q=find_statxy(b_under+at))) q->layer++;
      b_main[at]=b_under[at];
      b_under[at]=(Tile){};
    }
    if(qq) {
      qq->x=tx; qq->y=ty;
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
    lay=q->layer&3;
    if(!lay) return;
    at=convxy(0,q->x,q->y);
    if(at==-1) return;
  }
  if(lay==1) {
    t=b_under+at;
    if(!sn && (sn=t->stat)) q=find_statxy(t);
    if(!f) *t=(Tile){};
  } else if(lay==2) {
    t=b_main+at;
    if(!sn && (sn=t->stat)) q=find_statxy(t);
    if(!f) {
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
    q->x=q->y=q->instptr=65535;
    q->layer=128;
    q->delay=255;
  }
}

static int match_label(const char*v,const char*label) {
  int n=0;
  char a,b;
  for(;;) {
    a=v[n+1];
    b=label[n];
    if(!b || b=='\n' || b==' ' || b=='\r' || b==';' || b=='(' || b==')') {
      if(!a || a=='\n' || a==' ' || a=='\r' || a==';' || a=='(' || a==')') {
        n++;
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

static Sint32 find_zapped_label(Stat*s,const char*label) {
  const char*v=(char*)s->text;
  int n;
  if(*v!='\'') v=strstr(v,"\n'"),v+=(v?1:0);
  while(v) {
    if(n=match_label(v,label)) return (v-(const char*)s->text)+n;
    v=strstr(v,"\n'"),v+=(v?1:0);
  }
  return -1;
}

static void send_message(Uint32 n,const char*label,Uint8 ignlock) {
  const char*p;
  const char*q=strchr(label,':');
  StatXY*r;
  Stat*s;
  Sint32 f;
  int m;
  if(q || !n) {
    if(q) p=label,label=q+1; else p=0;
    for(n=0;n<maxstat;n++) if(stats[n].length) {
      if(p && !match_name(s->text,p)) continue;
      f=find_label(s=stats+n,label);
      if(f!=-1) {
        for(m=0;m<s->count;m++) if(!(s->xy[m].layer&0x80)) s->xy[m].instptr=f;
      }
    }
  } else if(r=get_statxy(n)) {
    if(*label=='*') ++label; else if((r->layer&0x80) && !ignlock) return;
    f=find_label(stats+(n&0xFF)-1,label);
    if(f!=-1) r->instptr=f;
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
          case SC_SPEC_CAMERA_X: v=scroll_x-cur_screen.view_x; break;
          case SC_SPEC_CAMERA_Y: v=scroll_y-cur_screen.view_y; break;
          case SC_SPEC_TEXT_SCROLL_PERCENT: v=(100L*(tcursor+(wind->flag&WF_ZERO_BASED?0:1)))/tnlines; break;
          case SC_SPEC_TEXT_LINE_NUMBER: v=tcursor+(wind->flag&WF_ZERO_BASED?0:1); break;
          case SC_SPEC_TEXT_LINE_COUNT: v=tnlines; break;
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
          case SC_IND_CURSOR: v_char[i]=(cur_screen.flag&SF_NO_SCROLL?(y-top-tscroll==tcursor):(y==mid))?chr:0; break;
          case SC_IND_SCROLL: v_char[i]=(y>cur_screen.view_y?(tscroll>mid-top):(tscroll<tnlines+mid-bot))
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
                  v_color[i]=(col&0xF0)|(cmd&0x0F);
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
            v_color[i]=(col&0xF0)|(cmd&0x0F);
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
            }
            v_char[i]=textfile_text[y*TEXTREC+x+1];
          }
        }
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
    set_timer(0);
    if((a=xyn&0xFFFF) && a<=maxstat && stats[a-1].length && stats[a-1].text[0]=='@') {
      for(b=0;b<80 && b<stats[a-1].length;b++) {
        textbuf[b]=c=stats[a-1].text[b+1];
        if(c=='=' || c=='\n' || !c) break;
      }
      textbuf[ntextbuf=b]=0;
    } else {
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
    open:
    for(;;) {
      update_text_window(&wind);
      redisplay();
      if(!next_event()) errx(0,"No events available.");
      if(event.type!=SDL_KEYDOWN) continue;
      switch(event.key.keysym.sym) {
        case SDLK_ESCAPE: goto close;
        case SDLK_END: case SDLK_KP1: tcursor=tnlines-1; break;
        case SDLK_DOWN: case SDLK_KP2: if(tcursor!=tnlines-1) ++tcursor; break;
        case SDLK_PAGEDOWN: case SDLK_KP3: if(!(cur_screen.flag&SF_NO_SCROLL)) tcursor=(tcursor+scl>=tnlines?tnlines-1:tcursor+scl); break;
        case SDLK_HOME: case SDLK_KP7: tcursor=0; break;
        case SDLK_UP: case SDLK_KP8: if(tcursor) --tcursor; break;
        case SDLK_PAGEUP: case SDLK_KP9: if(!(cur_screen.flag&SF_NO_SCROLL)) tcursor=(tcursor-scl>=0?tcursor-scl:0); break;
        case SDLK_TAB:
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
        case SDLK_F11:
          lpt_document() {
            if(ntextbuf) lpt_title(textbuf,ntextbuf);
            for(a=0;a<tnlines;a++) lpt_script(textfile_text+a*TEXTREC+1,textfile_text[a*TEXTREC]);
          }
          break;
        default:
          c=event.key.keysym.unicode;
          if(c>32 && c<127) {
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
    set_timer(playstate==PLAYSTATE_FAST?config.speed_fast:playstate==PLAYSTATE_NORMAL?config.speed:0);
    fp=open_lump_by_number(cur_screen_id=board_info.screen,"SCR","r");
    if(!fp || load_screen(fp)) errx(1,"Error restoring screen");
    fclose(fp);
  }
  free(textfile_text);
  textfile=0;
  textfile_text=0;
  textfile_size=0;
  return r;
}

static char script_go(Uint16 m,Uint16 n,Stat*s,StatXY*xy,Uint8 dir) {
  switch(dir) {
    case 'i': case 'I': return 1;
    case 'e': case 'E': dir=DIR_E; break;
    case 'w': case 'W': dir=DIR_W; break;
    case 'n': case 'N': dir=DIR_N; break;
    case 's': case 'S': dir=DIR_S; break;
    default: script_error(m,xy,"Improper direction"); return 2;
  }
  general_move(0,(n<<16)+m,xy->x,xy->y,0x0814,0,dir,dir);
  return condflag;
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
  *ip+=n;
  switch(*buf) {
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
    case 'I':
      if(n==1 || (n==4 && !memcmp(buf+1,"DLE",3))) return (condflag=1),-1;
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
      if(n==1 || (n==4 && !memcmp(buf+1,"AST",3))) return (condflag=1),((adj+2)&3);
      goto bad;
    default: bad: script_error(s+1-stats,xy,"Invalid direction"); return -1;
  }
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
    for(n=0;n<256;n++) if(elem_def[n].name[0]) break;
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
  if(lay==2) {
    // Main
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

static inline char check_blocked_at(Uint32 x,Uint32 y) {
  if(x>=board_info.width || y>=board_info.height) return 1;
  if(elem_def[b_main[y*board_info.width+x].kind].attrib&A_FLOOR) return 0;
  return 1;
}

static char parse_condition(Stat*s,StatXY*xy,Uint16*ip) {
  ScriptKind sk;
  Uint16 bip;
  char buf[128];
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
    if(c<=32) break;
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
    } else if(!strcmp(buf+1,"FULL")) {
      for(v=1,n=0;n<16 && v;n++) if(!namedflag[n].name[0]) v=0;
    } else if(!strcmp(buf+1,"LOCKED")) {
      if(xy->layer&0x80) v=1;
    } else if(!strncmp(buf+1,"MAIN:",5)) {
      n=6; main: *ip=bip+n;
      if(count_script_kind(2,&sk)) v=1;
    } else if(!strcmp(buf+1,"OVERLAY")) {
      if(board_info.flag&BF_OVERLAY) v=1;
    } else if(!strcmp(buf+1,"PERSIST")) {
      if(board_info.flag&BF_PERSIST) v=1;
    } else if(!strncmp(buf+1,"PLAYER:",7)) {
      n=8; player: *ip=bip+n;
      if(!parse_kind(s,xy,ip,&sk,0)) goto bad;
      for(z=0;z<stats->count && !v;z++)
       if((stats->xy[z].layer&3)==2 && stats->xy[z].x<board_info.width && stats->xy[z].y<board_info.height && match_script_kind(stats->xy[z].y*board_info.width+stats->xy[z].x,1,&sk)) v=1;
    } else if(!strcmp(buf+1,"RANDOM")) {
      v=dice(2);
    } else if(!strncmp(buf+1,"UNDER:",6)) {
      n=7; under: *ip=bip+n;
      if(count_script_kind(1,&sk)) v=1;
    } else if(!strcmp(buf+1,"USER")) {
      if(xy->layer&0x40) v=1;
    } else {
      bad: script_error(s+1-stats,xy,"Improper condition");
    }
  } else if(*buf=='@') {
    n=1; goto any;
  } else if(buf[1]=='@') {
    n=3;
    if(*buf=='B') goto beneath;
    if(*buf=='M') goto main;
    if(*buf=='P') goto player;
    if(*buf=='U') goto under;
    goto bad;
  } else if(*buf>='A' && *buf<='Z' && buf[1]!='@' && !buf[15]) {
    for(n=0;n<16 && !v;n++) if(!strcmp(namedflag[n].name,buf)) v=1;
  } else if((*buf>='0' && *buf<='9') || *buf=='$' || *buf=='-' || *buf=='+' || *buf=='(') {
    *ip=bip;
    z0=parse_number(s,xy,ip);
    if(!condflag) goto bad;
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
  } else {
    goto bad;
  }
  return v^inv;
}

static void script_set_flag(Stat*s,StatXY*xy,Uint16*ip,char v) {
  char buf[128]={};
  int c,n;
  while(s->text[*ip]==' ') ++*ip;
  if(s->text[*ip]=='!') v^=1,++*ip;
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
    if(!strcmp(buf+1,"LOCKED")) {
      if(v) xy->layer|=0x80; else xy->layer&=0x7F;
    } else if(!strcmp(buf+1,"OVERLAY")) {
      if(v) board_info.flag|=BF_OVERLAY; else board_info.flag&=~BF_OVERLAY;
    } else if(!strcmp(buf+1,"PERSIST")) {
      if(v) board_info.flag|=BF_PERSIST; else board_info.flag&=~BF_PERSIST;
    } else if(!strcmp(buf+1,"USER")) {
      if(v) xy->layer|=0x40; else xy->layer&=0xBF;
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
  } else {
    goto bad;
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
          if(memory[w+1]<ngtext || strcmp(buf,gtext[memory[w+1]])) {
            w=memory[w];
          } else {
            xy->instptr=ip;
            v=run_program(w+2,n,xy->x,xy->y,s->speed);
            if(v&1) stop=1;
            if(v&4) xy->instptr=ip=65535;
            if(v&8) return;
            if(v&2) {
              u=-1;
              ip=xy->instptr;
              goto begin;
            }
            goto skip;
          }
        }
        switch(*buf) {
          case 'B':
            if(!strcmp(buf,"BECOME")) {
              become:
              if(!parse_kind(s,xy,&ip,&sk,1)) {script_error(m,xy,"Improper #BECOME"); return;}
              change_to_script_kind(xy->x,xy->y,xy->layer,&sk);
              ip=65535; goto stop;
            } else if(!strcmp(buf,"BIND")) {
              for(i=0;i<maxstat;i++) if(stats[i].length && match_name(stats[i].text,s->text+ip)) {
                if(xy->x<board_info.width && xy->y<board_info.height && (j=xy->layer&3)) {
                  xy2=add_statxy(i);
                  *xy2=*xy;
                  xy2->instptr=0;
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
            } else goto badcommand; break;
          case 'E':
            if(!strcmp(buf,"END")) {
              ip=65535; stop=1;
            } else if(!strcmp(buf,"ERASE")) {
              if(!parse_kind(s,xy,&ip,&sk,0)) {script_error(m,xy,"Improper #ERASE"); return;}
              script_do_erase(&sk);
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
            } else goto badcommand; break;
          case 'G':
            if(!strcmp(buf,"GIVE")) {
              i=parse_letter(s,xy,&ip);
              if(condflag) {
                u=parse_number(s,xy,&ip);
                if(0<=(Sint32)(status_vars[i]+u)) status_vars[i]+=u;
              }
            } else if(!strcmp(buf,"GO")) {
              i=parse_direction(s,xy,&ip);
              if(i!=-1 && condflag) {
                general_move(0,(n<<16)+m,xy->x,xy->y,0x0814,0,i,i);
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
                u=-1; goto begin;
              }
            } else goto badcommand; break;
          case 'L':
            if(!strcmp(buf,"LOCK")) {
              xy->layer|=0x80;
            } else goto badcommand; break;
          case 'M':
            if(!buf[5] && !memcmp(buf,"MISC",4) && buf[4]>='1' && buf[4]<='3') {
              w=parse_number(s,xy,&ip);
              if(buf[4]=='1') s->misc1=w;
              if(buf[4]=='2') s->misc2=w;
              if(buf[4]=='3') s->misc3=w;
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
              }
            } else if(!strcmp(buf,"PUTAT")) {
              i=parse_number(s,xy,&ip);
              j=parse_number(s,xy,&ip);
              if(parse_kind(s,xy,&ip,&sk,1)) put_script_kind(i,j,&sk);
            } else if(!strcmp(buf,"PUTBELOW")) {
              if(xy->layer&2) {
                if(!parse_kind(s,xy,&ip,&sk,1)) {script_error(m,xy,"Improper #PUTBELOW"); return;}
                change_to_script_kind(xy->x,xy->y,1,&sk);
              }
            } else goto badcommand; break;
          case 'R':
            if(!strcmp(buf,"RESTART")) {
              ip=0; u=0; goto begin;
            } else if(!strcmp(buf,"RESTORE")) {
              while(s->text[ip]==' ') ip++;
              while((u=find_zapped_label(s,s->text+ip))!=-1) s->text[u]=':';
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
            } else if(!strcmp(buf,"TRY")) {
              i=parse_direction(s,xy,&ip);
              if(i!=-1 && condflag) {
                general_move(0,(n<<16)+m,xy->x,xy->y,0x0814,0,i,i);
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
          case 'Z':
            if(!strcmp(buf,"ZAP")) {
              while(s->text[ip]==' ') ip++;
              while((u=find_label(s,s->text+ip))!=-1) s->text[u]='\'';
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
    case '\'': case ':': case '@':
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
        for(v=0;v<80;) {
          c=buf[v]=s->text[ip];
          if(c=='\n' || !c) break;
          ip++;
          if(c==0x7B) {
            //TODO
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
  if(textfile && show_text_window((n<<16)+m,0) && (u=find_label(s,textbuf))>=0) {
    selection: ip=u; u=0; goto begin;
  }
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
      case OP_ASUB: regs[fo]=labs(regs[fo]-so); break;
      case OP_BACK: so^=2; goto forw;
      case OP_BFLG: condflag=(board_info.flag>>fo)&1; if(so>0) board_info.flag|=1<<fo; else if(so<0) board_info.flag&=~(1<<fo); break;
      case OP_BGIV: condflag=(status_vars[fo]&(1<<(so&31))?0:1); status_vars[fo]|=1<<(so&31); break;
      case OP_BIT: so=1<<(so&31); goto store;
      case OP_BLOC: switch(fo) {
        case 0 ... 7: regs[fo]=so; return pc;
        case 9: condflag=so?1:0; return pc;
        case 12: pc=run_program(pc,so,x,y,z); if(pc<256) return pc;
        case 13: pc=run_program(pc,w,so,y,z); if(pc<256) return pc;
        case 14: pc=run_program(pc,w,x,so,z); if(pc<256) return pc;
        case 15: pc=run_program(pc,w,x,y,so); if(pc<256) return pc;
      } break;
      case OP_BTAK: condflag=(status_vars[fo]&(1<<(so&31))?1:0); status_vars[fo]&=~(1<<(so&31)); break;
      case OP_BTST: condflag=((1L<<(so&31))&regs[fo])?1:0; break;
      case OP_CALL: so=run_program(so,w,x,y,z); goto store;
      case OP_CALM: if((t=convxy(so,x,y))!=-1) w=run_program(elem_def[b_main[t].kind].event[fo],w,t%board_info.width,t/board_info.width,z); break;
      case OP_CALU: if((t=convxy(so,x,y))!=-1) w=run_program(elem_def[b_under[t].kind].event[fo],w,t%board_info.width,t/board_info.width,z); break;
      case OP_CASE: so=memory[(so+regs[fo])&0xFFFF]; goto jump;
      case OP_CBC: memory[so&0xFFFF]&=~(1<<fo); break;
      case OP_CBS: memory[so&0xFFFF]|=(1<<fo); break;
      case OP_CBT: condflag=(memory[so&0xFFFF]&(1<<fo)?1:0); break;
      case OP_CHA: do_change(1,regs[fo],so); break;
      case OP_CHAX: do_change(2,regs[fo],so); break;
      case OP_CLAM: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=elem_def[b_main[t].kind].attrib&15; else condflag=0; break;
      case OP_CLAU: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=elem_def[b_under[t].kind].attrib&15; else condflag=0; break;
      case OP_CORU:
        if(so>=256) memory[MEM_COROUTINE_U_PC]=so;
        memory[MEM_COROUTINE_U_W_HI]=w>>16; memory[MEM_COROUTINE_U_W_LO]=w;
        memory[MEM_COROUTINE_U_X_HI]=x>>16; memory[MEM_COROUTINE_U_X_LO]=x;
        memory[MEM_COROUTINE_U_Y_HI]=y>>16; memory[MEM_COROUTINE_U_Y_LO]=y;
        memory[MEM_COROUTINE_U_Z_HI]=z>>16; memory[MEM_COROUTINE_U_Z_LO]=z;
        so=(memory[MEM_COROUTINE_U_W_HI]<<16)|memory[MEM_COROUTINE_U_W_LO];
        goto store;
      case OP_CORV:
        if(so>=256) memory[MEM_COROUTINE_V_PC]=so;
        memory[MEM_COROUTINE_V_W_HI]=w>>16; memory[MEM_COROUTINE_V_W_LO]=w;
        memory[MEM_COROUTINE_V_X_HI]=x>>16; memory[MEM_COROUTINE_V_X_LO]=x;
        memory[MEM_COROUTINE_V_Y_HI]=y>>16; memory[MEM_COROUTINE_V_Y_LO]=y;
        memory[MEM_COROUTINE_V_Z_HI]=z>>16; memory[MEM_COROUTINE_V_Z_LO]=z;
        so=(memory[MEM_COROUTINE_V_W_HI]<<16)|memory[MEM_COROUTINE_V_W_LO];
        goto store;
      case OP_COUN: regs[fo]=do_change(0,regs[fo],so); break;
      case OP_CWOE: t=regs[fo]&0xFF; cwoe: t=(elem_def[t].attrib); if(t&A_FLOOR) condflag=((1<<(t&15))&so)?1:0; else condflag=0; break;
      case OP_CWOT: if((t=convxy(regs[fo],x,y))!=-1) { t=b_main[t].kind; goto cwoe; } else condflag=0; break;
      case OP_DCL: condflag=(--regs[fo]<so?1:0); break;
      case OP_DEC: --so; goto store;
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
      case OP_DROP:
        condflag=0;
        if(rs=get_statxy(so)) {
          if(x<0 || x>board_info.width || y<0 || y>board_info.height || (rs->layer&3)!=2) break;
          rs->x=x; rs->y=y; rs->layer=(rs->layer&~3)+2;
          u=x+y*board_info.width;
          if(b_under[u].stat) break;
          b_under[u]=b_main[u];
          if(b_main[u].stat) if(rs=find_statxy(b_main+u)) rs->layer--;
          b_main[u].stat=so;
          b_main[u].kind=regs[fo];
          b_main[u].color=regs[fo]>>8;
          b_main[u].param=regs[fo]>>16;
          condflag=1;
        }
        break;
      case OP_EAP0: so=elem_def[so&255].app[0]; goto store;
      case OP_EAP1: so=elem_def[so&255].app[1]; goto store;
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
      case OP_GBF: regs[fo]=board_info.flag&~so; break;
      case OP_GBU: regs[fo]=board_info.userdata&~so; break;
      case OP_GCOU: so&=0xFFFF; so=(so<1?-1:so>maxstat?0:stats[so-1].count); goto store;
      case OP_GIP: if(rs=get_statxy(so)) regs[fo]=rs->instptr; break;
      case OP_GIVE: status_vars[fo]+=so; break;
      case OP_GM1: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc1); goto store;
      case OP_GM2: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc2); goto store;
      case OP_GM3: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc3); goto store;
      case OP_GMOV: if(so>0xFFFC) break; so=general_move(0,regs[fo],x,y,memory[so],memory[so+1],memory[so+2],memory[so+3]); goto setxy;
      case OP_GO: t=so; so=pc; pc=t; goto store;
      case OP_GOTO: goto jump;
      case OP_GPUS: if(so>0xFFFC) break; general_move(1,regs[fo],x,y,memory[so],memory[so+1],memory[so+2],memory[so+3]); break;
      case OP_GRTR: condflag=(regs[fo]>so?1:0); break;
      case OP_GSD: if(rs=get_statxy(so)) regs[fo]=rs->delay; break;
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
      case OP_ICG: condflag=(++regs[fo]>so?1:0); break;
      case OP_INC: ++so; goto store;
      case OP_JEV: if(!(regs[fo]&1)) goto jump; break;
      case OP_JF: if(!condflag) goto jump; break;
      case OP_JNEG: if(regs[fo]<0) goto jump; break;
      case OP_JNZ: if(regs[fo]) goto jump; break;
      case OP_JOD: if(regs[fo]&1) goto jump; break;
      case OP_JPOS: if(regs[fo]>=0) goto jump; break;
      case OP_JT: if(condflag) goto jump; break;
      case OP_JZ: if(!regs[fo]) goto jump; break;
      case OP_KILM: if((t=convxy(so,x,y))!=-1) break_tile(t,2,0,0,fo&4); goto died;
      case OP_KILO: if((t=convxy(so,x,y))!=-1) break_tile(t,3,0,0,fo&4); goto died;
      case OP_KILU: if((t=convxy(so,x,y))!=-1) break_tile(t,1,0,0,fo&4); goto died;
      case OP_LAY: if(rs=get_statxy(so)) { condflag=1; so=rs->layer; goto store; } else condflag=0; break;
      case OP_LESS: condflag=(regs[fo]<so?1:0); break;
      case OP_LET: goto store;
      case OP_LITE: calc_light(fo,so); break;
      case OP_LOCK: if(rs=get_statxy(so)) rs->layer=(rs->layer&0x3F)|(regs[fo]&0xC0); break;
      case OP_LOG: if(config.debug) debug_log(fo,so,w,x,y,z,pc); break;
      case OP_LOOP: if(!regs[fo]) break; --regs[fo]; goto jump;
      case OP_LSH: regs[fo]=(so&~31?0:regs[fo]<<so); break;
      case OP_MAX: if(so>regs[fo]) regs[fo]=so; break;
      case OP_MESS: do_text_op(fo,so); memcpy(vtextbuf,textbuf,nvtextbuf=ntextbuf); vtextbuf[nvtextbuf]=0; if(vtexttime=(nvtextbuf?config.message_timer:0)) add_message_text(); break;
      case OP_MIN: if(so<regs[fo]) regs[fo]=so; break;
      case OP_MNEW:
        so=convxy(so,x,y);
        if(so==-1 || !b_main[so].stat || b_main[so].stat>maxstat) break;
        rs=add_statxy(b_main[so].stat);
        rs->layer=2;
        rs->x=so%board_info.width;
        rs->y=so/board_info.width;
        so=((rs-stats[b_main[so].stat].xy)<<16)|b_main[so].stat;
        goto store;
      case OP_MOD: if(so) condflag=1,regs[fo]%=so; else condflag=0; break;
      case OP_MOVE: so=general_move(0,regs[fo],x,y,(so&0xF8)+0x8800+(so&7)*0x1100,(so&0xFF00)+1,0,0); goto setxy;
      case OP_MTIL: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=pack_tile(b_main+t); else condflag=0; break;
      case OP_MUL: regs[fo]*=so; break;
      case OP_NEG: so=-so; goto store;
      case OP_NOT: so=~so; goto store;
      case OP_OMOV: so=general_move(0,regs[fo],x,y,0x8802+(so&7)*0x1100,0xFFFF,0,0); goto setxy;
      case OP_ONEW:
        so=convxy(so,x,y);
        if(so==-1 || !b_over[so].stat || b_over[so].stat>maxstat) break;
        rs=add_statxy(b_over[so].stat);
        rs->layer=3;
        rs->x=so%board_info.width;
        rs->y=so/board_info.width;
        so=((rs-stats[b_over[so].stat].xy)<<16)|b_over[so].stat;
        goto store;
      case OP_OR: regs[fo]|=so; break;
      case OP_PACK:
        t=convxy(0,x,y);
        if(t!=-1) {
          condflag=1;
          so=t+1;
        } else {
          condflag=0;
        }
        goto store;
      case OP_PBF: board_info.flag=so; break;
      case OP_PBU: board_info.userdata=so; break;
      case OP_PEEK: regs[fo]=memory[so&0xFFFF]; break;
      case OP_PEER: regs[fo]=memory[(so+regs[fo])&0xFFFF]; break;
      case OP_PICK:
        condflag=0;
        if(rs=get_statxy(so)) {
          x=rs->x; y=rs->y;
          if(x<0 || x>board_info.width || y<0 || y>board_info.height || (rs->layer&3)!=2) break;
          u=x+y*board_info.width;
          regs[fo]=pack_tile(b_main+u);
          b_main[u]=b_under[u];
          if(b_main[u].stat) if(rs=find_statxy(b_under+u)) rs->layer++;
          b_under[u]=(Tile){};
          condflag=1;
        }
        break;
      case OP_PIP: if(rs=get_statxy(so)) rs->instptr=regs[fo]; break;
      case OP_PM1: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc1=regs[fo];
      case OP_PM2: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc2=regs[fo];
      case OP_PM3: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc3=regs[fo];
      case OP_POKE: memory[so&0xFFFF]=regs[fo]; break;
      case OP_PSD: if(rs=get_statxy(so)) rs->delay=regs[fo]; break;
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
      case OP_PTUC: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].color=regs[fo]; else condflag=0; break;
      case OP_PTUK: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].kind=regs[fo]; else condflag=0; break;
      case OP_PTUP: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].param=regs[fo]; else condflag=0; break;
      case OP_PTUS: if((t=convxy(so,x,y))!=-1) condflag=1,b_under[t].stat=regs[fo]; else condflag=0; break;
      case OP_PUSH: general_move(1,regs[fo],x,y,(so&0xF8)+0x8800+(so&7)*0x1100,(so&0xFF00)+1,0,0); break;
      case OP_REGL: load_registers(fo,so); break;
      case OP_REGS: save_registers(fo,so); break;
      case OP_REVB: revert_lump_by_number(so,"BRD"); break;
      case OP_ROB: if(status_vars[fo]>so) status_vars[fo]-=so,condflag=1; else status_vars[fo]=0,condflag=0; break;
      case OP_RSH: regs[fo]=(so&~31?(regs[fo]<0?-1:0):regs[fo]>>so); break;
      case OP_RSUB: regs[fo]=so-regs[fo]; break;
      case OP_RUN: run_script(regs[fo]&0xFFFF,(regs[fo]>>16)&0xFFFF,so); break;
      case OP_SCAN: so=scan_board(regs[fo],so&0xFF,x,y); if(!so) break; if(regs[fo]) regs[fo]=so; else goto unpack0; break;
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
        if(t!=-1 && !b_under[t].stat) {
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
      case OP_SIU: so=statxy_index_at(convxy(so,x,y),1,b_under); condflag=(so?1:0); goto store;
      case OP_SIXY: if((rs=get_statxy(so)) && (so=convxy(0,rs->x,rs->y)+1)) condflag=1; else condflag=so=0; goto store;
      case OP_SMOV: general_move(0,regs[fo],x,y,(so&0xF8)+0x8804+(so&7)*0x1100,(so&0xFF00)+1,0,0); break;
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
          if((rs->layer&3)!=2 || rs->x>=board_info.width || rs->y>=board_info.height) break;
          t=convxy(so,x,y);
          if(t==-1 || !(elem_def[b_main[t].kind].attrib&A_FLOOR)) break;
          u=rs->x+rs->y*board_info.width;
          if(!b_main[u].stat) break;
          if(t==u) {
            condflag=1;
            break;
          }
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
        so=((rs-stats[b_under[so].stat].xy)<<16)|b_under[so].stat;
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
      case OP_ZEX: so=(Uint16)so; goto store;
      default: errx(1,"Unimplemented opcode $%X at $%X",op&0x1FF,pc-1);
    }
    continue;
    store:
    switch(fo) {
      case 0 ... 7: regs[fo]=so; break;
      case 8: return so; break;
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
    setxy:
    if(regs[fo]) regs[fo]=so; else so--,x=so%board_info.width,y=so/board_info.width;
    continue;
    jump:
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
  char buf[16];
  set_timer(0);
  v_status[1]='F';
  redraw0:
  config.menu_x=x; config.menu_y=y;
  update_screen();
  if(vtexttime) display_message_text();
  draw_border(0x19,x,y,x+40,y+16);
  draw_text(x+12,y," Super ZZ Zero ",0x1B,-1);
  // 012345678901234567890123456789012345
  // =F1== Menu         =F7== Q. Restore
  // =F2== Sound: ___   =F8== 
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
  draw_text(x+2,y+3," F3  ",0x30,-1); draw_text(x+8,y+3,"Save",0x1F,-1);
  draw_text(x+2,y+4," F4  ",0x70,-1); draw_text(x+8,y+4,"Restore",0x1F,-1);
  draw_text(x+2,y+5," F5  ",0x30,-1); draw_text(x+8,y+5,"Q. Save",0x1F,-1);
  draw_text(x+2,y+6," F6  ",0x70,-1); if(config.debug) draw_text(x+8,y+6,"Debug",0x1F,-1);
  draw_text(x+21,y+1," F7  ",0x30,-1); draw_text(x+27,y+1,"Q. Restore",0x1F,-1);
  draw_text(x+21,y+2," F8  ",0x70,-1);
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
  if(playstate==PLAYSTATE_PAUSED) draw_text(x+21,y+10," ----",0x1A,5);
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
  do { if(!next_event()) errx(0,"No events available."); } while(event.type!=SDL_KEYDOWN);
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: case SDLK_RETURN: return;
    case SDLK_END: case SDLK_KP1: n=(nscrback+config.message_scrollback-24)%config.message_scrollback; break;
    case SDLK_UP: case SDLK_KP8: n=(n+config.message_scrollback-1)%config.message_scrollback; break;
    case SDLK_DOWN: case SDLK_KP2: n=(n+1)%config.message_scrollback; break;
    case SDLK_PAGEUP: case SDLK_KP9: n=(n+config.message_scrollback-24)%config.message_scrollback; break;
    case SDLK_PAGEDOWN: case SDLK_KP3: n=(n+24)%config.message_scrollback; break;
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
  draw_text(1,y,buf,7,snprintf(buf,80,"%5u: X=%05d Y=%05d Layer=$%02X Inst=%05d Delay=%03d",n,o->x,o->y,o->layer,o->instptr,o->delay));
}

static void debug_menu(void) {
  int i;
  char buf[81];
  v_status[1]='D';
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
          if(n && stats[n-1].count) win_form("Stat XY list") {
            win_list(stats[n-1].count,stats+n-1,statxy_list_callback,i);
            win_blank();
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
    win_command_esc(0,"Cancel") break;
  }
}

int run_game(void) {
  Uint8 ka=0;
  Sint8 kd=-1;
  Uint32 a,b,c,d,x,y;
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
  if(!(cur_screen.flag&SF_NO_SCROLL) && maxstat && stats->count) {
    if(memory[MEM_SCROLL_X_RATE]) {
      a=stats->xy->x; b=scroll_x;
      if(b<a-cur_screen.soft_edge[DIR_W]) b=a-cur_screen.soft_edge[DIR_W];
      if(b>a-cur_screen.soft_edge[DIR_E]) b=a-cur_screen.soft_edge[DIR_E];
      if(b<scroll_x-memory[MEM_SCROLL_X_RATE]) b=scroll_x-memory[MEM_SCROLL_X_RATE];
      if(b>scroll_x+memory[MEM_SCROLL_X_RATE]) b=scroll_x+memory[MEM_SCROLL_X_RATE];
      if(b<-cur_screen.hard_edge[DIR_W]) b=-cur_screen.hard_edge[DIR_W];
      if(b>board_info.width-1-cur_screen.hard_edge[DIR_E]) b=board_info.width-1-cur_screen.hard_edge[DIR_E];
      scroll_x=b;
    }
    if(memory[MEM_SCROLL_Y_RATE]) {
      a=stats->xy->y; b=scroll_y;
      if(b<a-cur_screen.soft_edge[DIR_N]) b=a-cur_screen.soft_edge[DIR_N];
      if(b>a-cur_screen.soft_edge[DIR_S]) b=a-cur_screen.soft_edge[DIR_S];
      if(b<scroll_y-memory[MEM_SCROLL_Y_RATE]) b=scroll_y-memory[MEM_SCROLL_Y_RATE];
      if(b>scroll_y+memory[MEM_SCROLL_Y_RATE]) b=scroll_y+memory[MEM_SCROLL_Y_RATE];
      if(b<-cur_screen.hard_edge[DIR_N]) b=-cur_screen.hard_edge[DIR_N];
      if(b>board_info.height-1-cur_screen.hard_edge[DIR_S]) b=board_info.height-1-cur_screen.hard_edge[DIR_S];
      scroll_y=b;
    }
  }
  display:
  update_screen();
  if(vtexttime) {
    display_message_text();
    if(!--vtexttime) nvtextbuf=0;
  }
  redisplay();
  ka=0; kd=-1;
  for(;;) {
    if(!next_event()) errx(0,"No events available.");
    repeat_event:
    if(event.type==SDL_KEYDOWN) {
      if(event.key.keysym.unicode>0 && event.key.keysym.unicode<127 && !(event.key.keysym.mod&(KMOD_ALT|KMOD_META))) {
        a=event.key.keysym.unicode;
        if((a>=32 && a<127) || a==8 || a==9 || a==13) {
          ka=a;
          if(event.key.keysym.sym==SDLK_KP2) kd=DIR_S;
          if(event.key.keysym.sym==SDLK_KP4) kd=DIR_W;
          if(event.key.keysym.sym==SDLK_KP6) kd=DIR_E;
          if(event.key.keysym.sym==SDLK_KP8) kd=DIR_N;
        }
      } else {
        switch(event.key.keysym.sym) {
          case SDLK_UP: ka=(event.key.keysym.mod&KMOD_SHIFT)?30:24; kd=DIR_N; break;
          case SDLK_DOWN: ka=(event.key.keysym.mod&KMOD_SHIFT)?31:25; kd=DIR_S; break;
          case SDLK_LEFT: ka=(event.key.keysym.mod&KMOD_SHIFT)?17:27; kd=DIR_W; break;
          case SDLK_RIGHT: ka=(event.key.keysym.mod&KMOD_SHIFT)?16:26; kd=DIR_E; break;
          case SDLK_F1:
            a=system_menu();
            resume:
            *v_status=playstate;
            set_timer(playstate==PLAYSTATE_PAUSED?0:playstate==PLAYSTATE_NORMAL?config.speed:config.speed_fast);
            v_status[1]=0;
            update_screen();
            if(a) goto repeat_event;
            goto display;
          case SDLK_F2:
            a=audio_get_volume();
            audio_set_volume(a&0xFFFF,(a>>16)^1);
            soundon=(audio_get_volume()<0x10000?1:0);
            audio_set_sfx("@0ZCX");
            break;
          case SDLK_F6: if(config.debug) debug_menu(); a=0; goto resume;
          case SDLK_F9: set_timer(0); v_status[1]=24; message_scrollback(); a=0; goto resume;
          case SDLK_F10: return 0;
          case SDLK_F12:
            if(playstate==PLAYSTATE_NORMAL) playstate=PLAYSTATE_FAST; else playstate=PLAYSTATE_NORMAL;
            *v_status=playstate;
            set_timer(playstate==PLAYSTATE_FAST?config.speed_fast:config.speed);
            break;
          case SDLK_DELETE: vtexttime=nvtextbuf=*vtextbuf=0; update_screen(); break;
          case SDLK_INSERT: goto nextturn;
          case SDLK_PAUSE:
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
    }
  }
  nextturn:
  if(ka) run_program(memory[MEM_KEY_EVENT],ka,kd==DIR_E?1:kd==DIR_W?-1:0,kd==DIR_S?1:kd==DIR_N?-1:0,kd);
  ++memory[MEM_FRAME_COUNTER];
  if(run_program(memory[MEM_FRAME_EVENT],0,0,0,0)) goto gameloop;
  for(a=y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++,a++) {
    t=b_main+a;
    if(b=elem_def[t->kind].event[EV_FRAME]) run_program(b,t->kind,x,y,t->param);
  }
  for(a=0;a<maxstat;a++) {
    if(stats[a].xy) for(b=0;b<stats[a].count;b++) {
      if((d=stats[a].xy[b].layer&3) && stats[a].xy[b].x<board_info.width && stats[a].xy[b].y<board_info.height) {
        if(stats[a].speed && !stats[a].xy[b].delay--) {
          t=(d==1?b_under:d==2?b_main:b_over)+stats[a].xy[b].y*board_info.width+stats[a].xy[b].x;
          stats[a].xy[b].delay=0;
          if(d==3 || !run_program(elem_def[t->kind].event[EV_STAT],a+(b<<16)+1,stats[a].xy[b].x,stats[a].xy[b].y,t->param)) stats[a].xy[b].delay=stats[a].speed-1;
        }
      } else {
        // Delete this stat
        memmove(stats[a].xy+b,stats[a].xy+b+1,(--stats[a].count-b)*sizeof(StatXY));
        --b;
      }
    }
  }
  goto gameloop;
}

