#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 editbrd.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

static Uint16 brd_id;
static Uint16 xcur,ycur;
static Uint8 status_on=255;
static Tile clip;
static Uint8 apparent_clip;
static Uint16 numprefix;
static Uint8 emode;
static Uint8*markgrid;
static Uint16 markwidth,markheight,markskip;

void set_board_name(Uint16 id,const char*name) {
  if(maxboard<id) {
    boardnames=realloc(boardnames,(id+1)*sizeof(Uint8*));
    if(!boardnames) err(1,"Allocation failed");
    while(maxboard<id) boardnames[++maxboard]=0;
  } else if(!boardnames) {
    boardnames=calloc(maxboard+1,sizeof(Uint8*));
    if(!boardnames) err(1,"Allocation failed");
  }
  free(boardnames[id]);
  boardnames[id]=strdup(name);
  if(!boardnames[id]) err(1,"Allocation failed");
}

static Uint8 set_mark(Uint16 x,Uint16 y,Uint8 mask) {
  Uint8*g0;
  Uint16 w0,h0,s0;
  Uint32 i;
  if(x>=board_info.width || y>=board_info.height) return 0;
  if(!markgrid || x>=markwidth || y>=markheight) {
    if(g0=markgrid) {
      w0=markwidth;
      h0=markheight;
      s0=markskip;
    }
    markwidth=board_info.width;
    markheight=board_info.height;
    markskip=(markwidth+7)>>3;
    markgrid=calloc(markskip,markheight);
    if(!markgrid) err(1,"Allocation failed");
    if(g0) {
      for(i=0;i<h0;i++) memcpy(markgrid+i*markskip,g0+i*s0,s0);
      free(g0);
    }
  }
  g0=markgrid+(x>>3)+y*markskip;
  if(*g0&(1<<(x&7))) {
    if(!(mask&2)) *g0&=~(1<<(x&7));
    return 1;
  } else {
    if(mask&1) *g0|=1<<(x&7);
    return 0;
  }
}

static void goto_board(Uint16 id) {
  FILE*fp=open_lump_by_number(id,"BRD","r");
  char b=0;
  int i;
  if(!fp) b=1,fp=open_lump_by_number(config.template_board,"BRD","r");
  if(fp) {
    load_board(fp);
    fclose(fp);
    if(b) {
      memset(b_under,0,board_info.width*board_info.height);
      memset(b_main,0,board_info.width*board_info.height);
      memset(b_under,0,board_info.width*board_info.height);
      for(i=0;i<maxstat;i++) {
        free(stats[i].text);
        free(stats[i].xy);
        stats[i].text=0;
        stats[i].xy=0;
        stats[i].length=0;
        stats[i].count=0;
      }
      maxstat=1;
    }
  } else {
    free(b_under);
    b_under=calloc(25*60,3*sizeof(Tile));
    if(!b_under) err(1,"Allocation failed");
    b_main=b_under+25*60;
    b_over=b_main+25*60;
    board_info.width=60;
    board_info.height=25;
    board_info.screen=0;
    board_info.exits[0]=board_info.exits[1]=0;
    board_info.exits[2]=board_info.exits[3]=0;
    board_info.userdata=board_info.flag=0;
    for(i=0;i<maxstat;i++) {
      free(stats[i].text);
      free(stats[i].xy);
    }
    free(stats);
    stats=calloc(1,sizeof(Stat));
    if(!stats) err(1,"Allocation failed");
    maxstat=1;
  }
  if(xcur>=board_info.width || ycur>=board_info.height) xcur=ycur=0;
  brd_id=id;
}

static void edit_board_info(void) {
  int i;
  Uint16 n;
  char nam[61]="";
  if(brd_id<=maxboard && boardnames && boardnames[brd_id]) strncpy(nam,boardnames[brd_id],60);
  nam[60]=0;
  win_form("Board info") {
    win_text('m',"Board name: ",nam);
    win_picture(1) {
      char buf[40];
      draw_text(1,0,buf,7,snprintf(buf,40,"Dimensions: %dx%d",board_info.width,board_info.height));
    }
    win_numeric('U',"User data: ",board_info.userdata,0,65535);
    win_numeric('c',"Screen: ",board_info.screen,0,65535);
    win_boolean('0',"User flag 0",board_info.flag,BF_USER0);
    win_boolean('1',"User flag 1",board_info.flag,BF_USER1);
    win_boolean('2',"User flag 2",board_info.flag,BF_USER2);
    win_boolean('3',"User flag 3",board_info.flag,BF_USER3);
    win_boolean('P',"Persist",board_info.flag,BF_PERSIST);
    win_boolean('g',"Suppress global scripts",board_info.flag,BF_NO_GLOBAL);
    win_boolean('V',"Visible overlay",board_info.flag,BF_OVERLAY);
    win_blank();
    win_numeric('E',"East exit:  ",board_info.exits[DIR_E],0,65535) win_refresh();
    win_numeric('N',"North exit: ",board_info.exits[DIR_N],0,65535) win_refresh();
    win_numeric('W',"West exit:  ",board_info.exits[DIR_W],0,65535) win_refresh();
    win_numeric('S',"South exit: ",board_info.exits[DIR_S],0,65535) win_refresh();
    win_blank();
    win_picture(4) {
      for(i=0;i<4;i++) {
        n=board_info.exits[i];
        v_char[i*80]="\x1A\x18\x1B\x19"[i];
        v_color[i*80]=(n?14:8);
        if(n && boardnames && n<=maxboard && boardnames[n]) draw_text(2,i,boardnames[n],7,60);
      }
    }
    win_blank();
    win_command_esc(0,"Done") break;
  }
  if(*nam) set_board_name(brd_id,nam);
}

static void clear_extra_stats(void) {
  int i,j;
  for(i=0;i<maxstat;i++) for(j=0;j<stats[i].count;) {
    if(stats[i].xy[j].x>=board_info.width || stats[i].xy[j].y>=board_info.height) {
      memmove(stats[i].xy+j,stats[i].xy+j+1,(stats[i].count-j-1)*sizeof(StatXY));
      --stats[i].count;
    } else {
      j++;
    }
  }
}

static void edit_tile(void) {
  char lay=1;
  Tile*p;
  win_form("Tile") {
    win_numeric('X',"X: ",xcur,0,board_info.width-1) win_refresh();
    win_numeric('Y',"Y: ",ycur,0,board_info.height-1) win_refresh();
    win_option('U',"Under layer",lay,0) win_refresh();
    win_option('M',"Main layer",lay,1) win_refresh();
    win_option('O',"Over layer",lay,2) win_refresh();
    win_blank();
    p=(lay==0?b_under:lay==1?b_main:b_over)+xcur+ycur*board_info.width;
    win_numeric('K',"Kind: ",p->kind,0,255);
    win_color('C',"Color: ",p->color);
    win_numeric('P',"Parameter: ",p->param,0,255);
    win_char('h',"Character: ",p->param) win_refresh();
    win_picture(1) {
      char buf[40];
      draw_text(0,0,buf,7,snprintf(buf,40,"Stat: %3d",p->stat));
      if(!p->stat) v_color[8]=8;
    }
    win_blank();
    win_command_esc(0,"Done") break;
  }
}

static void resize_board(void) {
  static Uint8 m=0;
  static Uint8 b=0;
  Uint16 w=board_info.width;
  Uint16 h=board_info.height;
  Tile*ku=b_under;
  Tile*km=b_main;
  Tile*ko=b_over;
  Uint32 x,y,z;
  Sint32 ox,oy;
  win_form("Resize board") {
    win_picture(1) {
      char buf[40];
      draw_text(1,0,buf,7,snprintf(buf,40,"Current: %dx%d",board_info.width,board_info.height));
    }
    win_numeric('W',"Width: ",w,1,9999);
    win_numeric('H',"Height: ",h,1,9999);
    win_heading("Do with existing grid:");
    win_option('N',"Northwest",m,0);
    win_option('o',"Northeast",m,1);
    win_option('S',"Southwest",m,2);
    win_option('u',"Southeast",m,3);
    win_option('C',"Center",m,4);
    win_option('T',"Tile",m,5);
    win_option('d',"Tile with duplicate stats",m,6);
    win_boolean('F',"Fill main with current",b,1);
    win_boolean('v',"Fill overlay with (0,0)",b,2);
    win_blank();
    win_command('p',"Use cursor position") {
      w=xcur+1;
      h=ycur+1;
      win_refresh();
    }
    win_command('x',"Execute") break;
    win_command_esc(0,"Cancel") return;
  }
  if(w==board_info.width && h==board_info.height) return;
  b_under=calloc(w*h,3*sizeof(Tile));
  if(!b_under) err(1,"Allocation failed");
  b_main=b_under+w*h;
  b_over=b_main+w*h;
  if(m>4) {
    for(x=0;x<w;x++) for(y=0;y<h;y++) {
      z=x%board_info.width+(y%board_info.height)*board_info.width;
      b_under[x+y*w]=ku[z];
      b_main[x+y*w]=km[z];
      b_over[x+y*w]=ko[z];
      if(m==5 && x>=board_info.width || y>=board_info.height) b_under[z].stat=b_main[z].stat=b_over[z].stat=0;
    }
    if(m==6) {
      ox=(w/board_info.width)+(w%board_info.width?1:0);
      oy=(h/board_info.height)+(h%board_info.height?1:0);
      for(x=0;x<maxstat;x++) if(stats[x].count) {
        z=stats[x].count;
        stats[x].xy=realloc(stats[x].xy,(stats[x].count*=ox*oy)*sizeof(StatXY));
        if(!stats[x].xy) err(1,"Allocation failed");
        for(y=z;y<stats[x].count;y++) {
          stats[x].xy[y]=stats[x].xy[y%z];
          stats[x].xy[y].x+=board_info.width*((y/z)%ox);
          stats[x].xy[y].y+=board_info.height*((y/z)/ox);
        }
      }
    }
  } else {
    if(b&1) for(x=0;x<w*h;x++) b_main[x]=clip,b_main[x].stat=0;
    if(b&2) for(x=0;x<w*h;x++) b_over[x]=*ko,b_over[x].stat=0;
    switch(m) {
      case 0: ox=oy=0; break;
      case 1: ox=w-board_info.width; oy=0; break;
      case 2: ox=0; oy=h-board_info.height; break;
      case 3: ox=w-board_info.width; oy=h-board_info.height; break;
      case 4: ox=(w-board_info.width)/2; oy=(h-board_info.height)/2; break;
    }
    for(x=0;x<maxstat;x++) for(y=0;y<stats[x].count;y++) {
      stats[x].xy[y].x+=ox;
      stats[x].xy[y].y+=oy;
    }
    xcur+=ox; ycur+=oy;
    if(xcur>=w) xcur=0;
    if(ycur>=h) ycur=0;
    ox=-ox; oy=-oy;
    for(x=0;x<board_info.width;x++) for(y=0;y<board_info.height;y++) {
      if(x+ox<0 || x+ox>=w || y+oy<0 || y+oy>=h) continue;
      z=x+ox+(y+oy)*board_info.width;
      b_under[x+y*w]=ku[z];
      b_main[x+y*w]=km[z];
      b_over[x+y*w]=ko[z];
    }
  }
  board_info.width=w;
  board_info.height=h;
  free(ku);
  clear_extra_stats();
}

static void set_board_editor_screen(void) {
  memset(cur_screen.command,SC_BOARD+5,80*25);
  memset(cur_screen.color,0x01,80*25);
  memset(cur_screen.parameter,177,80*25);
  cur_screen.view_x=39;
  cur_screen.view_y=12;
  cur_screen.message_x=cur_screen.message_y=180;
  cur_screen.flag=0;
  cur_screen.soft_edge[DIR_W]=cur_screen.hard_edge[DIR_W]=0;
  cur_screen.soft_edge[DIR_N]=cur_screen.hard_edge[DIR_N]=0;
  cur_screen.soft_edge[DIR_E]=cur_screen.hard_edge[DIR_E]=79;
  cur_screen.soft_edge[DIR_S]=cur_screen.hard_edge[DIR_S]=24;
}

static void esave(void) {
  FILE*fp=open_lump_by_number(brd_id,"BRD","w");
  if(fp) {
    save_board(fp,0);
    fclose(fp);
  }
}

static inline void switch_to_board(Sint32 id) {
  FILE*fp;
  if(id<0) return;
  esave();
  fp=open_lump_by_number(id,"BRD","r");
  if(!fp) return;
  load_board(fp);
  fclose(fp);
  if(xcur>=board_info.width || ycur>=board_info.height) xcur=ycur=0;
  brd_id=id;
}

static void escroll(void) {
  if(xcur>scroll_x+79) {
    scroll_x+=config.editor_scroll_x;
    if(xcur>scroll_x+79) scroll_x=xcur-79;
  } else if(xcur<scroll_x) {
    scroll_x-=config.editor_scroll_x;
    if(xcur<scroll_x) scroll_x=xcur;
  }
  if(ycur>scroll_y+24) {
    scroll_y+=config.editor_scroll_y;
    if(ycur>scroll_y+24) scroll_y=ycur-24;
  } else if(ycur<scroll_y) {
    scroll_y-=config.editor_scroll_y;
    if(ycur<scroll_y) scroll_y=ycur;
  }
  if(scroll_x+79>board_info.width) scroll_x=board_info.width-79;
  if(scroll_y+24>board_info.height) scroll_y=board_info.height-24;
  if(scroll_x<0) scroll_x=0;
  if(scroll_y<0) scroll_y=0;
}

static void set_apparent_clip(void) {
  Uint8 c=elem_def[clip.kind].app[0];
  Uint8 d=elem_def[clip.kind].app[1];
  if(c&0x20) {
    apparent_clip=appearance_mapping[((d&0x7E)+((clip.param>>(c&7))&"\x01\x03\x07\x0F"[(c>>3)&3]))&0x7F];
  } else switch(c&0x1F) {
    case AP_FIXED: case AP_UNDER: apparent_clip=d; break;
    case AP_PARAM: apparent_clip=d+clip.param; break;
    case AP_LINES: apparent_clip=appearance_mapping[(d&0x70)|0x0F]; break;
    case AP_ANIMATE: apparent_clip=appearance_mapping[(animation[d&3].step[0]+(d&0x7C))&0x7F]; break;
    case AP_MISC1: if(clip.stat && clip.stat<=maxstat) apparent_clip=stats[clip.stat-1].misc1; else apparent_clip=d; break;
    case AP_MISC2: if(clip.stat && clip.stat<=maxstat) apparent_clip=stats[clip.stat-1].misc2; else apparent_clip=d; break;
    case AP_MISC3: if(clip.stat && clip.stat<=maxstat) apparent_clip=stats[clip.stat-1].misc3; else apparent_clip=d; break;
    default: apparent_clip='?';
  }
}

static void estatus(void) {
  char buf[80];
  int y=24;
  if(board_info.height>24 && v_ycur>12) y=0;
  memset(v_color+y*80,0x11,80);
  draw_text(0,y,buf,0x1B,snprintf(buf,80,"%5d",brd_id));
  v_color[y*80+5]=v_color[y*80+6]=0x12;
  v_char[y*80+5]="\xFA\x18\x19\x12"[(board_info.exits[DIR_N]?1:0)+(board_info.exits[DIR_S]?2:0)];
  v_char[y*80+6]="\xFA\x1A\x1B\x1D"[(board_info.exits[DIR_E]?1:0)+(board_info.exits[DIR_W]?2:0)];
  draw_text(7,y,"<\xFE>",0x17,3);
  v_color[y*80+8]=clip.color;
  draw_text(10,y,elem_def[clip.kind].name,0x1B,-1);
  draw_text(25,y,buf,0x17,snprintf(buf,80,"<%02X>",clip.param));
  v_char[y*80+29]=(clip.stat?'s':' ');
  v_color[y*80+29]=0x1A;
  v_char[y*80+30]=apparent_clip;
  v_color[y*80+30]=clip.color;
  if(b_under[ycur*board_info.width+xcur].kind) v_char[y*80+31]='u',v_color[y*80+31]=0x13;
  if(b_main[ycur*board_info.width+xcur].kind) v_char[y*80+32]='m',v_color[y*80+32]=0x13;
  if(b_over[ycur*board_info.width+xcur].kind) v_char[y*80+33]='o',v_color[y*80+33]=0x13;
  if(numprefix) draw_text(34,y,buf,0x1E,snprintf(buf,80,"%5d",numprefix));
  if(markgrid && xcur<markwidth && ycur<markheight) {
    if(markgrid[(xcur>>3)+ycur*markskip]&(1<<(xcur&7))) v_char[y*80+39]=7,v_color[y*80+39]=0x1A;
  }
  v_char[y*80+42]=emode;
  v_color[y*80+42]=0x1C;
  draw_text(69,y,buf,0x19,snprintf(buf,80,"(%4d,%4d)",xcur,ycur));
  v_char[y*80+69]="(\x11\x10\x04"[(scroll_x?1:0)+(scroll_x+80<board_info.width?2:0)];
  v_char[y*80+79]=")\x1E\x1F\x04"[(scroll_y?1:0)+(scroll_y+25<board_info.height?2:0)];
}

static StatXY*find_stat(Uint16 x,Uint16 y,Uint8 n,Uint8 lay,Uint8 nlay) {
  int i;
  Stat*s;
  if(!n || n>maxstat) return 0;
  s=stats+n-1;
  if(nlay && !lay) {
    if(s->count>=0xFFFE) return 0;
    i=s->count;
    s->xy=realloc(s->xy,++s->count*sizeof(StatXY));
    if(!s->xy) err(1,"Allocation failed");
    s->xy[i].x=x;
    s->xy[i].y=y;
    s->xy[i].instptr=0;
    s->xy[i].layer=nlay;
    s->xy[i].delay=0;
    return s->xy+i;
  }
  for(i=0;i<s->count;i++) {
    if(s->xy[i].x==x && s->xy[i].y==y && (s->xy[i].layer&3)==lay) {
      if(nlay) {
        s->xy[i].layer=(s->xy[i].layer&~3)|(nlay&3);
      } else {
        if(i!=s->count-1) memmove(s->xy+i,s->xy+i+1,(s->count-i-1)*sizeof(StatXY));
        --s->count;
        return 0;
      }
      break;
    }
  }
  return 0;
}

static void delete_at(Uint16 x,Uint16 y) {
  Uint32 at=y*board_info.width+x;
  find_stat(x,y,b_main[at].stat,2,0);
  b_main[at]=b_under[at];
  b_under[at].kind=b_under[at].color=b_under[at].param=b_under[at].stat=0;
  find_stat(x,y,b_main[at].stat,1,2);
}

static void place_at(Uint16 x,Uint16 y,Tile t) {
  Uint32 at=y*board_info.width+x;
  if(b_main[at].kind==t.kind && b_main[at].color==t.color && b_main[at].param==t.param) return;
  if((elem_def[b_main[at].kind].attrib&~elem_def[t.kind].attrib)&A_FLOOR) {
    if(b_under[at].stat && !config.overwrite_stats) return;
    find_stat(x,y,b_under[at].stat,1,0);
    b_under[at]=b_main[at];
    find_stat(x,y,b_under[at].stat,2,1);
  } else {
    if(b_main[at].stat && !config.overwrite_stats) return;
    find_stat(x,y,b_main[at].stat,2,0);
  }
  b_main[at]=t;
  if(t.stat) find_stat(x,y,t.stat,0,2);
}

static void cursor_move(Sint32 xd,Sint32 yd) {
  Sint32 x=xcur+xd*(numprefix?:1);
  Sint32 y=ycur+yd*(numprefix?:1);
  if(emode!=15) {
    if(x<0) xcur=0; else if(x>=board_info.width) xcur=board_info.width-1; else xcur=x;
    if(y<0) ycur=0; else if(y>=board_info.height) ycur=board_info.height-1; else ycur=y;
  } else {
    if(!numprefix) numprefix=1;
    while(numprefix-- && xcur+xd>=0 && xcur+xd<board_info.width && ycur+yd>=0 && ycur+yd<board_info.height) {
      place_at(xcur+=xd,ycur+=yd,clip);
    }
  }
  numprefix=0;
}

static Sint32 cctmp;
static Tile cctile;
static Uint8 ccerror;

static Tile read_tile(const char*arg) {
  int k;
  const char*e;
  Tile t={0,0,0,0};
  if(*arg=='<') {
    arg++;
    if(*arg>='0' && *arg<='9') t.color=*arg-'0';
    else if(*arg>='A' && *arg<='F') t.color=*arg+10-'A';
    else if(*arg>='a' && *arg<='f') t.color=*arg+10-'a';
    else goto error;
    arg++;
    t.color<<=4;
    if(*arg>='0' && *arg<='9') t.color|=*arg-'0';
    else if(*arg>='A' && *arg<='F') t.color|=*arg+10-'A';
    else if(*arg>='a' && *arg<='f') t.color|=*arg+10-'a';
    else goto error;
    arg++;
    if(*arg++!='>') goto error;
  }
  e=strchrnul(arg,'<');
  for(k=0;k<256;k++) if(elem_def[k].name[0] && strlen(elem_def[k].name)==e-arg && !strncasecmp(elem_def[k].name,arg,e-arg)) break;
  if(k==256) goto error;
  t.kind=k;
  arg=e;
  if(*arg=='<') {
    arg++;
    if(*arg>='0' && *arg<='9') t.param=*arg-'0';
    else if(*arg>='A' && *arg<='F') t.param=*arg+10-'A';
    else if(*arg>='a' && *arg<='f') t.param=*arg+10-'a';
    else goto error;
    arg++;
    t.param<<=4;
    if(*arg>='0' && *arg<='9') t.param|=*arg-'0';
    else if(*arg>='A' && *arg<='F') t.param|=*arg+10-'A';
    else if(*arg>='a' && *arg<='f') t.param|=*arg+10-'a';
    else goto error;
    arg++;
    if(*arg++!='>') goto error;
  }
  return t;
  error:
  alert_text("Improper tile specification");
  ccerror=1;
  t.kind=0;
  return t;
}

static Uint16 read_coordinate(char**p,Uint16 c) {
  Sint8 r=0;
  Uint16 o=0;
  if(**p=='+' || **p=='-') r=(**p=='-'?-1:1),++*p;
  if(**p<'0' || **p>'9') return c+r;
  while(**p>='0' && **p<='9') o=10*o+**p-'0',++*p;
  return o+c*r;
}

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

static void cc_board(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  switch_to_board(strtol(arg,0,10));
}

static void cc_count_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  cctmp=0;
}

static void cc_count_step(Uint16 x,Uint16 y,const char*arg) {
  ++cctmp;
}

static void cc_count_end(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  char buf[40];
  snprintf(buf,40,"%ld",(long)cctmp);
  alert_text(buf);
}

static void cc_delete_step(Uint16 x,Uint16 y,const char*arg) {
  delete_at(x,y);
}

static void cc_exchangelayer_step(Uint16 x,Uint16 y,const char*arg) {
  //TODO
}

static void cc_export(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  char buf[75];
  const char*e;
  FILE*fp;
  if(!*arg) return;
  if(*arg=='|') fp=popen(arg+1,"w"); else fp=fopen(arg,"wx");
  if(fp) {
    if(e=save_board(fp,0)) alert_text(e);
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
    if(e=load_board(fp)) alert_text(e);
    if(*arg=='|') pclose(fp); else fclose(fp);
  } else {
    snprintf(buf,75,"%m");
    alert_text(buf);
  }
}

static void cc_mark_step(Uint16 x,Uint16 y,const char*arg) {
  set_mark(x,y,3);
}

static void cc_place_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  if(*arg) cctile=read_tile(arg); else cctile=clip;
}

static void cc_place_step(Uint16 x,Uint16 y,const char*arg) {
  place_at(x,y,cctile);
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

static void cc_unmark(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Uint16 x,y;
  if(!x0 && !y0 && x1>=markwidth && y1>=markheight) {
    free(markgrid);
    markwidth=markheight=markskip=0;
    markgrid=0;
  } else {
    for(x=x0,y=y0;;) {
      cc_unmark_step(x,y,0);
      if(x==x1) {
        x=x0;
        if(y==y1) break;
        if(y1>y) y++; else y--;
      } else {
        if(x1>x) x++; else x--;
      }
    }
  }
}

static const ColonCommand colon_commands[]={
  {"b",0,cc_board,0,0,0},
  {"board",0,cc_board,0,0,0},
  {"co",'%',0,cc_count_begin,cc_count_step,cc_count_end},
  {"count",'%',0,cc_count_begin,cc_count_step,cc_count_end},
  {"d",'.',0,0,cc_delete_step,0},
  {"delete",'.',0,0,cc_delete_step,0},
  {"ex",0,cc_export,0,0,0},
  {"exchangelayer",'.',0,0,cc_exchangelayer_step,0},
  {"export",0,cc_export,0,0,0},
  {"im",0,cc_import,0,0,0},
  {"import",0,cc_import,0,0,0},
  {"m",'.',0,0,cc_mark_step,0},
  {"mark",'.',0,0,cc_mark_step,0},
  {"p",'.',0,cc_place_begin,cc_place_step,0},
  {"place",'.',0,cc_place_begin,cc_place_step,0},
  {"status",0,cc_status,0,0,0},
  {"t",'.',0,0,cc_toggle_step,0},
  {"toggle",'.',0,0,cc_toggle_step,0},
  {"u",'.',cc_unmark,0,cc_unmark_step,0},
  {"unmark",'.',cc_unmark,0,cc_unmark_step,0},
  {"x",'.',0,0,cc_exchangelayer_step,0},
};

static void do_colon_command(char*text) {
  ColonCommand key;
  ColonCommand*found;
  Uint8 ra=0;
  Uint16 x0=xcur;
  Uint16 y0=ycur;
  Uint16 x1=xcur;
  Uint16 y1=ycur;
  Uint16 x,y;
  int i;
  if(*text=='~' && text[1]=='&') {
    ra='~';
    x0=y0=0;
    x1=board_info.width-1;
    y1=board_info.height-1;
    text+=2;
  } else if(*text=='&' || *text=='%') {
    ra=*text++;
    x0=y0=0;
    x1=board_info.width-1;
    y1=board_info.height-1;
  } else if(*text=='.') {
    ra='%';
    text++;
  }
  if(x0>=board_info.width) x0=board_info.width-1;
  if(y0>=board_info.height) y0=board_info.height-1;
  if(x1>=board_info.width) x1=board_info.width-1;
  if(y1>=board_info.height) y1=board_info.height-1;
  if((*text>='0' && *text<='9') || *text=='-' || *text=='+' || *text==',') {
    if(!ra) ra='%'; else if(ra=='%') { alert_text("Improper coordinates"); return; }
    x0=read_coordinate(&text,xcur);
    if(*text++!=',') goto syntax;
    y0=read_coordinate(&text,ycur);
    if(*text==':') {
      text++;
      x1=read_coordinate(&text,xcur);
      if(*text++!=',') goto syntax;
      y1=read_coordinate(&text,ycur);
    } else {
      x1=x0,y1=y0;
    }
  }
  if(!*text) {
    if(x0!=x1 || y0!=y1) goto syntax;
    xcur=x0;
    ycur=y0;
    return;
  }
  //TODO: filters/displacements
  key.name=text;
  text=strchrnul(text,' ');
  if(*text) *text++=0;
  found=bsearch(&key,colon_commands,sizeof(colon_commands)/sizeof(ColonCommand),sizeof(ColonCommand),compare_coloncommand);
  if(!found) { alert_text("Improper command"); return; }
  if(ra && !found->range) { alert_text("Improper use of range"); return; }
  if(!ra) {
    ra=found->range;
    if(ra=='.') ra='%'; else x0=y0=0,x1=board_info.width-1,y1=board_info.height-1;
  }
  ccerror=0;
  if(found->full) {
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
  ask_text(":",text,74);
  if(*text) do_colon_command(text);
}

Uint16 edit_board(Uint16 id) {
  int i;
  Sint32 k;
  if(status_on==255) status_on=config.brd_edit_status;
  set_apparent_clip();
  set_board_editor_screen();
  goto_board(id);
  scroll_x=scroll_y=0;
  v_status[1]='B';
  for(;;) {
    escroll();
    update_screen();
    v_xcur=xcur-scroll_x; v_ycur=ycur-scroll_y;
    if(status_on) estatus();
    redisplay();
    do { if(!next_event()) goto exit; } while(event.type!=SDL_KEYDOWN);
    k=(!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym;
    switch(emode) {
      case 0: case 15: no_mode: switch(k) {
        case 0x08: numprefix/=10; break;
        case 0x09: if(emode=(emode?0:15)) place_at(xcur,ycur,clip); break;
        case 0x1B: if(numprefix) numprefix=0; else if(emode) emode=0; else goto exit; break;
        case '0' ... '9': if((i=numprefix*10+k-'0')<65536) numprefix=i; break;
        case -SDLK_i: edit_board_info(); break;
        case -SDLK_r: resize_board(); break;
        case -SDLK_t:
          if(boardnames) write_name_list("BRD.NAM",boardnames,maxboard);
          run_test_game(brd_id);
          break;
        case ' ': set_mark(xcur,ycur,1); break;
        case 'd': case -SDLK_DELETE: delete_at(xcur,ycur); break;
        case 'e': edit_tile(); break;
        case 'h': case -SDLK_LEFT: cursor_move(-1,0); break;
        case 'j': case -SDLK_DOWN: cursor_move(0,1); break;
        case 'k': case -SDLK_UP: cursor_move(0,-1); break;
        case 'l': case -SDLK_RIGHT: cursor_move(1,0); break;
        case 'p': place_at(xcur,ycur,clip); break;
        case 'y': clip=(event.key.keysym.mod&KMOD_SHIFT?b_under:b_main)[xcur+ycur*board_info.width]; set_apparent_clip(); break;
        case -SDLK_HOME: xcur=ycur=0; break;
        case -SDLK_END: xcur=board_info.width-1; ycur=board_info.height-1; break;
        case '[': switch_to_board(brd_id-(numprefix?:1)); numprefix=0; break;
        case ']': switch_to_board(brd_id+(numprefix?:1)); numprefix=0; break;
        case '{': switch_to_board(numprefix); numprefix=0; break;
        case '}': switch_to_board(maxboard); numprefix=0; break;
        case ':': ask_colon_command(); break;
      } break;
      default: emode=0;
    }
  }
  exit:
  v_status[1]=0;
  esave();
  return brd_id;
}

