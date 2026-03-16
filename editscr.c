#if 0
gcc $CFLAGS -c -Wno-unused-result -std=gnu99 editscr.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

typedef struct {
  Uint8 com,col,par,ext;
} ScTile;

static Uint16 scr_id;
static Sint16 xcur,ycur,xcur2,ycur2;
static Uint8 status_on=255;
static ScTile clip;
static Uint16 numprefix;
static Uint8 emode;
static Uint8 markgrid[250];
static Uint8 viewmode;

void set_screen_name(Uint16 id,const char*name) {
  if(maxscreen<id) {
    screennames=realloc(screennames,(id+1)*sizeof(Uint8*));
    if(!screennames) err(1,"Allocation failed");
    while(maxscreen<id) screennames[++maxscreen]=0;
  } else if(!screennames) {
    screennames=calloc(maxscreen+1,sizeof(Uint8*));
    if(!screennames) err(1,"Allocation failed");
  }
  free(screennames[id]);
  screennames[id]=strdup(name);
  if(!screennames[id]) err(1,"Allocation failed");
}

static void goto_screen(Uint16 id) {
  FILE*fp=open_lump_by_number(id,"SCR","r");
  const char*e;
  if(fp) {
    if(e=load_screen(fp)) alert_text(e);
    fclose(fp);
  } else {
    memset(&cur_screen,0,sizeof(Screen));
    cur_screen.view_x=40;
    cur_screen.view_y=12;
    cur_screen.message_r=79;
    cur_screen.soft_edge[DIR_E]=cur_screen.hard_edge[DIR_E]=79;
    cur_screen.soft_edge[DIR_S]=cur_screen.hard_edge[DIR_S]=24;
  }
  scr_id=id;
  scroll_x=scroll_y=0;
  work_varproperties(&cur_screen.varprop);
}

static void edit_window(void) {
  WindowInfo wind;
  FILE*fp;
  const char*e;
  Uint8 xc=xcur;
  int i;
  v_xcur=255;
  if(fp=open_lump_by_number(scr_id,"WIN","r")) {
    if(e=load_window(fp,&wind)) alert_text(e);
    fclose(fp);
  } else {
    wind.flag=0;
    memset(wind.command,'X',80);
    memset(wind.color,0,80);
    memset(wind.parameter,0,80);
    memset(wind.wcolor,0,16);
  }
  show:
  memset(v_color,7,80*25);
  memset(v_char,0,80*25);
  draw_text(0,0," Window Edit ",0x1F,-1);
  for(i=0;i<80;i++) {
    v_color[i+3*80]=7; v_char[i+3*80]=wind.command[i];
    if(wind.color[i]%17) {
      v_color[i+4*80]=wind.color[i]; v_char[i+4*80]=7;
    } else {
      v_color[i+4*80]=15; v_char[i+4*80]=" 12-------------"[wind.color[i]/17];
    }
    if(wind.parameter[i]==255) {
      v_color[i+5*80]=0x1A; v_char[i+5*80]='+';
    } else if(wind.parameter[i]) {
      v_color[i+5*80]=0x1F; v_char[i+5*80]=wind.parameter[i];
    } else {
      v_color[i+5*80]=0x19; v_char[i+5*80]='-';
    }
  }
  v_char[cur_screen.soft_edge[DIR_W]+7*80]='('; v_char[cur_screen.soft_edge[DIR_E]+7*80]=')';
  v_char[cur_screen.hard_edge[DIR_W]+7*80]='['; v_char[cur_screen.hard_edge[DIR_E]+7*80]=']';
  draw_text(0,23,"<A-Z> Command  <1> Primary  <2> Secondary  <3> Color  <4> Parameter",7,-1);
  draw_text(0,24,"<F1> Flags   <F5> Save   <F6> Delete",7,-1);
  for(;;) {
    v_color[xc+2*80]=v_color[xc+6*80]=0x0E; v_char[xc+2*80]=31; v_char[xc+6*80]=30;
    redisplay();
    do { if(!next_event()) return; } while(event.type!=SDL_KEYDOWN);
    v_color[xc+2*80]=v_color[xc+6*80]=0;
    switch(event.key.keysym.sym) {
      case SDLK_LEFT: case SDLK_COMMA:
        if(xc) {
          --xc;
          if(event.key.keysym.mod&KMOD_SHIFT) {
            wind.command[xc]=wind.command[xc+1];
            wind.color[xc]=wind.color[xc+1];
            wind.parameter[xc]=wind.parameter[xc+1];
            goto show;
          }
        }
        break;
      case SDLK_RIGHT: case SDLK_PERIOD:
        if(xc!=79) {
          ++xc;
          if(event.key.keysym.mod&KMOD_SHIFT) {
            wind.command[xc]=wind.command[xc-1];
            wind.color[xc]=wind.color[xc-1];
            wind.parameter[xc]=wind.parameter[xc-1];
            goto show;
          }
        }
        break;
      case SDLK_HOME: case SDLK_LEFTBRACKET: xc=0; break;
      case SDLK_END: case SDLK_RIGHTBRACKET: xc=79; break;
      case SDLK_a ... SDLK_z: wind.command[xc]=event.key.keysym.sym-SDLK_a+'A'; goto show;
      case SDLK_1: wind.color[xc]=0x11; goto show;
      case SDLK_2: wind.color[xc]=0x22; goto show;
      case SDLK_3: wind.color[xc]=ask_color_char(0,wind.color[xc]); goto show;
      case SDLK_4: wind.parameter[xc]=ask_color_char(1,wind.parameter[xc]); goto show;
      case SDLK_F1:
        win_form("Window flags") {
          win_help("editwin","flag");
          win_boolean('S',"Single ends",wind.flag,WF_SINGLE_ENDS);
          win_boolean('Z',"Zero-based line numbers",wind.flag,WF_ZERO_BASED);
          win_boolean('H',"Horizontal scrolling",wind.flag,WF_HORIZ_SCROLL);
          win_boolean('X',"XOR color",wind.flag,WF_XOR_COLOR);
          win_boolean('t',"Alternate scrolling",wind.flag,WF_ALT_SCROLL);
          win_boolean('y',"Use window key event",wind.flag,WF_KEY_EVENT);
          win_blank();
          win_color('N',"Normal text:   ",wind.wcolor[WC_NORMAL_TEXT]);
          win_color('L',"Link text:     ",wind.wcolor[WC_LINK_TEXT]);
          win_color('C',"Center text:   ",wind.wcolor[WC_CENTER_TEXT]);
          win_color('a',"Label text:    ",wind.wcolor[WC_LABEL_TEXT]);
          win_blank();
          win_color('i',"Normal item:   ",wind.wcolor[WC_NORMAL_ITEM]);
          win_color('K',"Key item:      ",wind.wcolor[WC_KEY_ITEM]);
          win_color('F',"Fixed item:    ",wind.wcolor[WC_FIXED_ITEM]);
          win_color('g',"Hilight item:  ",wind.wcolor[WC_HILIGHT_ITEM]);
          win_color('V',"Vacant item:   ",wind.wcolor[WC_VACANT_ITEM]);
          win_blank();
          win_color('M',"Move item:     ",wind.wcolor[WC_MOVE_ITEM]);
          win_color('e',"Selected item: ",wind.wcolor[WC_SELECTED_ITEM]);
          win_blank();
          win_command_esc(0,"Done") break;
        }
        goto show;
      case SDLK_F5: goto save;
      case SDLK_F6: goto delete;
      case SDLK_SLASH: case SDLK_QUESTION: online_help("editwin",0); goto show;
    }
  }
  save:
  if(fp=open_lump_by_number(scr_id,"WIN","w")) {
    fputc(wind.flag,fp);
    fputc(0,fp);
    for(i=xc=0;xc<16;xc++) if(wind.wcolor[xc]) i|=1<<xc;
    if(i) {
      fputc(0x7F,fp);
      write16(fp,i);
      for(i=0;i<16;i++) if(wind.wcolor[i]) fputc(wind.wcolor[i],fp);
    }
    fputc(wind.command[0]|0xE0,fp);
    fputc(wind.parameter[0],fp);
    fputc(wind.color[0],fp);
    for(xc=0,i=1;i<80;i++) {
      if(wind.command[i]!=wind.command[i-1] || wind.parameter[i]!=wind.parameter[i-1] || wind.color[i]!=wind.color[i-1]) {
        if(xc) fputc(xc,fp);
        xc=0;
        fputc((wind.command[i]&0x1F)+(wind.parameter[i]==wind.parameter[i-1]?0:0x20)+(wind.color[i]==wind.color[i-1]?0:0x40)+0x80,fp);
        if(wind.parameter[i]!=wind.parameter[i-1]) fputc(wind.parameter[i],fp);
        if(wind.color[i]!=wind.color[i-1]) fputc(wind.color[i],fp);
      } else {
        ++xc;
      }
    }
    if(xc) fputc(xc,fp);
    fclose(fp);
  }
  return;
  delete:
  if(fp=open_lump_by_number(scr_id,"WIN","w")) fclose(fp);
}

static void edit_screen_info(void) {
  char nam[61]="";
  if(scr_id<=maxscreen && screennames && screennames[scr_id]) strncpy(nam,screennames[scr_id],60);
  nam[60]=0;
  win_form("Screen info") {
    win_help("editscr","info");
    win_text(':',"Screen name: ",nam);
    win_blank();
    win_numeric('X',"View center X: ",cur_screen.view_x,0,79);
    win_numeric('Y',"View center Y: ",cur_screen.view_y,0,24);
    win_blank();
    win_numeric('a',"Message X: ",cur_screen.message_x,0,255);
    win_numeric('g',"Message Y: ",cur_screen.message_y,0,25);
    win_numeric('l',"Message left boundary: ",cur_screen.message_l,0,79);
    win_numeric('r',"Message right boundary: ",cur_screen.message_r,0,79);
    win_blank();
    win_char('E',"East border:  ",cur_screen.border[DIR_E]);
    win_char('N',"North border: ",cur_screen.border[DIR_N]);
    win_char('W',"West border:  ",cur_screen.border[DIR_W]);
    win_char('S',"South border: ",cur_screen.border[DIR_S]);
    win_color('B',"Border color: ",cur_screen.border_color);
    win_blank();
    win_boolean('m',"Left align message",cur_screen.flag,SF_LEFT_ALIGN_MESSAGE);
    win_boolean('F',"Flashy message",cur_screen.flag,SF_FLASHY_MESSAGE);
    win_boolean('t',"Exit indicators use border character",cur_screen.flag,SF_EXIT_BORDER);
    win_boolean('U',"User indicators use border character",cur_screen.flag,SF_USER_BORDER);
    win_boolean('o',"Disable scrolling",cur_screen.flag,SF_NO_SCROLL);
    win_boolean('i',"Message edging",cur_screen.flag,SF_MESSAGE_EDGE);
    win_boolean('d',"Alternate mode",cur_screen.flag,SF_ALT_MODE);
    win_blank();
    win_command('.',"Edges...") {
      win_form("Screen edges") {
        win_help("editscr","edge");
        win_numeric('E',"East soft edge:  ",cur_screen.soft_edge[DIR_E],0,79);
        win_numeric('N',"North soft edge: ",cur_screen.soft_edge[DIR_N],0,24);
        win_numeric('W',"West soft edge:  ",cur_screen.soft_edge[DIR_W],0,79);
        win_numeric('S',"South soft edge: ",cur_screen.soft_edge[DIR_S],0,24);
        win_blank();
        win_numeric('a',"East hard edge:  ",cur_screen.hard_edge[DIR_E],0,79);
        win_numeric('o',"North hard edge: ",cur_screen.hard_edge[DIR_N],0,24);
        win_numeric('t',"West hard edge:  ",cur_screen.hard_edge[DIR_W],0,79);
        win_numeric('h',"South hard edge: ",cur_screen.hard_edge[DIR_S],0,24);
        win_blank();
        win_command_esc(0,"Done") break;
      }
    }
    win_command_esc(0,"Done") break;
  }
  if(*nam) set_screen_name(scr_id,nam);
}

static void set_edges(Uint8*e) {
  Sint16 x0=xcur;
  Sint16 y0=ycur;
  Sint16 x1=xcur2;
  Sint16 y1=ycur2;
  Sint16 z;
  if(x0>x1) z=x0,x0=x1,x1=z;
  if(y0>y1) z=y0,y0=y1,y1=z;
  e[DIR_E]=x1;
  e[DIR_N]=y0;
  e[DIR_W]=x0;
  e[DIR_S]=y1;
}

static void make_border(Uint8 thic) {
  // 314
  // 202
  // 516
  Uint8 bor[16]={
    0x00,0xC4,0xB3,0xDA,0xBF,0xC0,0xD9,0xC5,
    0x00,0xCD,0xBA,0xC9,0xBB,0xC8,0xBC,0xCE,
  };
  Sint16 x0=xcur;
  Sint16 y0=ycur;
  Sint16 x1=xcur2;
  Sint16 y1=ycur2;
  Sint16 z;
  if(x0>x1) z=x0,x0=x1,x1=z;
  if(y0>y1) z=y0,y0=y1,y1=z;
  for(z=y0;z<=y1;z++) {
    memset(cur_screen.command+z*80+x0,SC_BACKGROUND|3,x1+1-x0);
    memset(cur_screen.color+z*80+x0,clip.col,x1+1-x0);
    memset(cur_screen.parameter+z*80+x0,bor[thic+(z==y0?1:z==y1?1:0)],x1-x0);
    cur_screen.parameter[z*80+x0]=bor[thic+(z==y0?3:z==y1?5:2)];
    cur_screen.parameter[z*80+x1]=bor[thic+(z==y0?4:z==y1?6:2)];
  }
}

static void edit_tile(void) {
  char buf[8];
  static const char*const indic[16]={"Board user data","Cursor","Scroll Y","Scroll X",0,0,0,0,"Exit East","Exit North","Exit West","Exit South","User 0","User 1","User 2","User 3"};
  static const char*const valu[32]={"(A)","(B)","(C)","(D)","(E)","(F)","(G)","(H)","(S)","(T)","(U)","(V)","(W)","(X)","(Y)","(Z)",
   "Player X","Player Y","Camera X","Camera Y","Scroll %","Line number","Line count","Cur. board","Exit East","Exit North","Exit West","Exit South","Width","Height","User","Context-specific"};
  Uint8 x=xcur;
  Uint8 y=ycur;
  Uint8 done=0;
  Uint16 a,b,c,d,h,i;
  xcur=255;
  win_form("Tile") {
    win_numeric('X',"X: ",x,0,79);
    win_numeric('Y',"Y: ",y,0,24);
    if(x!=xcur || y!=ycur) {
      update:
      if(xcur!=255) {
        switch(h) {
          case SC_BACKGROUND: h|=b&3; break;
          case SC_BOARD: h|=b&7; break;
          case SC_NUMERIC: case SC_NUMERIC_SPECIAL: h|=b&15; a=(a<<4)+d; break;
          case SC_MEMORY: c=a>>8; break;
          case SC_INDICATOR: h|=b; break;
          case SC_TEXT: h|=b&15; break;
          case SC_ITEM: h|=b&15; if(b==1) a|=d<<5; if(b==2) a|=d&15; break;
          case SC_BITS_0_LO: h|=b; break;
        }
        cur_screen.command[ycur*80+xcur]=h;
        cur_screen.parameter[ycur*80+xcur]=a;
        cur_screen.color[ycur*80+xcur]=c;
      }
      xcur=x;
      ycur=y;
      if(done) break;
      h=cur_screen.command[y*80+x]&0xF0;
      if(h&0x80) h=0x80;
      a=cur_screen.parameter[y*80+x];
      b=cur_screen.command[y*80+x]&15;
      c=cur_screen.color[y*80+x];
      switch(h) {
        case SC_NUMERIC: case SC_NUMERIC_SPECIAL: d=a&15; a>>=4; break;
        case SC_MEMORY: a|=c<<8; break;
        case SC_TEXT: b|=c&0xF0; break;
        case SC_ITEM: if(b==1) d=a>>5; if(b==2) d=a&15; break;
        case SC_BITS_0_LO: b=cur_screen.command[y*80+x]&0x7F; break;
      }
      win_refresh();
    }
    win_blank();
    win_option('k',"Background",h,SC_BACKGROUND) win_refresh();
    win_option('B',"Board tiles",h,SC_BOARD) win_refresh();
    win_option('N',"Numeric variable",h,SC_NUMERIC) win_refresh();
    win_option('S',"Special numeric variable",h,SC_NUMERIC_SPECIAL) win_refresh();
    win_option('M',"Memory",h,SC_MEMORY) win_refresh();
    win_option('I',"Indicator",h,SC_INDICATOR) win_refresh();
    win_option('w',"Text window",h,SC_TEXT) win_refresh();
    win_option('t',"Item window",h,SC_ITEM) win_refresh();
    win_option('o',"Bit of variable",h,SC_BITS_0_LO) win_refresh();
    win_blank();
    if(h!=SC_MEMORY) a&=0xFF;
    if(h==SC_NUMERIC || h==SC_NUMERIC_SPECIAL) a&=15,d&=15;
    switch(h) {
      case SC_BACKGROUND:
        win_char('h',"Character: ",a);
        win_color('C',"Color: ",c);
        win_boolean('e',"Show character",b,1);
        win_boolean('r',"Show color",b,2);
        break;
      case SC_BOARD:
        win_char('h',"Default character: ",a);
        win_color('c',"Default color: ",c);
        win_boolean('U',"Use defaults if out of range",b,1);
        win_boolean('r',"Show border",b,2);
        win_boolean('p',"Suppress overlay",b,4);
        break;
      case SC_NUMERIC: case SC_NUMERIC_SPECIAL:
        win_color('C',"Color: ",c);
        win_numeric('D',"Digit position: ",d,0,15);
        win_numeric('F',"Format: ",a,0,15);
        win_picture(1) {
          draw_text(1,0,"Variable: ",7,-1);
          draw_text(11,0,valu[(b&15)|(h&0x10)]?:"???",15,-1);
        }
        win_command('v',"Select variable...") {
          b|=h&0x10;
          win_form("Select variable") {
            for(i=h&16;i<16+(h&16);i++) if(valu[i]) win_option("ABCDEFGHSTUVWXYZXYmaoLcbENWSitUp"[i],valu[i],b,i);
            win_blank();
            win_command_esc(0,"OK") break;
          }
          b&=0x0F;
        }
        break;
      case SC_MEMORY:
        win_numeric('A',"Address: ",a,0,65535);
        break;
      case SC_INDICATOR:
        win_char('h',"Character: ",a);
        win_color('C',"Color: ",c);
        win_picture(1) {
          draw_text(1,0,"Indicator: ",7,-1);
          draw_text(12,0,indic[b&15]?:"???",15,-1);
        }
        win_command('d',"Select indicator...") {
          win_form("Select indicator") {
            for(i=0;i<16;i++) if(indic[i]) win_option("tuol....ENWS0123"[i],indic[i],b,i);
            win_blank();
            win_command_esc(0,"OK") break;
          }
        }
        break;
      case SC_TEXT:
        win_char('h',"Default character: ",a);
        win_color('c',"Primary color: ",c) b=(b&0x0F)|(c&0xF0);
        win_color('d',"Secondary color: ",b) c=(c&0x0F)|(b&0xF0);
        break;
      case SC_ITEM:
        win_option('P',"Placeholder",b,0) win_refresh();
        win_option('E',"Element",b,1) win_refresh();
        win_option('f',"Select field",b,2) win_refresh();
        win_option('d',"Show field",b,3) win_refresh();
        snprintf(buf,7,"Flag 0");
        for(i=0;i<8;i++) win_option(buf[5]=i+'0',buf,b,i+8) win_refresh();
        win_blank();
        win_color('C',"Color: ",c);
        if(b==0 || b>=8) win_char('h',"Character: ",a);
        if(b==1) {
          win_numeric('v',"Inventory: ",d,0,7);
          a&=31; win_numeric('l',"Slot: ",a,0,31);
        }
        if(b==2) {
          win_command('.',"Field detail...") {
            d&=15;
            win_form("Window field specification") {
              win_boolean('H',"Hide if hiding quantity",a,0x80);
              win_boolean('M',"Multiply by quantity",a,0x40);
              win_blank();
              win_option('0',"Ext0",d,0);
              win_option('1',"Ext1",d,1);
              win_option('2',"Ext2",d,2);
              win_option('3',"Ext3",d,3);
              win_option('4',"Ext4",d,4);
              win_option('5',"Ext5",d,5);
              win_option('8',"Menu value (8-bits)",d,6);
              win_option('c',"Price",d,7);
              win_option('P',"Parameter",d,8);
              win_option('W',"Weight",d,9);
              win_option('o',"Constant",d,10);
              win_option('S',"Strength",d,11);
              win_option('h',"Max heap",d,12);
              win_blank();
              win_boolean('B',"Border N/S",a,0x20);
              win_boolean('d',"Border W/S",a,0x10);
              win_blank();
              win_command_esc(0,"OK") break;
            }
          }
        }
        break;
      case SC_BITS_0_LO:
        win_char('h',"Character: ",a);
        win_color('C',"Color: ",c);
        win_numeric('p',"Bit position: ",b,0,127);
        break;
    }
    win_blank();
    win_command_esc(0,"Done") {
      done=1;
      goto update;
    }
  }
}

static Uint8 set_mark(Uint8 x,Uint8 y,Uint8 mask) {
  Uint8*g;
  if(x>=80 || y>=25) return 0;
  g=markgrid+(x>>3)+y*10;
  if(*g&(1<<(x&7))) {
    if(!(mask&2)) *g&=~(1<<(x&7));
    return 1;
  } else {
    if(mask&1) *g|=1<<(x&7);
    return 0;
  }
}

static void esave(void) {
  FILE*fp=open_lump_by_number(scr_id,"SCR","w");
  if(fp) {
    save_screen(fp);
    fclose(fp);
  }
}

static void place_at(Uint8 x,Uint8 y,ScTile t) {
  Uint16 at=y*80+x;
  cur_screen.command[at]=t.com;
  cur_screen.color[at]=t.col;
  cur_screen.parameter[at]=t.par;
}

static Uint8 ask_numeric_digit_and_format(void) {
  Uint8 d=0;
  Uint8 f=0;
  win_form("Numeric digit/format") {
    win_numeric('D',"Digit position: ",d,0,15);
    win_numeric('F',"Format: ",f,0,15);
    win_blank();
    win_command_esc(0,"Done") break;
  }
  return (f<<4)|d;
}

typedef struct {
  Uint8 key,arg1,arg2;
  const char*text;
} MenuItem;

static void f_menu(Uint8 mnu) {
  static const MenuItem items[]={
    {0,1,0,"Backgrounds:"},
    {'O',SC_BACKGROUND|3,1,"Opaque"},
    {'S',SC_BACKGROUND|2,0,"Shadow"},
    {'T',SC_BACKGROUND|0,0,"Transparent"},
    {0,0,0,""},
    {0,0,0,"Tiles:"},
    {'1',SC_BOARD|0,0,"Dark"},
    {'2',SC_BOARD|1,1,"Dark+Background"},
    {'3',SC_BOARD|2,0,"Dark+Border"},
    {'4',SC_BOARD|3,1,"Dark+Border+Background"},
    {'5',SC_BOARD|4,0,"Light"},
    {'6',SC_BOARD|5,1,"Light+Background"},
    {'7',SC_BOARD|6,0,"Light+Border"},
    {'8',SC_BOARD|7,1,"Light+Border+Background"},
    {0,0,0,""},
    {0,0,0,"Miscellaneous:"},
    {'X',SC_TEXT,3,"Text window"},
    {'I',SC_ITEM,4,"Item window"},
    {'M',SC_MEMORY,6,"Memory"},
    {0,2,0,"Indicators:"},
    {'T',SC_IND_USERDATA,1,"Board user data"},
    {'U',SC_IND_CURSOR,1,"Cursor"},
    {'O',SC_IND_SCROLL_Y,1,"Scroll \x12"},
    {'L',SC_IND_SCROLL_X,1,"Scroll \x1D"},
    {'E',SC_IND_EXIT_E,1,"Exit East"},
    {'N',SC_IND_EXIT_N,1,"Exit North"},
    {'W',SC_IND_EXIT_W,1,"Exit West"},
    {'S',SC_IND_EXIT_S,1,"Exit South"},
    {'0',SC_IND_USER0,1,"User 0"},
    {'1',SC_IND_USER1,1,"User 1"},
    {'2',SC_IND_USER2,1,"User 2"},
    {'3',SC_IND_USER3,1,"User 3"},
    {0,0,0,""},
    {0,0,0,"Bits:"},
    {'A',SC_BITS_0_LO,5,"Bits of (A)"},
    {'B',SC_BITS_1_LO,5,"Bits of (B)"},
    {'C',SC_BITS_2_LO,5,"Bits of (C)"},
    {'D',SC_BITS_3_LO,5,"Bits of (D)"},
    {0,3,0,"Numbers:"},
    {'A',SC_NUMERIC|0x0,2,"(A)"},
    {'B',SC_NUMERIC|0x1,2,"(B)"},
    {'C',SC_NUMERIC|0x2,2,"(C)"},
    {'D',SC_NUMERIC|0x3,2,"(D)"},
    {'E',SC_NUMERIC|0x4,2,"(E)"},
    {'F',SC_NUMERIC|0x5,2,"(F)"},
    {'G',SC_NUMERIC|0x6,2,"(G)"},
    {'H',SC_NUMERIC|0x7,2,"(H)"},
    {'S',SC_NUMERIC|0x8,2,"(S)"},
    {'T',SC_NUMERIC|0x9,2,"(T)"},
    {'U',SC_NUMERIC|0xA,2,"(U)"},
    {'V',SC_NUMERIC|0xB,2,"(V)"},
    {'W',SC_NUMERIC|0xC,2,"(W)"},
    {'X',SC_NUMERIC|0xD,2,"(X)"},
    {'Y',SC_NUMERIC|0xE,2,"(Y)"},
    {'Z',SC_NUMERIC|0xF,2,"(Z)"},
    {0,4,0,"Numbers:"},
    {'1',SC_SPEC_PLAYER_X,2,"Player X"},
    {'2',SC_SPEC_PLAYER_Y,2,"Player Y"},
    {'3',SC_SPEC_CAMERA_X,2,"Camera X"},
    {'4',SC_SPEC_CAMERA_Y,2,"Camera Y"},
    {'5',SC_SPEC_TEXT_SCROLL_PERCENT,2,"Scroll %"},
    {'6',SC_SPEC_TEXT_LINE_NUMBER,2,"Line number"},
    {'7',SC_SPEC_TEXT_LINE_COUNT,2,"Line count"},
    {'8',SC_SPEC_CURRENT_BOARD,2,"Cur. board"},
    {'E',SC_SPEC_EXIT_E,2,"Exit East"},
    {'N',SC_SPEC_EXIT_N,2,"Exit North"},
    {'W',SC_SPEC_EXIT_W,2,"Exit West"},
    {'S',SC_SPEC_EXIT_S,2,"Exit South"},
    {'I',SC_SPEC_WIDTH,2,"Board width"},
    {'H',SC_SPEC_HEIGHT,2,"Board height"},
    {'Z',SC_SPEC_USERDATA,2,"Board user data"},
    {'C',SC_SPEC_CONTEXT_SPECIFIC,2,"Context-specific"},
    {0,255,0,0},
  };
  char buf[8]={};
  Uint32 i,j,k,q,y;
  draw_border(0x1B,20,1,60,23);
  v_ycur=25;
  for(i=0;;i++) {
    if(!items[i].key && items[i].arg1==255) return;
    if(!items[i].key && items[i].arg1==mnu) break;
  }
  q=i;
  for(y=3;;y++,i++) {
    if(items[i].key) {
      v_color[22+y*80]=v_color[23+y*80]=v_color[24+y*80]=(y&1?0x70:0x30);
      v_char[23+y*80]=items[i].key;
      draw_text(26,y,items[i].text,0x1E,-1);
    } else if(items[i].arg1==mnu || !items[i].arg1) {
      draw_text(22,y,items[i].text,0x1F,-1);
    } else {
      break;
    }
  }
  redisplay();
  do { if(!next_event()) return; } while(event.type!=SDL_KEYDOWN);
  k=event.key.keysym.unicode;
  if(!k) return;
  if(k>='a' && k<='z') k+='A'-'a';
  for(i=q+1;;i++) {
    if(items[i].key==k) break;
    if(items[i].arg1 && !items[i].key) return;
  }
  clip.com=items[i].arg1;
  switch(items[i].arg2) {
    case 0: clip.par=0; break;
    case 1: clip.par=ask_color_char(1,0); break;
    case 2: clip.par=ask_numeric_digit_and_format(); break;
    case 3: clip.par=ask_color_char(1,0); clip.com|=ask_color_char(0,clip.col)&15; break;
    case 5: ask_text("Bit position (0-31):",buf,2); clip.par=ask_color_char(1,0); clip.com|=strtol(buf,0,10)&31; break;
    case 6: ask_text("Address:",buf,7); i=(*buf=='$'?strtol(buf+1,0,16):strtol(buf,0,10)); clip.par=i; clip.col=i>>8; break;
  }
  place_at(xcur,ycur,clip);
}

static void cursor_move(Sint32 xd,Sint32 yd) {
  Sint32 x=xcur+xd*(numprefix?:1);
  Sint32 y=ycur+yd*(numprefix?:1);
  if(emode!=15) {
    if(x<0) xcur=0; else if(x>=80) xcur=79; else xcur=x;
    if(y<0) ycur=0; else if(y>=25) ycur=24; else ycur=y;
  } else {
    if(!numprefix) numprefix=1;
    while(numprefix-- && xcur+xd>=0 && xcur+xd<80 && ycur+yd>=0 && ycur+yd<25) {
      place_at(xcur+=xd,ycur+=yd,clip);
    }
  }
  numprefix=0;
}

static void far_cursor_move(Sint32 xd,Sint32 yd,char hi) {
  Uint32 w=board_info.width;
  Uint16 x0=xcur;
  Uint16 y0=ycur;
  Sint32 x,y;
  char r;
  repeat:
  r=1;
  x=xcur+xd*(numprefix?:1);
  y=ycur+yd*(numprefix?:1);
  numprefix=0;
  if(x<0) r=0,xcur=0; else if(x>=80) r=0,xcur=79; else xcur=x;
  if(y<0) r=0,ycur=0; else if(y>=25) r=0,ycur=24; else ycur=y;
  if(r && cur_screen.command[y0*80+x0]==cur_screen.command[y*80+x] && (!hi || cur_screen.color[y0*80+x0]==cur_screen.color[y*80+x])) goto repeat;
}

static void find_next_marked(Sint32 dir) {
  Sint16 x=xcur;
  Sint16 y=ycur;
  while(dir>0 && y<25) {
    if(++x==80) x=0,++y;
    if(set_mark(x,y,2)) --dir;
  }
  while(dir<0 && y>=0) {
    if(!x--) x=79,--y;
    if(set_mark(x,y,2)) ++dir;
  }
  if(x>=0 && x<80 && y>=0 && y<25) {
    xcur=x;
    ycur=y;
  }
}

static void digit_move(Sint32 xd,Sint32 yd) {
  int i,j;
  Sint32 x=xcur+xd;
  Sint32 y=ycur+yd;
  ScTile t={cur_screen.command[ycur*80+xcur],cur_screen.color[ycur*80+xcur],cur_screen.parameter[ycur*80+xcur],1};
  if((t.com&0xF0)==SC_MEMORY) {
    if(!numprefix) numprefix=1;
    while(numprefix-- && xcur+xd>=0 && xcur+xd<80 && ycur+yd>=0 && ycur+yd<25) {
      if(t.par==255) t.par=0,++t.col; else ++t.par;
      place_at(xcur+=xd,ycur+=yd,clip=t);
    }
  } else {
    if((t.com&0xF0)!=SC_NUMERIC && (t.com&0xF0)!=SC_NUMERIC_SPECIAL) return;
    if(!numprefix) numprefix=1;
    while(numprefix-- && xcur+xd>=0 && xcur+xd<80 && ycur+yd>=0 && ycur+yd<25) {
      if(!(++t.par&15)) {
        j=num_format[--t.par>>4].code;
        if(j==NF_METER) j=NF_METER_EXT; else if(j==NF_METER_HALF) j=NF_METER_HALF_EXT; else if(j==NF_BOARD_NAME) j=NF_BOARD_NAME_EXT; else return;
        for(i=0;i<16;i++) if(num_format[i].code==j && num_format[i].lead==num_format[t.par>>4].lead && num_format[i].mark==num_format[t.par>>4].mark && num_format[i].div==num_format[t.par>>4].div) break;
        if(i==16) break;
        t.par=i<<4;
      }
      place_at(xcur+=xd,ycur+=yd,clip=t);
    }
  }
  numprefix=0;
}

static void mass_move(Sint32 d) {
  Uint32 a;
  d*=numprefix?:1;
  if(d<0) {
    for(a=0;a<-d;a++) if(markgrid[a>>3]&(1<<(a&7))) return;
    for(a=0;a<2000+d;a++) if(markgrid[(a-d)>>3]&(1<<((a-d)&7))) {
      cur_screen.command[a]=cur_screen.command[a-d];
      cur_screen.color[a]=cur_screen.color[a-d];
      cur_screen.parameter[a]=cur_screen.parameter[a-d];
    }
    for(a=0;a<2000+d;a++) {
      markgrid[a>>3]&=~(1<<(a&7));
      if(markgrid[(a-d)>>3]&(1<<((a-d)&7))) markgrid[a>>3]|=1<<(a&7);
    }
  } else {
    for(a=1999;a>2000-d;a--) if(markgrid[a>>3]&(1<<(a&7))) return;
    for(a=1999;a>=d;a--) if(markgrid[(a-d)>>3]&(1<<((a-d)&7))) {
      cur_screen.command[a]=cur_screen.command[a-d];
      cur_screen.color[a]=cur_screen.color[a-d];
      cur_screen.parameter[a]=cur_screen.parameter[a-d];
    }
    for(a=1999;a>=d;a--) {
      markgrid[a>>3]&=~(1<<(a&7));
      if(markgrid[(a-d)>>3]&(1<<((a-d)&7))) markgrid[a>>3]|=1<<(a&7);
    }
  }
}

static Uint16 read_coordinate(char**p,Uint16 c) {
  Sint8 r=0;
  Uint16 o=0;
  if(**p=='+' || **p=='-') r=(**p=='-'?-1:1),++*p;
  if(**p<'0' || **p>'9') return c+r;
  while(**p>='0' && **p<='9') o=10*o+**p-'0',++*p;
  return o+c*r;
}

static Sint32 cctmp;
static ScTile cctile;
static Uint8*ccdata;
static Uint8 ccerror;

typedef struct {
  const char*name;
  char range;
  void(*full)(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg);
  void(*begin)(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg);
  void(*step)(Uint16 x,Uint16 y,const char*arg);
  void(*end)(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg);
} ColonCommand;

static int compare_coloncommand(const void*a,const void*b) {
  const ColonCommand*x=a;
  const ColonCommand*y=b;
  return strcmp(x->name,y->name);
}

static void cc_color_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  if(*arg) cctmp=strtol(arg,0,16); else cctmp=clip.col;
}

static void cc_color_step(Uint16 x,Uint16 y,const char*arg) {
  cur_screen.color[y*80+x]=cctmp;
}

static void cc_export(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  char buf[75];
  const char*e;
  FILE*fp;
  if(!*arg) return;
  if(*arg=='|') fp=popen(arg+1,"w"); else fp=fopen(arg,"wx");
  if(fp) {
    if(e=save_screen(fp)) alert_text(e);
    if(*arg=='|') pclose(fp); else fclose(fp);
  } else {
    snprintf(buf,75,"%m");
    alert_text(buf);
  }
}

static void cc_import(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  char buf[75];
  const char*e;
  FILE*fp;
  if(!*arg) return;
  if(*arg=='|') fp=popen(arg+1,"r"); else fp=fopen(arg,"r");
  if(fp) {
    if(e=load_screen(fp)) alert_text(e);
    if(*arg=='|') pclose(fp); else fclose(fp);
  } else {
    snprintf(buf,75,"%m");
    alert_text(buf);
  }
}

static void cc_mark_step(Uint16 x,Uint16 y,const char*arg) {
  set_mark(x,y,3);
}

static void cc_markonly_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  ccdata=calloc(1,250);
  if(!ccdata) err(1,"Allocation failed");
}

static void cc_markonly_step(Uint16 x,Uint16 y,const char*arg) {
  ccdata[(x>>3)+y*10]|=1<<(x&7);
}

static void cc_markonly_end(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  memcpy(markgrid,ccdata,250);
  free(ccdata);
}

static void cc_memory_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  cctmp=(*arg=='$'?strtol(arg+1,0,16):strtol(arg,0,10));
}

static void cc_memory_step(Uint16 x,Uint16 y,const char*arg) {
  cur_screen.command[y*80+x]=SC_MEMORY;
  cur_screen.color[y*80+x]=cctmp>>8;
  cur_screen.parameter[y*80+x]=cctmp;
  ++cctmp;
}

static void cc_mzmexport(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Uint16 x,y;
  char buf[75];
  FILE*fp;
  if(!*arg) return;
  if(*arg=='|') fp=popen(arg+1,"w"); else fp=fopen(arg,"w");
  if(fp) {
    if(x0>x1) x=x0,x0=x1,x1=x;
    if(y0>y1) y=y0,y0=y1,y1=y;
    fwrite("MZM2",1,4,fp);
    fputc(x1+1-x0,fp); fputc(0,fp); fputc(y1+1-y0,fp);
    fwrite("\0\0\0\0\0\0\x01\0",1,9,fp);
    for(y=y0;y<=y1;y++) for(x=x0;x<=x1;x++) {
      fputc(cur_screen.parameter[y*80+x],fp);
      fputc(cur_screen.color[y*80+x],fp);
    }
    if(*arg=='|') pclose(fp); else fclose(fp);
  } else {
    snprintf(buf,75,"%m");
    alert_text(buf);
  }
}

static void cc_mzmimport(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  int c,m;
  Uint16 x,y,w,h;
  char buf[75];
  FILE*fp;
  if(!*arg) return;
  if(*arg=='|') fp=popen(arg+1,"r"); else fp=fopen(arg,"r");
  if(fp) {
    if(fgetc(fp)!='M' || fgetc(fp)!='Z' || fgetc(fp)!='M') goto error;
    if(x0>x1) m=x0,x0=x1,x1=m;
    if(y0>y1) m=y0,y0=y1,y1=m;
    switch(c=fgetc(fp)) {
      case 'X': w=read8(fp); h=read8(fp); m=0; fread(buf,1,10,fp); break;
      case '2': case '3': w=read16(fp); h=read16(fp); fread(buf,1,5,fp); m=read8(fp); fread(buf,1,c=='3'?6:2,fp); break;
      default: goto error;
    }
    if(m&~1) goto error;
    if(w==x1+1-x0 && h==y1+1-y0) {
      for(y=y0;y<=y1;y++) for(x=x0;x<=x1;x++) {
        cur_screen.command[y*80+x]=m?3:read8(fp);
        cur_screen.parameter[y*80+x]=read8(fp);
        cur_screen.color[y*80+x]=read8(fp);
        if(!m) fread(buf,1,3,fp);
      }
    } else {
      snprintf(buf,75,"MZM is wrong size; expected %dx%d but found %dx%d",x1+1-x0,y1+1-y0,w,h);
      alert_text(buf);
    }
    if(0) error: alert_text("Error loading MZM");
    if(*arg=='|') pclose(fp); else fclose(fp);
  } else {
    snprintf(buf,75,"%m");
    alert_text(buf);
  }
}

static void cc_place_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  cctile=clip;
}

static void cc_place_step(Uint16 x,Uint16 y,const char*arg) {
  place_at(x,y,cctile);
}

static void cc_rawexport(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  // This command is only used for testing and is not documented.
  char buf[75];
  FILE*fp;
  if(!*arg) return;
  if(*arg=='|') fp=popen(arg+1,"w"); else fp=fopen(arg,"w");
  if(fp) {
    fwrite(&cur_screen,1,sizeof(Screen),fp);
    if(*arg=='|') pclose(fp); else fclose(fp);
  } else {
    snprintf(buf,75,"%m");
    alert_text(buf);
  }
}

static void cc_status(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  if(*arg=='1') status_on=1;
  if(*arg=='0') status_on=0;
  if(!*arg) status_on=!status_on;
}

static void cc_toggle_step(Uint16 x,Uint16 y,const char*arg) {
  set_mark(x,y,1);
}

static void cc_unmark_step(Uint16 x,Uint16 y,const char*arg) {
  set_mark(x,y,0);
}

static const ColonCommand colon_commands[]={
  {"c",'.',0,cc_color_begin,cc_color_step,0},
  {"color",'.',0,cc_color_begin,cc_color_step,0},
  {"ex",0,cc_export,0,0,0},
  {"export",0,cc_export,0,0,0},
  {"im",0,cc_import,0,0,0},
  {"import",0,cc_import,0,0,0},
  {"m",'.',0,0,cc_mark_step,0},
  {"mark",'.',0,0,cc_mark_step,0},
  {"markonly",'.',0,cc_markonly_begin,cc_markonly_step,cc_markonly_end},
  {"mem",'.',0,cc_memory_begin,cc_memory_step,0},
  {"memory",'.',0,cc_memory_begin,cc_memory_step,0},
  {"mo",'.',0,cc_markonly_begin,cc_markonly_step,cc_markonly_end},
  {"mzmex",'%',cc_mzmexport,0,0,0},
  {"mzmexport",'%',cc_mzmexport,0,0,0},
  {"mzmim",'%',cc_mzmimport,0,0,0},
  {"mzmimport",'%',cc_mzmimport,0,0,0},
  {"p",'.',0,cc_place_begin,cc_place_step,0},
  {"place",'.',0,cc_place_begin,cc_place_step,0},
  {"rawexport",0,cc_rawexport,0,0,0},
  {"rex",0,cc_rawexport,0,0,0},
  {"status",0,cc_status,0,0,0},
  {"t",'.',0,0,cc_toggle_step,0},
  {"toggle",'.',0,0,cc_toggle_step,0},
  {"u",'.',0,0,cc_unmark_step,0},
  {"unmark",'.',0,0,cc_unmark_step,0},
};

static void do_colon_command(char*text) {
  char*nex;
  ColonCommand key;
  ColonCommand*found;
  Uint8 ra=0;
  Uint16 x0=xcur;
  Uint16 y0=ycur;
  Uint16 x1=xcur;
  Uint16 y1=ycur;
  Uint16 x,y,ox,oy;
  int i;
  if(*text=='~' && text[1]=='&') {
    ra='~';
    x0=y0=0;
    x1=79;
    y1=24;
    text+=2;
  } else if(*text=='&' || *text=='%') {
    ra=*text++;
    x0=y0=0;
    x1=79;
    y1=24;
  } else if(*text=='.') {
    ra='%';
    text++;
  }
  if((*text>='0' && *text<='9') || *text=='-' || *text=='+' || *text==',') {
    if(!ra) ra='%'; else if(ra=='%') { alert_text("Improper coordinates"); return; }
    x0=read_coordinate(&text,xcur);
    if(*text++!=',') goto syntax;
    y0=read_coordinate(&text,ycur);
    xy1:
    if(*text==':') {
      text++;
      if(emode=='v' && (*text=='<' || *text=='>')) {
        if(*text=='<') x1=xcur2,y1=ycur2;
        if(*text=='>') x1=xcur,y1=ycur;
        text++;
      } else {
        x1=read_coordinate(&text,xcur);
        if(*text++!=',') goto syntax;
        y1=read_coordinate(&text,ycur);
      }
    } else {
      x1=x0,y1=y0;
    }
  } else if(emode=='v' && (*text=='<' || *text=='>')) {
    if(!ra) ra='%'; else if(ra=='%') { alert_text("Improper coordinates"); return; }
    if(*text=='<') x0=xcur2,y0=ycur2;
    if(*text=='>') x0=xcur,y0=ycur;
    text++;
    goto xy1;
  }
  if(*text==' ') ++text;
  key.name=text;
  text=strchrnul(text,' ');
  if(*text) *text++=0;
  found=bsearch(&key,colon_commands,sizeof(colon_commands)/sizeof(ColonCommand),sizeof(ColonCommand),compare_coloncommand);
  if(!found) { alert_text("Improper command"); return; }
  if(ra && !found->range) { alert_text("Improper use of range"); return; }
  if(!ra) {
    ra=found->range;
    if(ra=='.') ra='%'; else x0=y0=0,x1=79,y1=24;
  }
  ccerror=0;
  if(ra=='~' || ra=='&') {
    if(found->begin) found->begin(x0,y0,x1,y1,text);
    for(x=x0,y=y0;;) {
      if(ccerror) return;
      ox=x,oy=y;
      if(ra=='&') {
        if(x>=80 || y>=25) goto nomatch;
        if(!(markgrid[y*10+(x>>3)]&(1<<(x&7)))) goto nomatch;
      } else if(ra=='~' && x<80 && y<25) {
        if(markgrid[y*10+(x>>3)]&(1<<(x&7))) goto nomatch;
      }
      if(x>=80 || y>=25) goto nomatch;
      found->step(x,y,text);
      nomatch:
      x=ox,y=oy;
      if(x==x1) {
        x=x0;
        if(y==y1) break;
        if(y1>y) y++; else y--;
      } else {
        if(x1>x) x++; else x--;
      }
    }
    if(ccerror) return;
    if(found->end) found->end(x0,y0,x1,y1,text);
  } else if(found->full) {
    found->full(x0,y0,x1,y1,text);
  } else {
    if(found->begin) found->begin(x0,y0,x1,y1,text);
    for(x=x0,y=y0;;) {
      if(ccerror) return;
      found->step(x,y,text);
      if(x==x1) {
        x=x0;
        if(y==y1) break;
        if(y1>y) y++; else y--;
      } else {
        if(x1>x) x++; else x--;
      }
    }
    if(ccerror) return;
    if(found->end) found->end(x0,y0,x1,y1,text);
  }
  return;
  syntax: alert_text("Syntax error"); return;
}

static void ask_colon_command(void) {
  char text[75]="";
  if(emode=='v') strcpy(text,"<:>");
  if(emode=='m') strcpy(text,"&");
  ask_text(":",text,74);
  if(*text) {
    do_colon_command(text);
    if(emode=='v') emode=0;
  }
}

static void update_e_screen(void) {
  Uint32 at;
  Uint8 com,col,par,x,y;
  for(at=x=y=0;at<80*25;at++,x++) {
    if(x==80) x=0,++y;
    com=cur_screen.command[at];
    col=cur_screen.color[at];
    par=cur_screen.parameter[at];
    switch(viewmode) {
      case 0:
        v_char[at]=par;
        v_color[at]=col;
        break;
      case 1:
        v_char[at]="\xF9\xB1##MiTI!!!!!!!!"[com>>4];
        v_color[at]=0x07;
        if(!com) v_color[at]=0x08;
        if(x==cur_screen.message_x && y==cur_screen.message_y) v_color[at]+=2;
        if(x==cur_screen.view_x && y==cur_screen.view_y) v_color[at]+=4;
        if(x>=cur_screen.soft_edge[DIR_W] && x<=cur_screen.soft_edge[DIR_E] && y>=cur_screen.soft_edge[DIR_N] && y<=cur_screen.soft_edge[DIR_S]) v_color[at]|=0x18;
        if(x>=cur_screen.hard_edge[DIR_W] && x<=cur_screen.hard_edge[DIR_E] && y>=cur_screen.hard_edge[DIR_N] && y<=cur_screen.hard_edge[DIR_S]) v_color[at]|=0x28;
        if(y==cur_screen.message_y && x>=cur_screen.message_l && x<=cur_screen.message_r) v_color[at]|=0x48;
        break;
    }
    if(markgrid[(x>>3)+y*10]&(1<<(x&7))) v_color[at]=viewmode?~v_color[at]:config.mark_color?:~v_color[at];
  }
}

static void show_selection(void) {
  Sint16 x0=xcur;
  Sint16 y0=ycur;
  Sint16 x1=xcur2;
  Sint16 y1=ycur2;
  Sint16 z,x;
  if(x0>x1) z=x0,x0=x1,x1=z;
  if(y0>y1) z=y0,y0=y1,y1=z;
  if(x0<0) x0=0;
  if(y0<0) y0=0;
  if(x1>79) x1=79;
  if(y1>24) y1=24;
  for(z=y0*80;y0<=y1;y0++,z+=80) for(x=x0;x<=x1;x++) v_color[z+x]=(config.block_color?:~v_color[z+x]),v_char[z+x]=0xB0;
}

static void estatus(void) {
  char buf[80];
  int y=(ycur>12?0:24);
  memset(v_color+y*80,0x11,80);
  draw_text(0,y,buf,0x1A,snprintf(buf,80,"%5d",scr_id));
  draw_text(7,y,"<\xFE>",0x17,3);
  v_color[y*80+8]=clip.col;
  draw_text(12,y,buf,0x17,snprintf(buf,80,"<%02X,%02X,%02X>",clip.com,clip.col,clip.par));
  if(numprefix) draw_text(34,y,buf,0x1E,snprintf(buf,80,"%5d",numprefix));
  if(markgrid[(xcur>>3)+ycur*10]&(1<<(xcur&7))) v_char[y*80+39]=7,v_color[y*80+39]=0x1A;
  v_char[y*80+42]=emode;
  v_color[y*80+42]=0x1C;
  draw_text(69,y,buf,0x19,snprintf(buf,80,"(%4d,%4d)",xcur,ycur));
  if(emode=='v') {
    draw_text(59,y,buf,0x19,snprintf(buf,80,"%c%04d%c%04d",xcur2>xcur?'-':'+',abs(xcur2-xcur),ycur2>ycur?'-':'+',abs(ycur2-ycur)));
  }
}

static int shifted_arrows(void) {
  Uint16 m=event.key.keysym.mod;
  Uint16 k=event.key.keysym.sym;
  m=(m&KMOD_ALT?config.alt_arrows:m&KMOD_CTRL?config.ctrl_arrows:0);
  if(!m) return 0;
  numprefix=(numprefix?:1)*(m&255);
  switch(m>>8) {
    case '-': return 0;
    case 'm': set_mark(xcur,ycur,3); return 0;
    default: return 1;
  }
}

Uint16 edit_screen(Uint16 id) {
  int i;
  Sint32 k;
  if(status_on==255) status_on=config.scr_edit_status;
  goto_screen(id);
  v_status[1]='S';
  emode=0;
  for(;;) {
    update_e_screen();
    v_xcur=xcur; v_ycur=ycur;
    if(emode=='v') show_selection();
    if(status_on) estatus();
    redisplay();
    do { if(!next_event()) goto exit; } while(event.type!=SDL_KEYDOWN);
    k=(!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym;
    if((event.key.keysym.mod&(KMOD_ALT|KMOD_CTRL)) && (k==-SDLK_UP || k==-SDLK_DOWN || k==-SDLK_LEFT || k==-SDLK_RIGHT) && shifted_arrows()) continue;
    switch(emode) {
      case 0: case 15: no_mode: switch(k) {
        case 0x08: numprefix/=10; break;
        case 0x09: if(emode=(emode?0:15)) place_at(xcur,ycur,clip); break;
        case 0x11: load_font(0,LOADFONT_RESET); load_palette(0,LOADPAL_RESET); break;
        case 0x16: viewmode^=1; break;
        case 0x1A: xcur=cur_screen.view_x; ycur=cur_screen.view_y; break;
        case 0x1B: if(numprefix) numprefix=0; else if(emode) emode=0; else goto exit; break;
        case '0' ... '9': if((i=numprefix*10+k-'0')<65536) numprefix=i; break;
        case -SDLK_i: edit_screen_info(); break;
        case -SDLK_l: cur_screen.message_l=xcur; break;
        case -SDLK_m: cur_screen.message_x=xcur; cur_screen.message_y=ycur; break;
        case -SDLK_r: cur_screen.message_r=xcur; break;
        case -SDLK_v: cur_screen.view_x=xcur; cur_screen.view_y=ycur; break;
        case -SDLK_w: edit_window(); break;
        case -SDLK_y: edit_varprop(&cur_screen.varprop); work_varproperties(&cur_screen.varprop); break;
        case -SDLK_z: numprefix=0xFFFF; break;
        case ' ': set_mark(xcur,ycur,1); break;
        case 'c': case 0x03: clip.col=ask_color_char(0,clip.col); break;
        case 'C': cur_screen.color[ycur*80+xcur]=ask_color_char(0,cur_screen.color[ycur*80+xcur]); break;
        case 'd': case 0x7F: place_at(xcur,ycur,(ScTile){SC_BACKGROUND,0,0,0}); break;
        case 'e': edit_tile(); break;
        case 'h': case -SDLK_LEFT: if(event.key.keysym.mod&KMOD_SHIFT) goto shift_h; cursor_move(-1,0); break;
        case 'H': shift_h: digit_move(-1,0); break;
        case 'i': do_colon_command("%toggle"); break;
        case 'j': case -SDLK_DOWN: if(event.key.keysym.mod&KMOD_SHIFT) goto shift_j; cursor_move(0,1); break;
        case 'J': shift_j: digit_move(0,1); break;
        case 'k': case -SDLK_UP: if(event.key.keysym.mod&KMOD_SHIFT) goto shift_k; cursor_move(0,-1); break;
        case 'K': shift_k: digit_move(0,-1); break;
        case 'l': case -SDLK_RIGHT: if(event.key.keysym.mod&KMOD_SHIFT) goto shift_l; cursor_move(1,0); break;
        case 'L': shift_l: digit_move(1,0); break;
        case 'm': emode='m'; break;
        case 'n': find_next_marked(numprefix?:1); numprefix=0; break;
        case 'N': find_next_marked(-(numprefix?:1)); numprefix=0; break;
        case 'p': place_at(xcur,ycur,clip); break;
        case 'r': clip.col=(clip.col<<4)|(clip.col>>4); break;
        case 't': emode='t'; xcur2=xcur; break;
        case 'u': unmark: if(emode!=15) emode=0; memset(markgrid,0,250); break;
        case 'v': xcur2=xcur; ycur2=ycur; emode='v'; break;
        case 'y': clip.com=cur_screen.command[ycur*80+xcur]; clip.col=cur_screen.color[ycur*80+xcur]; clip.par=cur_screen.parameter[ycur*80+xcur]; break;
        case 'z': case 'Z': emode=k; break;
        case '<': case -SDLK_HOME: xcur=ycur=0; break;
        case '>': case -SDLK_END: xcur=79; ycur=24; break;
        case ':': ask_colon_command(); break;
        case -SDLK_F1: f_menu(1); break;
        case -SDLK_F2: f_menu(2); break;
        case -SDLK_F3: f_menu(3); break;
        case -SDLK_F4: f_menu(4); break;
        case -SDLK_SLASH: case -SDLK_QUESTION: online_help("editscr",0); break;
      } break;
      case 'm': switch(k) {
        case 'c': do_colon_command("&color"); goto unmark;
        case 'C': do_colon_command("&color"); emode=0; break;
        case 'p': do_colon_command("&place"); goto unmark;
        case 'P': do_colon_command("&place"); emode=0; break;
        case 'h': case -SDLK_LEFT: mass_move(-1); cursor_move(-1,0); break;
        case 'j': case -SDLK_DOWN: mass_move(80); cursor_move(0,1); break;
        case 'k': case -SDLK_UP: mass_move(-80); cursor_move(0,-1); break;
        case 'l': case -SDLK_RIGHT: mass_move(1); cursor_move(1,0); break;
        default: goto no_mode;
      } break;
      case 't': switch(k) {
        case 0x08: if(xcur) --xcur; break;
        case 0x0A: case 0x0D: xcur=xcur2; if(ycur<24) ++ycur; break;
        case 0x10: if(k=ask_color_char(1,' ')) goto text; break;
        case 0x20 ... 0x7E: text: place_at(xcur,ycur,(ScTile){SC_BACKGROUND|3,clip.col,k,0}); if(xcur<79) ++xcur; break;
        default: goto no_mode;
      } break;
      case 'v': switch(k) {
        case -SDLK_h: set_edges(cur_screen.hard_edge); emode=0; break;
        case -SDLK_s: set_edges(cur_screen.soft_edge); emode=0; break;
        case 'c': do_colon_command("<:>unmark"); emode=0; break;
        case 'i': case ' ': do_colon_command("<:>toggle"); emode=0; break;
        case 'm': do_colon_command("<:>mark"); emode='m'; break;
        case 's': case 0x0D: do_colon_command("<:>mark"); emode=0; break;
        case ';': i=xcur; xcur=xcur2; xcur2=i; i=ycur; ycur=ycur2; ycur2=i; break;
        case 'b': make_border(0); emode=0; break;
        case 'B': make_border(8); emode=0; break;
        default: goto no_mode;
      } break;
      case 'z': case 'Z': switch(k) {
        case 'h': case -SDLK_LEFT: far_cursor_move(-1,0,emode=='Z'); break;
        case 'j': case -SDLK_DOWN: far_cursor_move(0,1,emode=='Z'); break;
        case 'k': case -SDLK_UP: far_cursor_move(0,-1,emode=='Z'); break;
        case 'l': case -SDLK_RIGHT: far_cursor_move(1,0,emode=='Z'); break;
      } emode=numprefix=0; break;
      default: emode=0;
    }
  }
  exit:
  v_status[1]=0;
  esave();
  return scr_id;
}

