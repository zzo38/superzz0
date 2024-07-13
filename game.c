#if 0
gcc -s -O2 -c -std=gnu99 -Wno-unused-result -fwrapv game.c `sdl-config --cflags`
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

#define PLAYSTATE_NORMAL 16
#define PLAYSTATE_FAST 175
#define PLAYSTATE_PAUSED 186
static Uint8 playstate=PLAYSTATE_NORMAL;

typedef struct {
  Uint8 text[81];
} MessageScrollback;
static MessageScrollback*scrback;
static Uint16 nscrback;

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
  if(cur_board_id!=b || !board_info.width) {
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
          case SC_SPEC_TEXT_SCROLL_PERCENT: 
          case SC_SPEC_TEXT_LINE_NUMBER: 
          case SC_SPEC_TEXT_LINE_COUNT: 
          case SC_SPEC_EXIT_E: v=board_info.exits[DIR_E]; break;
          case SC_SPEC_EXIT_N: v=board_info.exits[DIR_N]; break;
          case SC_SPEC_EXIT_W: v=board_info.exits[DIR_W]; break;
          case SC_SPEC_EXIT_S: v=board_info.exits[DIR_S]; break;
          case SC_SPEC_WIDTH: v=board_info.width; break;
          case SC_SPEC_HEIGHT: v=board_info.height; break;
          case SC_SPEC_USERDATA: v=board_info.userdata; break;
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
    if(!b || b=='\n' || b==' ' || b=='\r') {
      if(!a || a=='\n' || a==' ' || a=='\r' || a==';') {
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
    if(!b || b==':' || b=='=' || b=='\n' || b=='\r') {
      return (!a || a==':' || a=='=' || a=='\n' || a=='\r');
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

static void send_message(Uint32 n,const char*label) {
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
    if(r->layer&0x80) return;
    f=find_label(stats+(n&0xFF)-1,label);
    if(f!=-1) r->instptr=f;
  }
}

static void run_script(Uint16 m,Uint16 n,Sint32 u) {
#if 0
  // m=stat number, n=XY index, u=(<0 if imply #, =0 if restart, >0 if normal)
  char buf[128];
  Stat*s=stats+m-1;
  StatXY*xy=s->xy+n;
  Uint16 ip=xy->instptr;
  Uint8 c,n;
  if(!u) xy->instptr=ip=0;
  if(!s->text) return;
  if(ip>=s->length) {
    xy->instptr=0xFFFF;
    return;
  }
  begin:
  switch(c) {
    case '#':
      ip++; // fall through
    command:
      if(s->text[ip]=='=') {
        ip++;
        if(s->text[ip]==' ') ip++;
        send:
        
      } else {
        n=0;
        while((n<64) && (c=s->text[ip++])) {
          if((c>='A' && c<='Z') || (c>='0' && c<='9')) buf[n++]=c;
          else if(c>='a' && c<='z') buf[n++]=c+'A'-'a';
          else break;
        }
        buf[n]=0;
        
      }
      break;
    case '/':
      
      break;
    case '?':
      
      break;
    case '\'': case ':': case '@':
      while(s->text[ip] && s->text[ip]!='\n') ip++;
      if(s->text[ip]) ip++;
      break;
    default:
      if(u<0 && c!='!' && c!='$') goto command;
      //TODO: text
  }
  u=0;
  goto begin;
#endif
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
      case OP_EMAT: condflag=(elem_def[so&255].attrib&(0x10000000UL<<fo)?1:0); break;
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
      case OP_LESS: condflag=(regs[fo]<so?1:0); break;
      case OP_LET: goto store;
      case OP_LITE: calc_light(fo,so); break;
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
      case OP_SEND: if(so>0 && so<=ngtext) send_message(regs[fo],gtext[so]); else if(!so) send_message(regs[fo],textbuf); break;
      case OP_SEX: so=(Sint16)so; goto store;
      case OP_SGN: so=(so<0?-1:so>0?1:0); goto store;
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
      case OP_TEXT: do_text_op(fo,so); break;
      case OP_TINC: ++so; if(condflag) goto store; break;
      case OP_TLET: if(condflag) goto store; break;
      case OP_TMAT: if((t=convxy(so,x,y))!=-1) condflag=(elem_def[b_main[t].kind].attrib&(0x10000000UL<<fo)?1:0); break;
      case OP_TSTB: condflag=((1L<<(regs[fo]&31))&so)?1:0; break;
      case OP_UMAT: if((t=convxy(so,x,y))!=-1) condflag=(elem_def[b_under[t].kind].attrib&(0x10000000UL<<fo)?1:0); break;
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
  Uint8 x=config.menu_x;
  Uint8 y=config.menu_y;
  Uint8 z;
  set_timer(0);
  v_status[1]='F';
  redraw0:
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
  // (speed)
  // (volume)
  // (???)
  // (???)
  // (???)
  // =^v<>=    Menu position
  // 012345678901234567890123456789012345
  redraw1:
  draw_text(x+2,y+1," F1  ",0x30,-1); draw_text(x+8,y+1,"Menu",0x1F,-1);
  draw_text(x+2,y+2," F2  ",0x70,-1); //draw_text(x+8,y+2,"Sound:",0x1F,-1);
  draw_text(x+2,y+3," F3  ",0x30,-1); draw_text(x+8,y+3,"Save",0x1F,-1);
  draw_text(x+2,y+4," F4  ",0x70,-1); draw_text(x+8,y+4,"Restore",0x1F,-1);
  draw_text(x+2,y+5," F5  ",0x30,-1); draw_text(x+8,y+5,"Q. Save",0x1F,-1);
  draw_text(x+2,y+6," F6  ",0x70,-1); if(config.debug) draw_text(x+8,y+6,"Debug",0x1F,-1);
  draw_text(x+21,y+1," F7  ",0x30,-1); draw_text(x+27,y+1,"Q. Restore",0x1F,-1);
  draw_text(x+21,y+2," F8  ",0x70,-1);
  draw_text(x+21,y+3," F9  ",0x30,-1); draw_text(x+27,y+3,"Messages",0x1F,-1);
  draw_text(x+21,y+4," F10 ",0x70,-1); draw_text(x+27,y+4,"Quit",0x1F,-1);
  draw_text(x+21,y+5," F11 ",0x30,-1); draw_text(x+27,y+5,"Print",0x1F,-1);
  draw_text(x+21,y+6," F12 ",0x70,-1); draw_text(x+27,y+6,"Speed:",0x1F,-1);
  draw_text(x+34,y+6,playstate==PLAYSTATE_NORMAL?"NORM":playstate==PLAYSTATE_FAST?"FAST":"STOP",0x1A,-1);
  draw_text(x+2,y+7," PAUSE ",0x30,-1); draw_text(x+12,y+7,"Pause/resume",0x1F,-1);
  draw_text(x+2,y+8," INS ",0x70,-1); draw_text(x+12,y+8,"Next frame",0x1F,-1);
  draw_text(x+2,y+9," DEL ",0x30,-1); draw_text(x+12,y+9,"Clear message",0x1F,-1);
  draw_text(x+2,y+15," \x18\x19\x1B\x1A ",0x30,-1); draw_text(x+12,y+15,"Menu position",0x1F,-1);
  redisplay();
  do { if(!next_event()) errx(0,"No events available."); } while(event.type!=SDL_KEYDOWN);
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: case SDLK_F1: case SDLK_SPACE: case SDLK_RETURN: return 0;
    case SDLK_F3: case SDLK_F4: case SDLK_F5: case SDLK_F6: case SDLK_F7: case SDLK_F9: case SDLK_F10: case SDLK_INSERT: return 1;
    case SDLK_F12: *v_status=playstate=(playstate==PLAYSTATE_NORMAL?PLAYSTATE_FAST:PLAYSTATE_NORMAL); break;
    case SDLK_PAUSE: *v_status=playstate=(playstate==PLAYSTATE_PAUSED?PLAYSTATE_NORMAL:PLAYSTATE_PAUSED); break;
    case SDLK_DELETE: vtexttime=nvtextbuf=*vtextbuf=0; goto redraw0;
    case SDLK_UP: case SDLK_KP8: if(y>1) y-=2; goto redraw0;
    case SDLK_DOWN: case SDLK_KP2: if(y<7) y+=2; goto redraw0;
    case SDLK_LEFT: case SDLK_KP4: if(x>2) x-=3; goto redraw0;
    case SDLK_RIGHT: case SDLK_KP6: if(x<37) x+=3; goto redraw0;
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
  }
  goto redraw;
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
        win_blank();
        win_command_esc(0,"Cancel") break;
      }
    }
    win_command('S',"Stats...") {
      
    }
    win_command('R',"Registers...") {
      win_form("Registers") {
        win_blank();
        win_command_esc(0,"Cancel") break;
      }
    }
    win_command('M',"Memory...") {
      
    }
    win_command('a',"Set random seed...") {
      *buf=0;
      ask_text("New random seed:",buf,32);
      if(*buf) rseed=strtoll(buf,0,0);
      if(!rseed) reseed(0);
    }
    win_command('C',"Call...") {
      
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
  if(config.message_scrollback && config.message_scrollback<24) config.message_scrollback=24;
  warp_to_board(cur_board_id,1);
  set_timer(config.speed);
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
          if(d==3 || !run_program(elem_def[t->kind].event[EV_STAT],a+(b<<16)+1,stats[a].xy[b].x,stats[a].xy[b].y,t->param)) {
            stats[a].xy[b].delay=stats[a].speed-1;
            if(stats[a].xy[b].instptr!=0xFFFF) run_script(a+1,b,1);
          }
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

