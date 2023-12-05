#if 0
gcc -s -O2 -c -Wno-unused-result -fwrapv game.c `sdl-config --cflags`
exit
#endif

#include "common.h"
#include "opcodes.h"

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

static Uint8 digit_of(Uint32 n,Uint8 f) {
  NumericFormat*nf=num_format+(f>>4);
  Uint8 d=nf->div;
  int i;
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
      
    case NF_BOARD_NAME_EXT:
      
      return nf->lead;
  }
  return '?';
}

static Uint8 draw_tile(Sint32 bx,Sint32 by,Uint8 sx,Uint8 sy,Uint8 h) {
  
}

void update_screen(void) {
  int i;
  Uint32 v;
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
        
        break;
      case SC_NUMERIC:
        v_char[i]=digit_of(status_vars[cmd&15],chr);
        v_color[i]=col;
        break;
      case SC_NUMERIC_SPECIAL:
        switch(cmd) {
          case SC_SPEC_PLAYER_X: v=stats->count?stats->xy->x:0; break;
          case SC_SPEC_PLAYER_Y: v=stats->count?stats->xy->y:0; break;
          case SC_SPEC_CAMERA_X: 
          case SC_SPEC_CAMERA_Y: 
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
        
        break;
      case SC_TEXT:
        
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
  Stat*s=stats+n;
  StatXY*r;
  s->xy=realloc(s->xy,++s->count*sizeof(StatXY));
  if(!s->xy) errx(1,"Allocation failed");
  r=s->xy+s->count-1;
  r->x=r->y=r->instptr=0;
  r->layer=r->delay=0;
  return r;
}

static Uint32 dice(Uint32 n) {
  
}

static Sint32 xop_special(Sint32 so,Uint16 ex) {
  switch(ex&0x0FF0) {
    case XOP_S_EVENT: return elem_def[so&255].event[ex&15];
    case XOP_S_STATUS_PLUS: return status_vars[ex&15]+so;
    case XOP_S_STATUS_MINUS: return status_vars[ex&15]-so;
    case XOP_S_WIDTH: return board_info.width+so+(ex&15)-8;
    case XOP_S_HEIGHT: return board_info.height+so+(ex&15)-8;
    case XOP_S_PLAYER_X: return stats->count?stats->xy->x:0;
    case XOP_S_PLAYER_Y: return stats->count?stats->xy->y:0;
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

static Sint32 run_program(Uint16 pc,Sint32 w,Sint32 x,Sint32 y,Sint32 z) {
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
        case XOP_RANDOM: so=dice(so+(ex&255)); break;
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
      case OP_CALL: if(fo==8) goto jump; so=run_program(so,w,x,y,z); goto store;
      case OP_CASE: so=memory[(so+regs[fo])&0xFFFF]; goto jump;
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
      case OP_LITE: memory[0xE3]=(fo<<8)|(so<0?0:so>255?255:so); break;
      case OP_LOOP: if(!regs[fo]) break; --regs[fo]; goto jump;
      case OP_LSH: regs[fo]=(so&~31?0:regs[fo]<<so); break;
      case OP_MAX: if(so>regs[fo]) goto store; break;
      case OP_MIN: if(so<regs[fo]) goto store; break;
      case OP_MOD: if(so) condflag=1,regs[fo]%=so; else condflag=0; break;
      case OP_MUL: regs[fo]*=so; break;
      case OP_NEG: so=-so; goto store;
      case OP_NOT: so=~so; goto store;
      case OP_OR: regs[fo]|=so; break;
      case OP_PACK:
        t=convxy(0,x,y);
        if(t!=-1) {
          condflag=1;
          so=t+1;
          goto store;
        } else {
          condflag=0;
          if(so) goto store;
        }
        break;
      case OP_PBF: board_info.flag=so; break;
      case OP_PBU: board_info.userdata=so; break;
      case OP_PEEK: regs[fo]=memory[so&0xFFFF]; break;
      case OP_PEER: regs[fo]=memory[(so+regs[fo])&0xFFFF]; break;
      case OP_POKE: memory[so&0xFFFF]=regs[fo]; break;
      case OP_REGL: load_registers(fo,so); break;
      case OP_REGS: save_registers(fo,so); break;
      case OP_ROB: if(status_vars[fo]>so) status_vars[fo]-=so,condflag=1; else status_vars[fo]=0,condflag=0; break;
      case OP_RSH: regs[fo]=(so&~31?(regs[fo]<0?-1:0):regs[fo]>>so); break;
      case OP_RSUB: regs[fo]=so-regs[fo]; break;
      case OP_SEX: so=(Sint16)so; goto store;
      case OP_SGN: so=(so<0?-1:so>0?1:0); goto store;
      case OP_SUB: regs[fo]-=so; break;
      case OP_TAKE: if(status_vars[fo]>=so) status_vars[fo]-=so,condflag=1; else condflag=0; break;
      case OP_TDEC: --so; if(condflag) goto store; break;
      case OP_TINC: ++so; if(condflag) goto store; break;
      case OP_TLET: if(condflag) goto store; break;
      case OP_TMAT: if((t==convxy(so,x,y))!=-1) condflag=(elem_def[b_main[t].kind].attrib&(0x10000000UL<<fo)?1:0); break;
      case OP_TSTB: condflag=((1L<<(regs[fo]&31))&so)?1:0; break;
      case OP_UMAT: if((t==convxy(so,x,y))!=-1) condflag=(elem_def[b_under[t].kind].attrib&(0x10000000UL<<fo)?1:0); break;
      case OP_UNPC: if(!so--) break; x=so%board_info.width; y=so%board_info.height; break;
      case OP_UPTO: condflag=(regs[fo]<so?1:0); regs[fo]+=condflag; break;
      case OP_URSH: regs[fo]=(so&~31?0:((Uint32)regs[fo])>>so); break;
      case OP_VGET: so=status_vars[so&15]; goto store;
      case OP_VPUT: status_vars[so&15]=regs[fo]; break;
      case OP_VSET: status_vars[fo]=so; break;
      case OP_WARP:
        memory[0xE4]=regs[fo];
        memory[0xE5]=(so&0xFFFF?:1);
        memory[0xE6]=x>>16; memory[0xE7]=x;
        memory[0xE8]=y>>16; memory[0xE9]=y;
        memory[0xEA]=z>>16; memory[0xEB]=z;
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
  //TODO
}

