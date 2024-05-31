#if 0
gcc -s -O2 -c -Wno-unused-result -fwrapv game.c `sdl-config --cflags`
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
Uint16*memory;
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
  
  if(board_info.flag&BF_PERSIST) {
    
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
      return n/d>f?nf->mark:nf->lead;
    case NF_METER_HALF:
      n/=d;
      return n>=f+f?219:n==f+f-1?nf->mark:nf->lead;
    case NF_METER_EXT:
      return n/d>f+16?nf->mark:nf->lead;
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
      case 0 ... 1: so=op>>12; break;
      case 2: so=memory[pc++]; break;
      case 3: so=memory[memory[pc++]]; break;
      case 4: so=w; break;
      case 5: so=x; break;
      case 6: so=y; break;
      case 7: so=z; break;
      case 8 ... 15: so=regs[(op>>12)&7]; break;
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
        case XOP_EXTRACT_BITS: so=(so>>(ex&15))&((-1)<<((ex>>4)&15)); break;
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
      case OP_BGIV: condflag=(status_vars[fo]&(1<<(so&31))?0:1); status_vars[fo]|=1<<(so&31); break;
      case OP_BIT: so=1<<(so&31); goto store;
      case OP_BTAK: condflag=(status_vars[fo]&(1<<(so&31))?1:0); status_vars[fo]&=~(1<<(so&31)); break;
      case OP_BTST: condflag=((1L<<(so&31))&regs[fo])?1:0; break;
      case OP_CALL: so=run_program(so,w,x,y,z); goto store;
      case OP_CASE: so=memory[(so+regs[fo])&0xFFFF]; goto jump;
      case OP_CHA: do_change(1,regs[fo],so); break;
      case OP_CHAX: do_change(2,regs[fo],so); break;
      case OP_CLAM: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=elem_def[b_main[t].kind].attrib&15; else condflag=0; break;
      case OP_CLAU: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=elem_def[b_under[t].kind].attrib&15; else condflag=0; break;
      case OP_COUN: regs[fo]=do_change(0,regs[fo],so); break;
      case OP_CWOE: t=regs[fo]&0xFF; cwoe: t=(elem_def[t].attrib); t=(t&A_FLOOR?t:0); condflag=((1<<(t&15))&so)?1:0; break;
      case OP_CWOT: if((t=convxy(regs[fo],x,y))!=-1) { t=b_main[t].kind; goto cwoe; } else condflag=0; break;
      case OP_DEC: --so; goto store;
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
      case OP_FORW: forw:
        so&=3; t=x,u=y;
        if(so==DIR_N) u--; else if(so==DIR_S) u++;
        if(so==DIR_W) t--; else if(so==DIR_E) t++;
        if(t>=0 && u>=0 && t<board_info.width && u<board_info.height) condflag=1,x=t,y=u; else condflag=0;
        break;
      case OP_GBF: regs[fo]=board_info.flag&~so; break;
      case OP_GBU: regs[fo]=board_info.userdata&~so; break;
      case OP_GCOU: so&=0xFFFF; so=(so<1?-1:so>maxstat?0:stats[so-1].count); goto store;
      case OP_GIVE: status_vars[fo]+=so; break;
      case OP_GM1: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc1); goto store;
      case OP_GM2: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc2); goto store;
      case OP_GM3: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].misc3); goto store;
      case OP_GO: t=so; so=pc; pc=t; goto store;
      case OP_GOTO: goto jump;
      case OP_GRTR: condflag=(regs[fo]>so?1:0); break;
      case OP_GSPD: so&=0xFFFF; so=(so<1?0:so>maxstat?0:stats[so-1].speed); goto store;
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
      case OP_LESS: condflag=(regs[fo]<so?1:0); break;
      case OP_LET: goto store;
      case OP_LITE: calc_light(fo,so); break;
      case OP_LOOP: if(!regs[fo]) break; --regs[fo]; goto jump;
      case OP_LSH: regs[fo]=(so&~31?0:regs[fo]<<so); break;
      case OP_MAX: if(so>regs[fo]) regs[fo]=so; break;
      case OP_MIN: if(so<regs[fo]) regs[fo]=so; break;
      case OP_MOD: if(so) condflag=1,regs[fo]%=so; else condflag=0; break;
      case OP_MTIL: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=pack_tile(b_main+t); else condflag=0; break;
      case OP_MUL: regs[fo]*=so; break;
      case OP_NEG: so=-so; goto store;
      case OP_NOT: so=~so; goto store;
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
      case OP_PM1: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc1=regs[fo];
      case OP_PM2: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc2=regs[fo];
      case OP_PM3: so&=0xFFFF; if(so>0 && so<=maxstat) stats[so-1].misc3=regs[fo];
      case OP_POKE: memory[so&0xFFFF]=regs[fo]; break;
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
      case OP_SIXY: if((rs=get_statxy(so)) && (so=convxy(0,rs->x,rs->y)+1)) condflag=1; else condflag=so=0; goto store;
      case OP_SUB: regs[fo]-=so; break;
      case OP_SWPA: t=regs[0]; regs[0]=so; so=t; goto store;
      case OP_SWPB: t=regs[1]; regs[1]=so; so=t; goto store;
      case OP_SWPC: t=regs[2]; regs[2]=so; so=t; goto store;
      case OP_SWPD: t=regs[3]; regs[3]=so; so=t; goto store;
      case OP_SWPE: t=regs[4]; regs[4]=so; so=t; goto store;
      case OP_SWPF: t=regs[5]; regs[5]=so; so=t; goto store;
      case OP_SWPG: t=regs[6]; regs[6]=so; so=t; goto store;
      case OP_SWPH: t=regs[7]; regs[7]=so; so=t; goto store;
      case OP_SWPW: t=w; w=so; so=t; goto store;
      case OP_SWPX: t=x; x=so; so=t; goto store;
      case OP_SWPY: t=y; y=so; so=t; goto store;
      case OP_SWPZ: t=z; z=so; so=t; goto store;
      case OP_TAKE: if(status_vars[fo]>=so) status_vars[fo]-=so,condflag=1; else condflag=0; break;
      case OP_TDEC: --so; if(condflag) goto store; break;
      case OP_TINC: ++so; if(condflag) goto store; break;
      case OP_TLET: if(condflag) goto store; break;
      case OP_TMAT: if((t=convxy(so,x,y))!=-1) condflag=(elem_def[b_main[t].kind].attrib&(0x10000000UL<<fo)?1:0); break;
      case OP_TSTB: condflag=((1L<<(regs[fo]&31))&so)?1:0; break;
      case OP_UMAT: if((t=convxy(so,x,y))!=-1) condflag=(elem_def[b_under[t].kind].attrib&(0x10000000UL<<fo)?1:0); break;
      case OP_UNPC: unpack0: if(!so--) break; x=so%board_info.width; y=so%board_info.height; break;
      case OP_UPTO: condflag=(regs[fo]<so?1:0); regs[fo]+=condflag; break;
      case OP_URSH: regs[fo]=(so&~31?0:((Uint32)regs[fo])>>so); break;
      case OP_UTIL: if((t=convxy(so,x,y))!=-1) condflag=1,regs[fo]=pack_tile(b_under+t); else condflag=0; break;
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
      case 12: w=so; break;
      case 13: x=so; break;
      case 14: y=so; break;
      case 15: z=so; break;
    }
    continue;
    jump:
    pc=so;
    if(pc<256) return pc;
  }
}

int run_game(void) {
  Uint32 a,b,c,d,x,y;
  Tile*t;
  reseed(0);
  
  gameloop:
  while(a=memory[MEM_WARP_CALL]) {
    memory[MEM_WARP_CALL]=0;
    b=cur_board_id;
    warp_to_board(memory[MEM_WARP_TO],0);
    run_program(a,b,(memory[MEM_WARP_X_HI]<<16)|memory[MEM_WARP_X_LO],(memory[MEM_WARP_Y_HI]<<16)|memory[MEM_WARP_Y_LO],(memory[MEM_WARP_Z_HI]<<16)|memory[MEM_WARP_Z_LO]);
    send_message(0,"ENTERED");
  }
  *v_status=16;
  //TODO: scrolling
  update_screen();
  //TODO: read input, timer, and system functions
  
  ++memory[MEM_FRAME_COUNTER];
  if(run_program(memory[MEM_FRAME_EVENT],0,0,0,0)) goto gameloop;
  for(a=y=0;y<board_info.height;y++) for(x=0;x<board_info.width;x++,a++) {
    t=b_main+a;
    if(b=elem_def[t->kind].event[EV_FRAME]) run_program(b,t->kind,x,y,t->param);
  }
  for(a=0;a<maxstat;a++) {
    if(stats[a].xy) for(b=0;b<stats[a].count;b++) {
      if((d=stats[a].xy[b].layer&3) && stats[a].xy[b].x<board_info.width && stats[a].xy[b].y<board_info.height) {
        if(!stats[a].xy[b].delay--) {
          t=(d==1?b_under:d==2?b_main:b_over)+stats[a].xy[b].y*board_info.width+stats[a].xy[b].x;
          if(d==3 || !run_program(elem_def[t->kind].event[EV_STAT],a+(b<<16)+1,stats[a].xy[b].x,stats[a].xy[b].y,t->param)) {
            stats[a].xy[b].delay=stats[a].speed;
            run_script(a+1,b,1);
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

