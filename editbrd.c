#if 0
gcc -g -O0 -c -Wno-unused-result -std=gnu99 editbrd.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"
#include <math.h>

static Uint16 brd_id;
static Uint16 xcur,ycur,xcur2,ycur2;
static Uint8 status_on=255;
static Tile clip,overclip;
static Uint8 apparent_clip;
static Uint16 numprefix;
static Uint8 emode,vmode;
static Uint8*markgrid;
static Uint16 markwidth,markheight,markskip;
static Uint8*markgrid2;
static Uint16 markwidth2,markheight2,markskip2;

static StatXY*find_stat(Uint16 x,Uint16 y,Uint8 n,Uint8 lay,Uint8 nlay);
static void stat_list_callback(Uint16 n,int y,void*uz);
static void stat_xy_edit(Stat*s,Uint16 n);
static Uint8 parameter_edit(Uint16 addr,Uint8 par,Uint8 sta,StatXY*sxy);

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

static void exchange_mark_grid(void) {
  Uint8*g;
  Uint16 w,h,s;
  g=markgrid2; w=markwidth2; h=markheight2; s=markskip2;
  markgrid2=markgrid; markwidth2=markwidth; markheight2=markheight; markskip2=markskip;
  markgrid=g; markwidth=w; markheight=h; markskip=s;
}

static void goto_board(Uint16 id) {
  FILE*fp=open_lump_by_number(id,"BRD","r");
  const char*e;
  char b=0;
  int i;
  Uint16 m;
  if(!fp) {
    if(maxboard<id) {
      alert_text("Board not found");
      return;
    }
    b=1;
    if(!config.editor_auto || !memory[0x241]) fp=open_lump_by_number(config.template_board,"BRD","r");
  }
  if(fp) {
    if(e=load_board(fp)) alert_text(e);
    fclose(fp);
    if(b) {
      memset(b_under,0,board_info.width*board_info.height*sizeof(Tile));
      memset(b_main,0,board_info.width*board_info.height*sizeof(Tile));
      memset(b_over,0,board_info.width*board_info.height*sizeof(Tile));
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
  } else if(b && config.editor_auto && (m=memory[0x241])) {
    if(m>0xFFF0 || memory[m]!=2 || (memory[m+3]&~127)) {
      alert_text("The ED1 65 command is used improperly");
      goto blank;
    }
    free(b_under);
    if(!board_info.width || !(memory[m+4]&1)) board_info.width=memory[m+1]?:80;
    if(!board_info.height || !(memory[m+4]&1)) board_info.height=memory[m+2]?:25;
    b_under=calloc(board_info.height*board_info.width,3*sizeof(Tile));
    if(!b_under) err(1,"Allocation failed");
    b_main=b_under+board_info.height*board_info.width;
    b_over=b_main+board_info.height*board_info.width;
    board_info.exits[0]=board_info.exits[1]=0;
    board_info.exits[2]=board_info.exits[3]=0;
    if(!stats || maxstat!=memory[m+3]) {
      for(i=0;i<maxstat;i++) {
        free(stats[i].text);
        free(stats[i].xy);
        stats[i].text=0;
        stats[i].length=0;
        stats[i].xy=0;
        stats[i].frame=0;
        stats[i].count=0;
      }
      stats=realloc(stats,(memory[m+3]?:1)*sizeof(Stat));
      if(!stats) err(1,"Allocation failed");
      while(maxstat<memory[m+3]) memset(stats+maxstat++,0,sizeof(Stat));
      maxstat=memory[m+3]?:1;
    }
    parameter_edit(m+5,0,0,0);
  } else {
    blank:
    free(b_under);
    if(!board_info.width) board_info.width=60;
    if(!board_info.height) board_info.height=25;
    b_under=calloc(board_info.height*board_info.width,3*sizeof(Tile));
    if(!b_under) err(1,"Allocation failed");
    b_main=b_under+board_info.height*board_info.width;
    b_over=b_main+board_info.height*board_info.width;
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
    win_numeric('U',(config.editor_custom_labels && memory[0x224])?(char*)gtext[memory[0x224]]:"User data: ",board_info.userdata,0,65535);
    win_numeric('c',"Screen: ",board_info.screen,0,65535);
    win_boolean('0',(config.editor_custom_labels && memory[0x220])?(char*)gtext[memory[0x220]]:"User flag 0",board_info.flag,BF_USER0);
    win_boolean('1',(config.editor_custom_labels && memory[0x221])?(char*)gtext[memory[0x221]]:"User flag 1",board_info.flag,BF_USER1);
    win_boolean('2',(config.editor_custom_labels && memory[0x222])?(char*)gtext[memory[0x222]]:"User flag 2",board_info.flag,BF_USER2);
    win_boolean('3',(config.editor_custom_labels && memory[0x223])?(char*)gtext[memory[0x223]]:"User flag 3",board_info.flag,BF_USER3);
    win_boolean('P',"Persist",board_info.flag,BF_PERSIST);
    win_boolean('g',"Suppress global scripts",board_info.flag,BF_NO_GLOBAL);
    win_boolean('V',"Visible overlay",board_info.flag,BF_OVERLAY);
    if(config.editor_custom_labels && memory[0x225] && maxstat) win_numeric('5',gtext[memory[0x225]],stats->misc1,0,65535);
    if(config.editor_custom_labels && memory[0x226] && maxstat) win_numeric('6',gtext[memory[0x226]],stats->misc2,0,65535);
    if(config.editor_custom_labels && memory[0x227] && maxstat) win_numeric('7',gtext[memory[0x227]],stats->misc3,0,65535);
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

static Uint8 new_stat(void) {
  if(maxstat==255) return 0;
  stats=realloc(stats,(maxstat+1)*sizeof(Stat));
  if(!stats) err(1,"Allocation failed");
  stats[maxstat].misc1=stats[maxstat].misc2=stats[maxstat].misc3=0;
  stats[maxstat].speed=1;
  stats[maxstat].length=0;
  stats[maxstat].count=0;
  stats[maxstat].text=0;
  stats[maxstat].xy=0;
  stats[maxstat].frame=0;
  return ++maxstat;
}

static void edit_tile(char lay) {
  Tile*p;
  win_form("Tile") {
    win_numeric('X',"X: ",xcur,0,board_info.width-1) win_refresh();
    win_numeric('Y',"Y: ",ycur,0,board_info.height-1) win_refresh();
    win_option('U',"Under layer",lay,0) win_refresh();
    win_option('M',"Main layer",lay,1) win_refresh();
    win_option('O',"Over layer",lay,2) win_refresh();
    win_blank();
    p=(lay==0?b_under:lay==1?b_main:b_over)+xcur+ycur*board_info.width;
    if(lay==2) {
      win_boolean('S',"Solid",p->kind,OVER_SOLID);
      win_boolean('R',"Reserved",p->kind,OVER_RESERVED);
      win_boolean('B',"BG Thru",p->kind,OVER_BG_THRU);
      win_boolean('V',"Visible",p->kind,OVER_VISIBLE);
      win_boolean('1',"User defined (1)",p->kind,0x01);
      win_boolean('2',"User defined (2)",p->kind,0x02);
      win_boolean('4',"User defined (4)",p->kind,0x04);
      win_boolean('8',"User defined (8)",p->kind,0x08);
    } else {
      win_numeric('K',"Kind: ",p->kind,0,255);
    }
    win_color('C',"Color: ",p->color);
    win_numeric('P',"Parameter: ",p->param,0,255);
    win_char('h',"Character: ",p->param) win_refresh();
    win_picture(1) {
      char buf[40];
      draw_text(0,0,buf,7,snprintf(buf,40,"Stat: %3d",p->stat));
      if(!p->stat) v_color[8]=8;
    }
    win_command('t',"Select stat...") {
      int n;
      win_form("Select stat") {
        win_cursor(p->stat);
        win_list(maxstat+1,0,stat_list_callback,n) {
          if(n!=p->stat) {
            find_stat(xcur,ycur,p->stat,lay+1,0);
            find_stat(xcur,ycur,p->stat=n,0,lay+1);
          }
          break;
        }
        win_blank();
        win_command_esc(0,"Cancel") break;
      }
    }
    if(p->stat && p->stat<=maxstat) {
      win_command('E',"Edit stat item...") {
        StatXY*r=find_stat(xcur,ycur,p->stat,lay+1,lay+1);
        if(r) stat_xy_edit(stats+p->stat-1,r-stats[p->stat-1].xy);
      }
    }
    // win_command('n',"Parameter menu...");
    win_blank();
    win_command_esc(0,"Done") break;
  }
}

static void stat_list_callback(Uint16 n,int y,void*uz) {
  char buf[81];
  Stat*s=n?stats+n-1:0;
  if(!n) {
    draw_text(1,y,"  0 (N/A)",0x08,-1);
    v_color[y*80+3]=0x0E;
    return;
  }
  draw_text(1,y,buf,0x0E,snprintf(buf,4,"%3u",n));
  if(s->text && *s->text=='@') {
    for(n=1;s->text[n] && s->text[n]!='\r' && s->text[n]!='\n' && n<28;n++);
    draw_text(5,y,s->text+1,0x06,n-1);
  }
  draw_text(35,y,buf,0x07,snprintf(buf,48,"(%05u,%05u,%05u) L=%05u C=%05u S=%03u",s->misc1,s->misc2,s->misc3,s->length,s->count,s->speed));
}

static void stat_xy_list_callback(Uint16 n,int y,void*uz) {
  char buf[81];
  StatXY*o=((Stat*)uz)->xy+n;
  draw_text(1,y,buf,0x0E,snprintf(buf,40,"%5u",n));
  draw_text(7,y,buf,0x07,snprintf(buf,40,"(%05d,%05d,%c)",o->x,o->y,"-umo"[o->layer&3]));
  v_char[80*y+23]=(o->layer&0x80?'L':250); v_color[80*y+23]=(o->layer&0x80?12:8);
  v_char[80*y+24]=(o->layer&0x40?'U':250); v_color[80*y+24]=(o->layer&0x40?13:8);
  draw_text(26,y,buf,0x07,snprintf(buf,40,"D=%3u",o->delay));
  if(o->instptr==65535) draw_text(32,y,"(STOP)",7,-1); else draw_text(32,y,buf,7,snprintf(buf,40,"IP=%5u",o->instptr));
}

static Uint16 exchange_statxy(Stat*s,Uint16 m,Uint16 n) {
  StatXY*o=s->xy+n;
  StatXY*p=s->xy+m;
  StatXY x=*p;
  *p=*o;
  *o=x;
  return m;
}

static void stat_xy_edit(Stat*s,Uint16 n) {
  char r;
  StatXY*o=s->xy+n;
  char f=(o->layer>>2)&3;
  char buf[81];
  static const char*const lay[4]={"N/A","Under","Main","Over"};
  win_form("Stat XY Edit") {
    win_picture(4) {
      draw_text(1,0,buf,7,snprintf(buf,80,"Index: %5u/%5u",n,s->count));
      draw_text(1,1,buf,7,snprintf(buf,80,"X: %5u",o->x));
      draw_text(1,2,buf,7,snprintf(buf,80,"Y: %5u",o->y));
      draw_text(1,3,buf,7,snprintf(buf,80,"Layer: %s",lay[o->layer&3]));
    }
    win_boolean('U',"User",o->layer,0x40);
    win_boolean('L',"Lock",o->layer,0x80);
    win_numeric('y',"Delay: ",o->delay,0,255);
    win_numeric('I',"Instruction: ",o->instptr,0,65535);
    win_blank();
    win_command('R',"Restart script") o->instptr=0,r=1;
    win_command('o',"Stop script") o->instptr=0xFFFF,r=1;
    win_blank();
    win_heading("Facing:");
    win_option('E',"East",f,0);
    win_option('N',"North",f,1);
    win_option('W',"West",f,2);
    win_option('S',"South",f,3);
    win_boolean('k',"Walking",o->layer,0x10);
    win_blank();
    win_command('t',"Move to start") o=s->xy+(n=exchange_statxy(s,0,n)),r=1;
    if(n) win_command('p',"Move to previous") o=s->xy+(n=exchange_statxy(s,n-1,n)),r=1;
    if(n<s->count-1) win_command('x',"Move to next") o=s->xy+(n=exchange_statxy(s,n+1,n)),r=1;
    win_command('d',"Move to end") o=s->xy+(n=exchange_statxy(s,s->count-1,n)),r=1;
    if(r) {
      r=0;
      win_refresh();
    }
    win_blank();
    win_command_esc(0,"Done") break;
  }
  o->layer=(o->layer&0xF3)|(f<<2);
}

static int statxy_sorter_callback(const void*aa,const void*bb) {
  const StatXY*a=aa;
  const StatXY*b=bb;
  int q=numprefix>>4;
  int t=numprefix&15;
  int z=(a->layer-b->layer)&3;
  if(t&8) {
    if(a->layer&0x80&~b->layer) return -1;
    if(b->layer&0x80&~a->layer) return 1;
  }
  if(q<2 && z) return z*(t&4?-1:1);
  if((q&1) && a->y!=b->y) return a->y<b->y?(t&2?1:-1):(t&2?-1:1);
  if(a->x!=b->x) return a->x<b->x?(t&1?1:-1):(t&1?-1:1);
  if(a->y!=b->y) return a->y<b->y?(t&2?1:-1):(t&2?-1:1);
  return z*(t&4?-1:1);
}

static void stat_edit(int n) {
  char title[40];
  char buf[80];
  char nam[60];
  Stat*s=stats+n-1;
  int i,j;
  Uint32 at;
  Stat q;
  snprintf(title,40,"Stat #%d",n);
  name:
  nam[1]=0;
  if(s->text && *s->text=='@') {
    for(i=0;s->text[i] && s->text[i]!='\r' && s->text[i]!='\n' && i<48;i++) nam[i]=s->text[i];
    nam[i]=0;
  }
  win_form(title) {
    win_numeric('1',"Misc1: ",s->misc1,0,0xFFFF);
    win_numeric('2',"Misc2: ",s->misc2,0,0xFFFF);
    win_numeric('3',"Misc3: ",s->misc3,0,0xFFFF);
    win_numeric('S',"Speed: ",s->speed,0,255);
    win_picture(3) {
      draw_text(1,0,buf,7,snprintf(buf,80,"Length: %5d",s->length));
      draw_text(1,1,buf,7,snprintf(buf,80,"Count: %5d",s->count));
      draw_text(1,2,"Name: ",7,-1);
      draw_text(7,2,nam+1,6,-1);
    }
    win_command('T',"Text") {
      s->text=text_editor(s->text);
      s->length=(s->text?strlen(s->text):0);
      goto name;
    }
    win_command('L',"XY List") {
      win_form(title) {
        win_list(s->count,s,stat_xy_list_callback,i) stat_xy_edit(s,i);
        win_blank();
        win_command('S',"Sort...") {
          i=j=0;
          win_form("Sort stat XY list") {
            win_option('X',"Layer,X,Y",i,0);
            win_option('Y',"Layer,Y,X",i,1);
            win_option('L',"X,Y,Layer",i,2);
            win_option('a',"Y,X,Layer",i,3);
            win_blank();
            win_boolean('R',"Reverse X",j,1);
            win_boolean('v',"Reverse Y",j,2);
            win_boolean('s',"Reverse Layer",j,4);
            win_boolean('U',"First if user bits set",j,8);
            win_blank();
            win_command('E',"Execute") {
              numprefix=(i<<4)+j;
              qsort(s->xy,s->count,sizeof(StatXY),statxy_sorter_callback);
              numprefix=0;
              break;
            }
            win_command_esc(0,"Cancel") break;
          }
        }
        win_command('R',"Reverse") {
          if(s->count) for(i=0;i<=s->count/2;i++) exchange_statxy(s,i,s->count-i-1);
        }
        win_command_esc(0,"Done") break;
      }
    }
    win_command('x',"Exchange") {
      *buf=0;
      ask_text("Exchange with:",buf,8);
      if(*buf && (i=strtol(buf,0,10)) && i<=maxstat && i!=n) {
        for(at=0;at<board_info.width*board_info.height*3;at++) {
          if(b_under[at].stat==n) b_under[at].stat=i; else if(b_under[at].stat==i) b_under[at].stat=n;
        }
        q=stats[i-1];
        stats[i-1]=stats[n-1];
        stats[n-1]=q;
        s=stats+(n=i)-1;
      }
    }
    if(maxstat>1) win_command('D',"Delete") {
      if(ask_yn("Delete?",0)) {
        for(at=0;at<board_info.width*board_info.height*3;at++) {
          if(b_under[at].stat==n) b_under[at].stat=0; else if(b_under[at].stat>n) --b_under[at].stat;
        }
        if(n<maxstat) memmove(stats+n-1,stats+n,(maxstat-n)*sizeof(Stat));
        --maxstat;
        return;
      }
    }
    win_command_esc(0,"Done") break;
  }
}

static void stat_list(int pc) {
  int n;
  restart:
  win_form("Stat list") {
    win_cursor(pc?:1);
    win_list(maxstat+1,0,stat_list_callback,n) if(n) stat_edit(n);
    win_blank();
    if(maxstat<255) win_command('A',"Add new stat") {
      pc=new_stat();
      goto restart;
    }
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
    win_boolean('v',"Fill overlay with NW tile",b,2);
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

static Sint32 add_board(void) {
  char buf[61]="";
  ask_text("Add new board:",buf,60);
  if(*buf) {
    set_board_name(maxboard+1,buf);
    return maxboard;
  } else {
    return -1;
  }
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
  // 00000000001111111111222222222233333333334444444444555555555566666666667777777777
  // 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  // _____ee<c>_______________<pp>s*umo_____  VE                +____+____(____,____)
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
  if(vmode) v_char[y*80+41]='V',v_color[y*80+41]=0x1D;
  v_char[y*80+42]=emode;
  v_color[y*80+42]=0x1C;
  draw_text(69,y,buf,0x19,snprintf(buf,80,"(%4d,%4d)",xcur,ycur));
  v_char[y*80+69]="(\x11\x10\x04"[(scroll_x?1:0)+(scroll_x+80<board_info.width?2:0)];
  v_char[y*80+79]=")\x1E\x1F\x04"[(scroll_y?1:0)+(scroll_y+25<board_info.height?2:0)];
  if(emode=='v') {
    draw_text(59,y,buf,0x19,snprintf(buf,80,"%c%04d%c%04d",xcur2>xcur?'-':'+',abs(xcur2-xcur),ycur2>ycur?'-':'+',abs(ycur2-ycur)));
  } else if(emode=='w' && xcur2) {
    draw_text(59,y,buf,0x19,snprintf(buf,80,"%05d",xcur2));
  }
}

static void estatus_over(void) {
  // 00000000001111111111222222222233333333334444444444555555555566666666667777777777
  // 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  // _____^^<c>________________<p>s umo_____  VE                +____+____(____,____)
  char buf[80];
  int y=24;
  int x;
  if(board_info.height>24 && v_ycur>12) y=0;
  memset(v_color+y*80,0x11,80);
  draw_text(0,y,buf,0x1B,snprintf(buf,80,"%5d",brd_id));
  v_color[y*80+5]=v_color[y*80+6]=0x14;
  v_char[y*80+5]=v_char[y*80+6]='^';
  draw_text(7,y,"<\xFE>",0x17,3);
  v_color[y*80+8]=overclip.color;
  draw_text(10,y,"overlay:",0x1B,-1);
  for(x=0;x<8;x++) if(overclip.kind&(1<<x)) v_color[y*80+x+18]=0x1B,v_char[y*80+x+18]="1248SRBV"[x];
  draw_text(26,y,"<\xFE>",0x17,3);
  v_char[y*80+27]=overclip.param;
  v_color[y*80+27]=0x1F;
  v_char[y*80+29]=(overclip.stat?'s':' ');
  v_color[y*80+29]=0x1A;
  if(b_under[ycur*board_info.width+xcur].kind) v_char[y*80+31]='u',v_color[y*80+31]=0x13;
  if(b_main[ycur*board_info.width+xcur].kind) v_char[y*80+32]='m',v_color[y*80+32]=0x13;
  if(b_over[ycur*board_info.width+xcur].kind) v_char[y*80+33]='o',v_color[y*80+33]=0x13;
  if(numprefix) draw_text(34,y,buf,0x1E,snprintf(buf,80,"%5d",numprefix));
  if(markgrid && xcur<markwidth && ycur<markheight) {
    if(markgrid[(xcur>>3)+ycur*markskip]&(1<<(xcur&7))) v_char[y*80+39]=7,v_color[y*80+39]=0x1A;
  }
  if(vmode) v_char[y*80+41]='V',v_color[y*80+41]=0x1D;
  v_char[y*80+42]=emode;
  v_color[y*80+42]=0x1C;
  draw_text(69,y,buf,0x19,snprintf(buf,80,"(%4d,%4d)",xcur,ycur));
  v_char[y*80+69]="(\x11\x10\x04"[(scroll_x?1:0)+(scroll_x+80<board_info.width?2:0)];
  v_char[y*80+79]=")\x1E\x1F\x04"[(scroll_y?1:0)+(scroll_y+25<board_info.height?2:0)];
  if(emode=='v') {
    draw_text(59,y,buf,0x19,snprintf(buf,80,"%c%04d%c%04d",xcur2>xcur?'-':'+',abs(xcur2-xcur),ycur2>ycur?'-':'+',abs(ycur2-ycur)));
  }
}

static StatXY*find_stat(Uint16 x,Uint16 y,Uint8 n,Uint8 lay,Uint8 nlay) {
  // Moves a stat with number (n) from layer (lay) to (nlay), at coordinates (x,y).
  // If either layer number is zero, means a nonexistent stat XY record.
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
    s->xy[i].frame=0;
    s->xy[i].extra=0;
    s->xy[i].sensor=(Tile){};
    return s->xy+i;
  }
  for(i=0;i<s->count;i++) {
    if(s->xy[i].x==x && s->xy[i].y==y && (s->xy[i].layer&3)==lay) {
      if(nlay) {
        s->xy[i].layer=(s->xy[i].layer&~3)|(nlay&3);
        return s->xy+i;
      } else {
        if(i!=s->count-1) memmove(s->xy+i,s->xy+i+1,(s->count-i-1)*sizeof(StatXY));
        --s->count;
        return 0;
      }
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

static void over_delete_at(Uint16 x,Uint16 y) {
  Uint32 at=y*board_info.width+x;
  find_stat(x,y,b_main[at].stat,3,0);
  b_over[at].kind=b_over[at].color=b_over[at].param=b_over[at].stat=0;
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

static void over_place_at(Uint16 x,Uint16 y,Tile t) {
  Uint32 at=y*board_info.width+x;
  if(b_over[at].kind==t.kind && b_over[at].color==t.color && b_over[at].param==t.param) return;
  if(b_over[at].stat && !config.overwrite_stats) return;
  find_stat(x,y,b_over[at].stat,3,0);
  b_over[at]=t;
  if(t.stat) find_stat(x,y,t.stat,0,3);
}

static void write_at(Uint16 x,Uint16 y,Tile t) {
  Uint32 at=y*board_info.width+x;
  Uint8 z=(t.stat!=b_main[at].stat);
  if(b_main[at].stat && !config.overwrite_stats) return;
  if(z) find_stat(x,y,b_main[at].stat,2,0);
  b_main[at]=t;
  if(t.stat && z) find_stat(x,y,t.stat,0,2);
}

static void write_under(Uint16 x,Uint16 y,Tile t) {
  Uint32 at=y*board_info.width+x;
  Uint8 z=(t.stat!=b_under[at].stat);
  if(b_under[at].stat && !config.overwrite_stats) return;
  if(z) find_stat(x,y,b_under[at].stat,1,0);
  b_under[at]=t;
  if(t.stat && z) find_stat(x,y,t.stat,0,1);
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

static void over_cursor_move(Sint32 xd,Sint32 yd) {
  Sint32 x=xcur+xd*(numprefix?:1);
  Sint32 y=ycur+yd*(numprefix?:1);
  if(emode!='*') {
    if(x<0) xcur=0; else if(x>=board_info.width) xcur=board_info.width-1; else xcur=x;
    if(y<0) ycur=0; else if(y>=board_info.height) ycur=board_info.height-1; else ycur=y;
  } else {
    if(!numprefix) numprefix=1;
    while(numprefix-- && xcur+xd>=0 && xcur+xd<board_info.width && ycur+yd>=0 && ycur+yd<board_info.height) {
      over_place_at(xcur+=xd,ycur+=yd,clip);
    }
  }
  numprefix=0;
}

static void far_cursor_move(Sint32 xd,Sint32 yd,char hi,char ov) {
  Tile*b=(ov?b_over:b_main);
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
  if(x<0) r=0,xcur=0; else if(x>=board_info.width) r=0,xcur=board_info.width-1; else xcur=x;
  if(y<0) r=0,ycur=0; else if(y>=board_info.height) r=0,ycur=board_info.height-1; else ycur=y;
  if(r && b[y0*w+x0].kind==b[y*w+x].kind && (!hi || b[y0*w+x0].color==b[y*w+x].color)) goto repeat;
}

static void find_next_marked(Sint32 dir) {
  Sint32 x=xcur;
  Sint32 y=ycur;
  while(dir>0 && y<board_info.height) {
    if(++x==board_info.width) x=0,++y;
    if(set_mark(x,y,2)) --dir;
  }
  while(dir<0 && y>=0) {
    if(!x--) x=board_info.width-1,--y;
    if(set_mark(x,y,2)) ++dir;
  }
  if(x>=0 && x<board_info.width && y>=0 && y<board_info.height) {
    xcur=x;
    ycur=y;
  }
}

static void flood(Sint32 x,Sint32 y,Sint32 x0,Sint32 y0,Uint16 m) {
  Uint32 a,a0;
  if(x<0 || x>=board_info.width || y<0 || y>=board_info.height) return;
  a=y*board_info.width+x; a0=y0*board_info.width+x0;
  if(m&0x0001) {
    if(b_main[a].kind!=b_main[a0].kind) return;
    if(m&0x0080) if(b_under[a].kind!=b_under[a0].kind) return;
  }
  if(m&0x0002) {
    if(b_main[a].color!=b_main[a0].color) return;
    if(m&0x0080) if(b_under[a].color!=b_under[a0].color) return;
  }
  if(m&0x0004) {
    if(b_main[a].param!=b_main[a0].param) return;
    if(m&0x0080) if(b_under[a].param!=b_under[a0].param) return;
  }
  if(m&0x0010) if(b_over[a].kind!=b_over[a0].kind) return;
  if(m&0x0020) if(b_over[a].color!=b_over[a0].color) return;
  if(m&0x0040) if(b_over[a].param!=b_over[a0].param) return;
  if(set_mark(x,y,3)) return;
  if(numprefix && abs(x-x0)+abs(y-y0)>=numprefix) return;
  if(emode=='F') {
    flood(x+1,y+1,x0,y0,m);
    flood(x-1,y+1,x0,y0,m);
    flood(x-1,y-1,x0,y0,m);
    flood(x+1,y-1,x0,y0,m);
  }
  flood(x+1,y,x0,y0,m);
  flood(x-1,y,x0,y0,m);
  flood(x,y-1,x0,y0,m);
  flood(x,y+1,x0,y0,m);
}

static void copy_cell(Sint32 x0,Sint32 y0,Sint32 x1,Sint32 y1) {
  place_at(x1,y1,b_main[y0*board_info.width+x0]);
}

static void over_copy_cell(Sint32 x0,Sint32 y0,Sint32 x1,Sint32 y1) {
  over_place_at(x1,y1,b_over[y0*board_info.width+x0]);
}

static void mass_move(Sint32 xd,Sint32 yd,void(*f)(Sint32,Sint32,Sint32,Sint32)) {
  Sint32 x,y;
  Uint32 w=board_info.width;
  Uint32 h=board_info.height;
  Uint8 z;
  if(!markgrid) return;
  if(xd>0) {
    if(xd>=w) return;
    for(y=0;y<h;y++) for(x=w-xd;x<w;x++) if(set_mark(x,y,2)) return;
  } else if(xd<0) {
    if(-xd>=w) return;
    for(y=0;y<h;y++) for(x=0;x<-xd;x++) if(set_mark(x,y,2)) return;
  }
  if(yd>0) {
    if(yd>=h) return;
    for(y=h-yd;y<h;y++) for(x=0;x<w;x++) if(set_mark(x,y,2)) return;
  } else if(yd<0) {
    if(-yd>=h) return;
    //for(y=0;y<-yd;y++) for(x=0;x<w;x++) if(set_mark(x,y,2)) return;
    for(y=0;y<markheight && y<-yd;y++) for(x=0;x<markskip;x++) if(markgrid[y*markskip+x]) return;
  }
  for(x=(xd>0?w-1:0);x>=0 && x<w;x+=(xd>0?-1:1)) for(y=(yd>0?h-1:0);y>=0 && y<h;y+=(yd>0?-1:1)) {
    set_mark(x,y,z=3*set_mark(x-xd,y-yd,2));
    if(z) f(x-xd,y-yd,x,y);
  }
}

static void block_tiling(char q) {
  // q: 0(normal) 1(floor) 2(over)
  Tile c;
  Uint16 x,y;
  Uint32 x0=xcur;
  Uint32 y0=ycur;
  Sint32 xn=xcur2-xcur;
  Sint32 yn=ycur2-ycur;
  Sint32 z;
  if(xn<0) x0=xcur2,xn=-xn;
  if(yn<0) y0=ycur2,yn=-yn;
  ++xn; ++yn;
  for(y=0;y<markheight;y++) for(x=0;x<markwidth;x++) if((x<x0 || x>=x0+xn) && (y<y0 || y>=y0+yn) && set_mark(x,y,2)) {
    z=((numprefix?y+(dice(numprefix+1)+7)/8:y)%yn+y0)*board_info.width+((numprefix?x+(dice(numprefix+1)+7)/8:x)%xn+x0);
    c=(q==2?b_over:q?(elem_def[b_main[z].kind].attrib&A_FLOOR?b_main:b_under):b_main)[z];
    z=y*board_info.width+x;
    switch(q) {
      case 0: place_at(x,y,c); break;
      case 1: if(elem_def[b_main[z].kind].attrib&A_FLOOR) write_under(x,y,c); else write_at(x,y,c); break;
      case 2: over_place_at(x,y,c); break;
    }
  }
  numprefix=0;
}

static Sint32 cctmp;
static Tile cctile;
static Uint8*ccdata;
static Uint8 ccerror;
static Uint8 ccrestrict=0;

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
  if(!ccrestrict) goto_board(strtol(arg,0,10));
}

static void cc_boardinfo(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Uint16 c,f;
  while(*arg) {
    while(*arg==' ') ++arg;
    if(!*arg) break;
    switch(c=*arg++) {
      case '=': case '+': case '-':
        f=0;
        while(*arg && *arg!=' ') switch(*arg++) {
          case '0': f|=BF_USER0; break;
          case '1': f|=BF_USER1; break;
          case '2': f|=BF_USER2; break;
          case '3': f|=BF_USER3; break;
          case 'P': f|=BF_PERSIST; break;
          case 'N': f|=BF_NO_GLOBAL; break;
          case 'O': f|=BF_OVERLAY; break;
        }
        if(c=='=') board_info.flag=f;
        if(c=='-') board_info.flag&=~f;
        if(c=='+') board_info.flag|=f;
        break;
      case 's': board_info.screen=strtol(arg,(char**)&arg,10); break;
      case 'u': board_info.userdata=strtol(arg,(char**)&arg,10); break;
      default: alert_text("Improper :boardinfo"); return;
    }
  }
}

static void cc_color_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  if(*arg) cctmp=strtol(arg,0,16); else cctmp=clip.color;
}

static void cc_color_step(Uint16 x,Uint16 y,const char*arg) {
  Uint32 at=y*board_info.width+x;
  b_main[at].color=cctmp;
}

static void cc_count_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  cctmp=0;
}

static void cc_count_step(Uint16 x,Uint16 y,const char*arg) {
  ++cctmp;
}

static void cc_count_end(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  char buf[40];
  long n=(abs(x1-x0)+1)*(long)(abs(y1-y0)+1);
  snprintf(buf,40,"%ld/%ld (%4.1f%%)",(long)cctmp,n,cctmp*100.0/n);
  alert_text(buf);
}

static void cc_crop(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Sint32 x,y;
  Uint32 a,z;
  Tile*ku=b_under;
  Tile*km=b_main;
  Tile*ko=b_over;
  Uint16 w=board_info.width;
  Uint16 h=board_info.height;
  free(markgrid); markwidth=markheight=markskip=0; markgrid=0;
  free(markgrid2); markwidth2=markheight2=markskip2=0; markgrid2=0;
  if(x0>x1) a=x0,x0=x1,x1=a;
  if(y0>y1) a=y0,y0=y1,y1=a;
  if(x0>=w || y0>=h) return;
  if(!x0 && !y0 && x1==w-1 && y1==h-1) {
    clear_extra_stats();
    return;
  }
  w=x1+1-x0;
  h=y1+1-y0;
  b_under=calloc(w*h,3*sizeof(Tile));
  if(!b_under) err(1,"Allocation failed");
  b_main=b_under+w*h;
  b_over=b_main+w*h;
  a=x1+1-x0;
  for(x=0;x<w;x++) for(y=0;y<h;y++) {
    z=x+x0+(y+y0)*board_info.width;
    b_under[x+y*w]=ku[z];
    b_main[x+y*w]=km[z];
    b_over[x+y*w]=ko[z];
  }
  free(ku);
  board_info.width=w;
  board_info.height=h;
  xcur-=x0;
  ycur-=y0;
  for(x=0;x<maxstat;x++) if(stats[x].count && stats[x].xy) for(y=0;y<stats[x].count;y++) {
    stats[x].xy[y].x-=x0;
    stats[x].xy[y].y-=y0;
  }
  clear_extra_stats();
}

static void cc_debug_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  printf("(%d,%d:%d,%d)BEGIN\"%s\"",x0,y0,x1,y1,arg);
  printf("<%02X,%02X,%02X,%02X>#%d\n",cctile.color,cctile.kind,cctile.param,cctile.stat,cctmp);
  cctmp=0;
}

static void cc_debug_step(Uint16 x,Uint16 y,const char*arg) {
  printf("(%d,%d)\"%s\"",x,y,arg);
  printf("<%02X,%02X,%02X,%02X>#%d\n",cctile.color,cctile.kind,cctile.param,cctile.stat,cctmp);
  ++cctmp;
}

static void cc_debug_end(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  printf("(%d,%d:%d,%d)END\"%s\"",x0,y0,x1,y1,arg);
  printf("<%02X,%02X,%02X,%02X>#%d\n",cctile.color,cctile.kind,cctile.param,cctile.stat,cctmp);
}

static void cc_delete_step(Uint16 x,Uint16 y,const char*arg) {
  delete_at(x,y);
}

static void cc_deletex_step(Uint16 x,Uint16 y,const char*arg) {
  delete_at(x,y);
  delete_at(x,y);
}

static void cc_exchangelayer_step(Uint16 x,Uint16 y,const char*arg) {
  Tile*u=b_under+y*board_info.width+x;
  Tile*m=b_main+y*board_info.width+x;
  Tile t=*u;
  *u=*m;
  *m=t;
  if(u->stat==m->stat) return;
  if(u->stat && u->stat<=maxstat) find_stat(x,y,u->stat,2,1);
  if(m->stat && m->stat<=maxstat) find_stat(x,y,u->stat,1,2);
}

static void cc_export(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  char buf[75];
  const char*e;
  FILE*fp;
  if(ccrestrict || !*arg) return;
  if(*arg=='|') fp=popen(arg+1,"w"); else fp=fopen(arg,"wx");
  if(fp) {
    if(e=save_board(fp,0)) alert_text(e);
    if(*arg=='|') pclose(fp); else fclose(fp);
  } else {
    snprintf(buf,75,"%m");
    alert_text(buf);
  }
}

static void cc_floorplace_step(Uint16 x,Uint16 y,const char*arg) {
  Tile*u=b_under+y*board_info.width+x;
  Tile*m=b_main+y*board_info.width+x;
  if(u->kind) return;
  if(m->kind) {
    if(elem_def[m->kind].attrib&A_FLOOR) return;
    write_under(x,y,cctile);
  } else {
    write_at(x,y,cctile);
  }
}

static void cc_hflip(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Sint32 x,y;
  Uint32 a,b;
  Tile t;
  StatXY*sa;
  StatXY*sb;
  if(x0>x1) a=x0,x0=x1,x1=a;
  if(y0>y1) a=y0,y0=y1,y1=a;
  for(y=y0;y<=y1;y++) {
    for(x=x0;x<(x0+x1+1)/2;x++) {
      a=y*board_info.width+x;
      b=y*board_info.width+x0+x1-x;
      sa=find_stat(x,y,b_main[a].stat,2,2);
      sb=find_stat(x,y,b_main[b].stat,2,2);
      if(sa) sa->x=x0+x1-x;
      if(sb) sb->x=x;
      t=b_main[a]; b_main[a]=b_main[b]; b_main[b]=t;
      sa=find_stat(x,y,b_under[a].stat,1,1);
      sb=find_stat(x,y,b_under[b].stat,1,1);
      if(sa) sa->x=x0+x1-x;
      if(sb) sb->x=x;
      t=b_under[a]; b_under[a]=b_under[b]; b_under[b]=t;
    }
  }
}

static void cc_import(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  char buf[75];
  const char*e;
  FILE*fp;
  if(ccrestrict || !*arg) return;
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

static void cc_markonly_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  cctmp=(board_info.width+7)>>3;
  ccdata=calloc(cctmp,board_info.height);
  if(!ccdata) err(1,"Allocation failed");
}

static void cc_markonly_step(Uint16 x,Uint16 y,const char*arg) {
  ccdata[(x>>3)+y*cctmp]|=1<<(x&7);
}

static void cc_markonly_end(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  free(markgrid);
  markwidth=board_info.width;
  markheight=board_info.height;
  markskip=cctmp;
  markgrid=ccdata;
}

static void cc_ohflip(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Sint32 x,y;
  Uint32 a,b;
  Tile t;
  StatXY*sa;
  StatXY*sb;
  if(x0>x1) a=x0,x0=x1,x1=a;
  if(y0>y1) a=y0,y0=y1,y1=a;
  for(y=y0;y<=y1;y++) {
    for(x=x0;x<(x0+x1+1)/2;x++) {
      a=y*board_info.width+x;
      b=y*board_info.width+x0+x1-x;
      sa=find_stat(x,y,b_over[a].stat,3,3);
      sb=find_stat(x,y,b_over[b].stat,3,3);
      if(sa) sa->x=x0+x1-x;
      if(sb) sb->x=x;
      t=b_over[a]; b_over[a]=b_over[b]; b_over[b]=t;
    }
  }
}

static void cc_overcolor_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  if(*arg) cctmp=strtol(arg,0,16); else cctmp=overclip.color;
}

static void cc_overcolor_step(Uint16 x,Uint16 y,const char*arg) {
  Uint32 at=y*board_info.width+x;
  b_over[at].color=cctmp;
}

static void cc_overdelete_step(Uint16 x,Uint16 y,const char*arg) {
  over_delete_at(x,y);
}

static void cc_overplace_step(Uint16 x,Uint16 y,const char*arg) {
  over_place_at(x,y,overclip);
}

static void cc_ovflip(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Sint32 x,y;
  Uint32 a,b;
  Tile t;
  StatXY*sa;
  StatXY*sb;
  if(x0>x1) a=x0,x0=x1,x1=a;
  if(y0>y1) a=y0,y0=y1,y1=a;
  for(y=y0;y<(y0+y1+1)/2;y++) {
    for(x=x0;x<=x1;x++) {
      a=y*board_info.width+x;
      b=(y0+y1-y)*board_info.width+x;
      sa=find_stat(x,y,b_over[a].stat,3,3);
      sb=find_stat(x,y,b_over[b].stat,3,3);
      if(sa) sa->y=y0+y1+y;
      if(sb) sb->y=y;
      t=b_over[a]; b_over[a]=b_over[b]; b_over[b]=t;
    }
  }
}

static void cc_place_begin(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  if(*arg) cctile=read_tile(arg); else cctile=clip;
}

static void cc_place_step(Uint16 x,Uint16 y,const char*arg) {
  place_at(x,y,cctile);
}

static void cc_reseed(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  reseed(strtoll(arg,0,10));
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
      set_mark(x,y,0);
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

static void cc_vflip(Uint16 x0,Uint16 y0,Uint16 x1,Uint16 y1,const char*arg) {
  Sint32 x,y;
  Uint32 a,b;
  Tile t;
  StatXY*sa;
  StatXY*sb;
  if(x0>x1) a=x0,x0=x1,x1=a;
  if(y0>y1) a=y0,y0=y1,y1=a;
  for(y=y0;y<(y0+y1+1)/2;y++) {
    for(x=x0;x<=x1;x++) {
      a=y*board_info.width+x;
      b=(y0+y1-y)*board_info.width+x;
      sa=find_stat(x,y,b_main[a].stat,2,2);
      sb=find_stat(x,y,b_main[b].stat,2,2);
      if(sa) sa->y=y0+y1+y;
      if(sb) sb->y=y;
      t=b_main[a]; b_main[a]=b_main[b]; b_main[b]=t;
      sa=find_stat(x,y,b_under[a].stat,1,1);
      sb=find_stat(x,y,b_under[b].stat,1,1);
      if(sa) sa->y=y0+y1-y;
      if(sb) sb->y=y;
      t=b_under[a]; b_under[a]=b_under[b]; b_under[b]=t;
    }
  }
}

static void cc_write_step(Uint16 x,Uint16 y,const char*arg) {
  write_at(x,y,cctile);
}

static void cc_writeunder_step(Uint16 x,Uint16 y,const char*arg) {
  write_under(x,y,cctile);
}

static const ColonCommand colon_commands[]={
  {"b",0,cc_board,0,0,0},
  {"bi",0,cc_boardinfo,0,0,0},
  {"board",0,cc_board,0,0,0},
  {"boardinfo",0,cc_boardinfo,0,0,0},
  {"c",'.',0,cc_color_begin,cc_color_step,0},
  {"co",'%',0,cc_count_begin,cc_count_step,cc_count_end},
  {"color",'.',0,cc_color_begin,cc_color_step,0},
  {"count",'%',0,cc_count_begin,cc_count_step,cc_count_end},
  {"crop",'%',cc_crop,0,0,0},
  {"d",'.',0,0,cc_delete_step,0},
  {"debug",'.',0,cc_debug_begin,cc_debug_step,cc_debug_end},
  {"delete",'.',0,0,cc_delete_step,0},
  {"deletex",'.',0,0,cc_deletex_step,0},
  {"dx",'.',0,0,cc_deletex_step,0},
  {"ex",0,cc_export,0,0,0},
  {"exchangelayer",'.',0,0,cc_exchangelayer_step,0},
  {"export",0,cc_export,0,0,0},
  {"floorplace",'.',0,cc_place_begin,cc_floorplace_step,0},
  {"fp",'.',0,cc_place_begin,cc_floorplace_step,0},
  {"hflip",'%',cc_hflip,0,0,0},
  {"im",0,cc_import,0,0,0},
  {"import",0,cc_import,0,0,0},
  {"m",'.',0,0,cc_mark_step,0},
  {"mark",'.',0,0,cc_mark_step,0},
  {"markonly",'.',0,cc_markonly_begin,cc_markonly_step,cc_markonly_end},
  {"mo",'.',0,cc_markonly_begin,cc_markonly_step,cc_markonly_end},
  {"oc",'.',0,cc_overcolor_begin,cc_overcolor_step,0},
  {"od",'.',0,0,cc_overdelete_step,0},
  {"ohflip",'%',cc_ohflip,0,0,0},
  {"op",'.',0,0,cc_overplace_step,0},
  {"overcolor",'.',0,cc_overcolor_begin,cc_overcolor_step,0},
  {"overdelete",'.',0,0,cc_overdelete_step,0},
  {"overplace",'.',0,0,cc_overplace_step,0},
  {"ovflip",'%',cc_ovflip,0,0,0},
  {"p",'.',0,cc_place_begin,cc_place_step,0},
  {"place",'.',0,cc_place_begin,cc_place_step,0},
  {"reseed",0,cc_reseed,0,0,0},
  {"status",0,cc_status,0,0,0},
  {"t",'.',0,0,cc_toggle_step,0},
  {"toggle",'.',0,0,cc_toggle_step,0},
  {"u",'.',cc_unmark,0,cc_unmark_step,0},
  {"unmark",'.',cc_unmark,0,cc_unmark_step,0},
  {"vflip",'%',cc_vflip,0,0,0},
  {"w",'.',0,cc_place_begin,cc_write_step,0},
  {"write",'.',0,cc_place_begin,cc_write_step,0},
  {"writeunder",'.',0,cc_place_begin,cc_writeunder_step,0},
  {"wu",'.',0,cc_place_begin,cc_writeunder_step,0},
  {"xl",'.',0,0,cc_exchangelayer_step,0},
};

typedef struct {
  Sint32 arg[12];
  Tile tile;
  Uint8 narg,kind,inv;
} Filter;

static int cf_copy_color(Uint16 x,Uint16 y,Filter*f) {
  Uint32 at=y*board_info.width+x;
  cctile.color=b_main[at].color;
  return 1;
}

static int cf_copy_tile(Uint16 x,Uint16 y,Filter*f) {
  Uint32 at=y*board_info.width+x;
  cctile=b_main[at];
  return 1;
}

static int cf_mark(Uint16 x,Uint16 y,Filter*f) {
  if(x>=markwidth || y>=markheight) return 0;
  return (markgrid[y*markskip+(x>>3)]&(1<<(x&7)));
}

static int cf_mark2(Uint16 x,Uint16 y,Filter*f) {
  if(x>=markwidth2 || y>=markheight2) return 0;
  return (markgrid2[y*markskip2+(x>>3)]&(1<<(x&7)));
}

static int cf_modulo(Uint16 x,Uint16 y,Filter*f) {
  Sint32 z=(x*f->arg[0]+y*f->arg[1])%(f->arg[2]?:1);
  int i;
  if(z<0) z+=f->arg[2];
  for(i=3;i<f->narg;i++) if(z==f->arg[i]) return 1;
  return 0;
}

static int cf_random(Uint16 x,Uint16 y,Filter*f) {
  return dice(f->narg?(f->arg[0]?:2):2)<(f->narg>1?f->arg[1]:1);
}

static int cf_stat(Uint16 x,Uint16 y,Filter*f) {
  Uint32 at=y*board_info.width+x;
  if(f->narg) return (b_under[at].stat==f->arg[0] || b_main[at].stat==f->arg[0]);
  return (b_under[at].stat || b_main[at].stat);
}

static int cf_tile(Uint16 x,Uint16 y,Filter*f) {
  Uint32 at=y*board_info.width+x;
  if((f->arg[0]&4) && b_main[at].kind==f->tile.kind) {
    if((f->arg[0]&1) && b_main[at].color!=f->tile.color) return 0;
    if((f->arg[0]&2) && b_main[at].param!=f->tile.param) return 0;
    return 1;
  } else if((f->arg[0]&8) && b_under[at].kind==f->tile.kind) {
    if((f->arg[0]&1) && b_under[at].color!=f->tile.color) return 0;
    if((f->arg[0]&2) && b_under[at].param!=f->tile.param) return 0;
    return 1;
  }
  return 0;
}

typedef int(*FilterCode)(Uint16 x,Uint16 y,Filter*f);

static const FilterCode filtcode[127]={
  ['&']=cf_mark,
  ['?']=cf_tile,
  ['C']=cf_copy_color,
  ['T']=cf_copy_tile,
  ['m']=cf_modulo,
  ['r']=cf_random,
  ['s']=cf_stat,
  ['x']=cf_mark2,
};

static void do_colon_command(char*text) {
  Filter fil[12];
  Uint8 nfil=0;
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
  if(!*text) {
    if(x0!=x1 || y0!=y1) goto syntax;
    xcur=x0;
    ycur=y0;
    return;
  }
  while(nfil<12) {
    fil[nfil].narg=fil[nfil].kind=fil[nfil].inv=0;
    if(*text=='~') ++text,fil[nfil].inv=1;
    if(*text=='[') {
      nex=strchr(text,']');
      if(!nex) goto syntax;
      *nex=0;
      fil[nfil].arg[0]=12;
      if(text[1]=='=') fil[nfil].arg[0]=4,text++;
      if(text[1]=='_') fil[nfil].arg[0]=8,text++;
      fil[nfil].tile=read_tile(text+1);
      fil[nfil].narg=1;
      fil[nfil].kind='?';
      if(text[1]=='<') fil[nfil].arg[0]|=1;
      if(nex[-1]=='>') fil[nfil].arg[0]|=2;
      text=nex+1;
    } else if(*text=='&') {
      fil[nfil].kind=*text++;
    } else if(*text=='+' || *text=='-') {
      fil[nfil].kind='+';
      fil[nfil].narg=2;
      fil[nfil].arg[0]=strtol(text,&text,10);
      if(*text!='+' && *text!='-') goto syntax;
      fil[nfil].arg[1]=strtol(text,&text,10);
    } else if(*text=='/') {
      if((text[1]<'a' || text[1]>'z') && (text[1]<'A' || text[1]>'Z')) goto syntax;
      fil[nfil].kind=text[1];
      text+=2;
      for(i=0;i<12;i++) {
        if(*text!='-' && *text!='+' && (*text<'0' || *text>'9')) break;
        fil[nfil].arg[i]=strtol(text,&text,10);
        fil[nfil].narg=i+1;
        if(*text!=',') break;
        ++text;
      }
    } else {
      break;
    }
    nfil++;
  }
  if(nfil && !ra) {
    ra='%';
    x0=y0=0;
    x1=board_info.width-1;
    y1=board_info.height-1;
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
    if(ra=='.') ra='%'; else x0=y0=0,x1=board_info.width-1,y1=board_info.height-1;
  }
  ccerror=0;
  if(nfil || ra=='~' || ra=='&') {
    if(!found->step) { alert_text("Filters/marks cannot be used with this command"); return; }
    if(found->begin) found->begin(x0,y0,x1,y1,text);
    if(ra!='&' || markgrid) for(x=x0,y=y0;;) {
      if(ccerror) return;
      ox=x,oy=y;
      if(ra=='&') {
        if(x>=markwidth || y>=markheight) goto nomatch;
        if(!(markgrid[y*markskip+(x>>3)]&(1<<(x&7)))) goto nomatch;
      } else if(ra=='~' && x<markwidth && y<markheight) {
        if(markgrid[y*markskip+(x>>3)]&(1<<(x&7))) goto nomatch;
      }
      for(i=0;i<nfil;i++) {
        if(fil[i].kind=='+') {
          if(((x+fil[i].arg[0])|(y+fil[i].arg[1]))&~0xFFFF) goto nomatch;
          x+=fil[i].arg[0];
          y+=fil[i].arg[1];
        } else if(fil[i].kind=='b') {
          if(fil[i].inv^((x0==x || x1==x || y0==y || y1==y)?0:1)) goto nomatch;
        } else {
          if(!filtcode[fil[i].kind]) { alert_text("Unknown filter type"); return; }
          if((filtcode[fil[i].kind](x,y,fil+i)?0:1)^fil[i].inv) goto nomatch;
        }
      }
      if(x>=board_info.width || y>=board_info.height) goto nomatch;
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

static void do_escaped_colon_command(char*t) {
  char*p;
  char b[128];
  int n=0;
  int c;
  while(*t && n<127) {
    p=strchrnul(t,'`');
    if(n+(p-t)>127) goto big;
    if(t!=p) memcpy(b+n,t,p-t);
    n+=p-t;
    t=p;
    if(*t=='`') {
      if(n>99) goto big;
      switch(c=*++t) {
        case '`': b[n++]='`'; break;
        case 'A' ... 'H':
          if(regs[c-'A']<0) return;
          n+=snprintf(b+n,128-n,"%ld",(long)(regs[c-'A']));
          break;
        case 'a' ... 'h':
          n+=snprintf(b+n,128-n,"%+ld",(long)(regs[c-'a']));
          break;
      }
      if(*t) t++;
    }
  }
  if(*t || n>127) {
    big: alert_text("Too long escaped colon command");
    return;
  }
  b[n]=0;
  if(n) do_colon_command(b);
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

static void show_marks(void) {
  int x,y;
  Uint8*g;
  for(y=0;y<25;y++) {
    if(y+scroll_y>=markheight) break;
    g=markgrid+markskip*(y+scroll_y);
    for(x=0;x<80;x++) {
      if(x+scroll_x>=markwidth) break;
      if(g[(x+scroll_x)>>3]&(1<<((x+scroll_x)&7))) v_color[y*80+x]=(config.mark_color?:~v_color[y*80+x]);
    }
  }
}

static void show_selection(void) {
  Sint32 x0=xcur-scroll_x;
  Sint32 y0=ycur-scroll_y;
  Sint32 x1=xcur2-scroll_x;
  Sint32 y1=ycur2-scroll_y;
  Sint32 z,x;
  if(x0>x1) z=x0,x0=x1,x1=z;
  if(y0>y1) z=y0,y0=y1,y1=z;
  if(x0<0) x0=0;
  if(y0<0) y0=0;
  if(x1>79) x1=79;
  if(y1>24) y1=24;
  for(z=y0*80;y0<=y1;y0++,z+=80) for(x=x0;x<=x1;x++) v_color[z+x]=(config.block_color?:~v_color[z+x]);
}

static void update_over_screen(void) {
  int a,b,x,y;
  memset(v_color,0x01,80*25);
  memset(v_char,177,80*25);
  for(y=0;y<25;y++) {
    if(y+scroll_y>=board_info.height) break;
    a=y*80;
    b=(y+scroll_y)*board_info.width+scroll_x;
    for(x=0;x<80;x++) {
      if(x+scroll_x>=board_info.width) break;
      v_char[a+x]=b_over[b+x].param;
      v_color[a+x]=b_over[b+x].color;
    }
  }
}

static void ed_update_over_screen(void) {
  Uint8*g=markgrid;
  int a,b,x,y;
  memset(v_color,0x01,80*25);
  memset(v_char,177,80*25);
  for(y=0;y<25;y++) {
    if(y+scroll_y>=board_info.height) break;
    if(y+scroll_y>=markheight) g=0; else if(g) g=markgrid+markskip*(y+scroll_y);
    a=y*80;
    b=(y+scroll_y)*board_info.width+scroll_x;
    for(x=0;x<80;x++) {
      if(x+scroll_x>=board_info.width) break;
      v_color[a+x]=0x08;
      v_char[a+x]=0xF9;
      if(g && x+scroll_x<markwidth && (g[(x+scroll_x)>>3]&(1<<((x+scroll_x)&7)))) v_color[a+x]^=0x20;
      if(b_main[b+x].kind || b_under[b+x].kind) v_color[a+x]^=0x10;
      if(b_over[b+x].kind&OVER_VISIBLE) v_char[a+x]='*',v_color[a+x]^=0x02;
      if(b_over[b+x].kind&OVER_BG_THRU) v_color[a+x]^=0x01;
      if(b_over[b+x].kind&OVER_SOLID) v_char[a+x]='#',v_color[a+x]^=0x04;
    }
  }
}

static void ed_update_screen(void) {
  Uint8*g=markgrid;
  int a,b,x,y;
  memset(v_color,0x01,80*25);
  memset(v_char,177,80*25);
  for(y=0;y<25;y++) {
    if(y+scroll_y>=board_info.height) break;
    if(y+scroll_y>=markheight) g=0; else if(g) g=markgrid+markskip*(y+scroll_y);
    a=y*80;
    b=(y+scroll_y)*board_info.width+scroll_x;
    for(x=0;x<80;x++) {
      if(x+scroll_x>=board_info.width) break;
      v_color[a+x]=0x08;
      if(g && x+scroll_x<markwidth && (g[(x+scroll_x)>>3]&(1<<((x+scroll_x)&7)))) {
        v_char[a+x]=0x0F;
      } else if(!b_main[b+x].kind) {
        v_char[a+x]=0xF9;
      } else if(b_main[b+x].kind==clip.kind) {
        v_char[a+x]=(elem_def[b_main[b+x].kind].attrib&A_FLOOR?'+':'@');
      } else if(clip.kind && b_under[b+x].kind==clip.kind) {
        v_char[a+x]=(elem_def[b_main[b+x].kind].attrib&A_FLOOR?'-':'%');
      } else if(b_main[b+x].stat && b_main[b+x].stat<10) {
        v_char[a+x]=b_main[b+x].stat+'0';
      } else {
        v_char[a+x]=(elem_def[b_main[b+x].kind].attrib&A_FLOOR?'=':'#');
      }
      if(b_main[b+x].kind) v_color[a+x]^=0x01;
      if(!(elem_def[b_main[b+x].kind].attrib&A_FLOOR)) v_color[a+x]^=0x02;
      if(b_main[b+x].stat) v_color[a+x]^=0x07;
      if(b_under[b+x].kind) v_color[a+x]^=0x10;
      if(!(elem_def[b_under[b+x].kind].attrib&A_FLOOR)) v_color[a+x]^=0x20;
      if(b_under[b+x].stat) v_color[a+x]^=0x70;
    }
  }
}

static Uint8 parameter_calc(Uint8 par,Uint8 sta,const char*calc) {
  Sint32 w=par;
  Sint32 z=par;
  Sint32 v;
  int c;
  for(;;) switch(c=*calc++) {
    case 0: return par;
    case 'A' ... 'H': regs[c-'A']=w; break;
    case 'K' ... 'N': clip.values[c-'K']=w; break;
    case 'P': par=w; break;
    case 'S': sta=w; break;
    case 'T': if(config.default_colors) clip.color=w; break;
    case 'X': if(w>=0 && w<board_info.width) xcur=w; break;
    case 'Y': if(w>=0 && w<board_info.height) ycur=w; break;
    case 'a' ... 'h': z=w; w=regs[c-'a']; break;
    case 'k' ... 'n': z=w; w=clip.values[c-'k']; break;
    case 'p': z=w; w=par; break;
    case 's': z=w; w=sta; break;
    case 't': z=w; w=clip.color; break;
    case 'x': z=w; w=xcur; break;
    case 'y': z=w; w=ycur; break;
    case '.': v=w; w=z; z=v; break;
    case '0' ... '9': z=c-'0'; break;
    case '+': w+=z; break;
    case '-': w-=z; break;
    case '*': w*=z; break;
    case '&': w&=z; break;
    case '|': w|=z; break;
    case '^': w^=z; break;
    case '<': w<<=z&31; break;
    case '>': w>>=z&31; break;
    case '!': if(w) return par; break;
    case '?': if(!w) return par; break;
  }
}

static Sint32 parameter_which_get(Uint16 which,Uint8 par,Uint8 sta,StatXY*sxy) {
  Uint32 v=0;
  switch((which>>8)&0x7F) {
    case 0x00 ... 0x0F: v=par; break;
    case 0x10: if(sta && sta<=maxstat) v=stats[sta-1].speed; else return -1; break;
    case 0x11: if(sta && sta<=maxstat) v=stats[sta-1].misc1; else return -1; break;
    case 0x12: if(sta && sta<=maxstat) v=stats[sta-1].misc2; else return -1; break;
    case 0x13: if(sta && sta<=maxstat) v=stats[sta-1].misc3; else return -1; break;
    case 0x20: if(sxy) v=sxy->delay; else return -1; break;
    case 0x21: if(sxy) v=sxy->instptr; else return -1; break;
    case 0x22: if(sxy) v=sxy->layer&0xC0; else return -1; break;
    case 0x30: v=board_info.flag; break;
    case 0x31: v=board_info.userdata; break;
    case 0x32: v=board_info.screen; break;
    case 0x40 ... 0x43: v=overclip.values[(which>>8)&3]; break;
    case 0x38 ... 0x3B: v=board_info.exits[(which>>8)&3]; break;
    case 0x70 ... 0x77: v=regs[(which>>8)&7]; break;
    default: return -1;
  }
  if(which&0x8000) v=~v;
  v>>=which&15;
  v&=(2UL<<((which>>4)&15))-1;
  return v&0xFFFF;
}

static Uint8 parameter_edit(Uint16 addr,Uint8 par,Uint8 sta,StatXY*sxy) {
  char ok;
  Uint16 wst,opv,opw;
  Sint32 u,v;
  StatXY rxy;
  int i,j;
  if(addr<0x100) return par;
  if(addr<0x200) return ask_color_char(1,par);
  if(sxy) rxy=*sxy; else rxy=(StatXY){};
  normal: for(;;) {
    switch(memory[addr]&0xFF) {
      case 0: return par;
      case 1: addr+=2; break;
      case 3:
        i=memory[addr++]>>8;
        while(i--) if((memory[addr++]&0xFF)==par) *regs=memory[addr-1]>>8;
        break;
      case 4: regs[(memory[addr]>>8)&7]=memory[addr+1]; addr+=2; break;
      case 5:
        u=regs[(memory[addr]>>8)&7];
        v=parameter_which_get(memory[addr+1],par,sta,sxy);
        if(v!=-1) {
          if(memory[addr]&0x2000) regs[(memory[addr]>>8)&7]=v+(memory[addr]&0x4000?u:0);
          if(memory[addr]&0x1000) {
            v=u+(memory[addr]&0x4000?v:0);
            if(memory[addr+1]&0x8000) v=~v;
            switch(memory[addr+1]>>8) {
              case 0x00 ... 0x0F: par=v; break;
              case 0x10: stats[sta-1].speed=v; break;
              case 0x11: stats[sta-1].misc1=v; break;
              case 0x12: stats[sta-1].misc2=v; break;
              case 0x13: stats[sta-1].misc3=v; break;
              case 0x20: rxy.delay=sxy->delay=v; break;
              case 0x21: rxy.instptr=sxy->instptr=v; break;
              case 0x22: rxy.layer=sxy->layer=(sxy->layer&0x03)+(v&0xFC); break;
              case 0x30: board_info.flag=v; break;
              case 0x31: board_info.userdata=v; break;
              case 0x32: board_info.screen=v; break;
              case 0x40 ... 0x43: overclip.values[(memory[addr+1]>>8)&3]=v; break;
              case 0x38 ... 0x3B: board_info.exits[(memory[addr+1]>>8)&3]=v; break;
            }
          }
        }
        addr+=2;
        break;
      case ':': case ';':
        if(ccrestrict) errx(1,"Recursive colon commands");
        if(memory[addr+1]<ngtext && memory[addr+1]>0) {
          ccrestrict=1;
          if((memory[addr]&0xFF)==';') do_escaped_colon_command(gtext[memory[addr+1]]); else do_colon_command(gtext[memory[addr+1]]);
          ccrestrict=0;
        }
        sxy=0;
        addr+=2;
        break;
      case '=': if(memory[addr+1]<ngtext) par=parameter_calc(par,sta,gtext[memory[addr+1]]); addr+=2; break;
      case '@':
        if(memory[addr+1]>=ngtext) errx(1,"Invalid string number");
        if(memory[addr+1]) {
          for(i=0;i<maxstat;i++) {
            if(stats[i].text && stats[i].text[0]=='@') {
              for(j=0;;j++) {
                if((stats[i].text[j+1]=='=' || stats[i].text[j+1]=='\n' || !stats[i].text[j+1]) && (gtext[memory[addr+1]][j]=='=' || gtext[memory[addr+1]][j]=='\n' || !gtext[memory[addr+1]][j])) goto found;
                if(stats[i].text[j+1]=='=' || stats[i].text[j+1]=='\n' || !stats[i].text[j+1] || gtext[memory[addr+1]][j]=='=' || gtext[memory[addr+1]][j]=='\n' || !gtext[memory[addr+1]][j]) break;
                if((stats[i].text[j+1]^gtext[memory[addr+1]][j])&0xDF) break;
              }
            }
          }
        } else {
          i=maxstat;
        }
        found: sta=i+1;
        if(i==maxstat) {
          if(i==255) return par;
          new_stat();
          stats[i].speed=memory[addr+2];
          stats[i].length=strlen(gtext[memory[addr+1]])+2;
          stats[i].text=malloc(stats[i].length+1);
          if(!stats[i].text) err(1,"Allocation failed");
          snprintf(stats[i].text,stats[i].length+1,"@%s\n",gtext[memory[addr+1]]);
        }
        addr+=3;
        break;
      case 'C': par=ask_color_char(1,par); addr++; break;
      case 'E':
        if(sta && sta<=maxstat) {
          stats[sta-1].text=text_editor(stats[sta-1].text);
          stats[sta-1].length=(stats[sta-1].text?strlen(stats[sta-1].text):0);
        }
        addr++;
        break;
      case 'H': wst=addr; goto form;
      case 'P':
        cctile=clip;
        if(memory[addr+1]!=0xFFFF) cctile.kind=memory[addr+1];
        if(memory[addr+2]&0x800) cctile.stat=sta;
        if(memory[addr+2]&0x1000) cctile.param=par;
        if(config.default_colors) {
          switch((memory[addr+2]>>8)&7) {
            case 1: cctile.color=(cctile.color&0xF0)|(memory[addr+2]&0x0F); break;
            case 2: cctile.color=(cctile.color&0x0F)|(memory[addr+2]&0xF0); break;
            case 3: cctile.color=(memory[addr+2]&0xFF); break;
            case 4: if(cctile.color<0x10 || (cctile.color&0x0F)!=(memory[addr+2]&0x0F)) cctile.color=((cctile.color&7)<<4)|(memory[addr+2]&0x8F); break;
            case 5: cctile.color=(cctile.color&7)*0x11+(memory[addr+2]&0x88); break;
          }
        }
        if(memory[addr+2]&0x8000) write_at(xcur,ycur,cctile); else place_at(xcur,ycur,cctile);
        if(memory[addr+2]&0x4000) clip=cctile;
        sxy=(cctile.stat?find_stat(xcur,ycur,cctile.stat,2,2):0);
        if(sxy && (memory[addr+2]&0x2000)) {
          sxy->instptr=rxy.instptr;
          sxy->delay=rxy.delay;
          sxy->layer|=rxy.layer&0xC0;
        }
        if(sxy) rxy=*sxy;
        addr+=3;
        break;
      case 'S': if(memory[addr+1]==0xFFFF) sta=clip.stat; else sta=memory[addr+1]; addr+=2; break;
      case 'V':
        cctile.kind=memory[addr+1]; cctile.color=memory[addr+2]; cctile.param=memory[addr+3]; cctile.stat=0;
        for(i=0;i<board_info.width;i++) for(j=0;j<board_info.height;j++) over_place_at(i,j,cctile);
        addr+=4;
        break;
      default: errx(1,"Improper instruction: $%04X at $%04X",memory[addr],addr);
    }
  }
  form: win_form("Parameter edit") {
    addr=wst; opv=0; opw=0xFFFF; ok=1;
    item:
    switch(memory[addr]&0xFF) {
      case '/':
        v=parameter_which_get(memory[addr+1],par,sta,sxy);
        if(v<0) ok=0; else ok=(memory[addr+2]>>(v>15?15:v))&1;
        addr+=3;
        break;
      case 'B'+128:
        addr+=3;
        if(ok && memory[addr-2]<ngtext && (v=parameter_which_get(j=(memory[addr-1]&0xFF00)|0xF0,par,sta,sxy))>=0) {
          win_boolean(memory[addr-3]>>8,gtext[memory[addr-2]],v,1UL<<(memory[addr-1]&15)) goto store;
        }
        break;
      case 'C'+128:
        addr+=3;
        if(ok && memory[addr-2]<ngtext && (v=parameter_which_get(j=memory[addr-1],par,sta,sxy))>=0) {
          win_char(memory[addr-3]>>8,gtext[memory[addr-2]],v) goto store;
        }
        break;
      case 'H': if(ok && memory[addr+1]<ngtext) win_heading(gtext[memory[addr+1]]); addr+=2; break;
      case 'L'+128:
        addr+=3;
        if(ok && memory[addr-2]<ngtext && (v=parameter_which_get(j=memory[addr-1],par,sta,sxy))>=0) {
          if((j&0xFF)<0x30) v|=8;
          win_color(memory[addr-3]>>8,gtext[memory[addr-2]],v) goto store;
        }
        break;
      case 'M':
        addr+=2;
        if(ok) {
          j=memory[addr-1];
          v=0;
          goto store;
        }
        break;
      case 'N'+128:
        addr+=5;
        if(ok && memory[addr-4]<ngtext && (v=parameter_which_get(j=memory[addr-3],par,sta,sxy))>=0) {
          win_numeric(memory[addr-5]>>8,gtext[memory[addr-4]],v,memory[addr-2],memory[addr-1]) goto store;
        }
        break;
      case 'O':
        if(ok) opv=parameter_which_get(opw=memory[addr+1],par,sta,sxy);
        if(v<0) opw=0xFFFF;
        addr+=2;
        break;
      case 'O'+128:
        addr+=3;
        if(ok && opw!=0xFFFF && memory[addr-2]<ngtext) win_option(memory[addr-3]>>8,gtext[memory[addr-2]],opv,memory[addr-1]) {
          j=opw;
          v=opv;
          goto store;
        }
        break;
      default: goto endform;
    }
    goto item;
    store:
    u=parameter_which_get(0xF0|j&0x7FF0,par,sta,sxy);
    if(u<0) goto item;
    if(j&0x8000) v=~v;
    v&=(2UL<<((j>>4)&15))-1;
    v<<=j&15;
    v|=u&~(((2UL<<((j>>4)&15))-1)<<(j&15));
    switch((j>>8)&0x7F) {
      case 0x00 ... 0x0F: par=v; break;
      case 0x10: stats[sta-1].speed=v; break;
      case 0x11: stats[sta-1].misc1=v; break;
      case 0x12: stats[sta-1].misc2=v; break;
      case 0x13: stats[sta-1].misc3=v; break;
      case 0x20: rxy.delay=sxy->delay=v; break;
      case 0x21: rxy.instptr=sxy->instptr=v; break;
      case 0x22: rxy.layer=sxy->layer=(sxy->layer&0x3F)+(v&0xC0); break;
      case 0x30: board_info.flag=v; break;
      case 0x31: board_info.userdata=v; break;
      case 0x32: board_info.screen=v; break;
      case 0x38 ... 0x3B: board_info.exits[(j>>8)&3]=v; break;
      case 0x70 ... 0x77: regs[(j>>8)&7]=v; break;
    }
    goto item;
    endform:
    win_blank();
    win_command_esc(0,"Done") goto normal;
  }
}

static void do_revealing_list(Uint16 a0) {
  Uint16 a,m,u,v;
  Uint32 x,y,z;
  Tile t;
  if(!vmode) update_screen(); else ed_update_screen();
  for(y=0;y<25;y++) {
    if(y+scroll_y>=board_info.height) break;
    z=(y+scroll_y)*board_info.width+scroll_x;
    for(x=0;x<80;x++) {
      if(x+scroll_x>=board_info.width) break;
      for(a=a0;m=memory[a]&0x7FF;a+=2) {
        u=(memory[a]>>12)&3;
        tryagain:
        t=(u==3?b_over:u==2?b_main:b_under)[z+x];
        v=0;
        switch(m) {
          case 0x001: v=1; break;
          case 0x002: v=t.stat; break;
          case 0x004: v=!set_mark(x+scroll_x,y+scroll_y,2); break;
          case 0x005: v=set_mark(x+scroll_x,y+scroll_y,2); break;
          case 0x010 ... 0x01F: v=!((elem_def[t.kind].attrib^m)&15); break;
          case 0x100 ... 0x1FF: v=(t.kind==(m&0xFF)); break;
          case 0x200 ... 0x21F: v=!((elem_def[t.kind].attrib>>(m&31))&1); break;
          case 0x280 ... 0x29F: v=((elem_def[t.kind].attrib>>(m&31))&1); break;
          case 0x300 ... 0x3FF: v=(t.stat==(m&0xFF)); break;
        }
        if(!v) {
          if(u) continue;
          u=2;
          goto tryagain;
        }
        if(memory[a]&0x8000) v_color[y*80+x]^=memory[a+1]>>8; else if(memory[a+1]&0xFF00) v_color[y*80+x]=memory[a+1]>>8; else v_color[y*80+x]=t.color;
        if(memory[a]&0x4000) {
          switch(memory[a+1]&0xFF) {
            case 0x01: v_char[y*80+x]=t.param; break;
          }
        } else {
          v_char[y*80+x]=memory[a+1];
        }
        break;
      }
    }
  }
  redisplay();
  do { if(!next_event()) return; } while(event.type!=SDL_KEYDOWN);
}

static void f_menu(Uint16 f) {
  Uint8 b,c,x,y,z;
  Uint16 m;
  Uint32 k;
  f=memory[f+0x200];
  if(f<0x280 || emode=='v' || emode=='m') return;
  v_ycur=255;
  if(memory[f]) {
    m=f; x=0; y=2; z=0; c=0; b=255;
    while(memory[m]!=2 && b==255) {
      if(memory[m]==1) {
        if(c!=x) b=z-1; else if(y>2) y++;
        m+=2; c=x; z++;
      } else {
        m+=4;
      }
      if(y>=22) y=2,x=1; else y++;
    }
    if(x) {
      draw_border(0x1B,10,1,40,23);
      draw_border(0x1B,40,1,70,23);
      v_char[40+1*80]=0xC2;
      v_char[40+23*80]=0xC1;
      x=12;
    } else {
      draw_border(0x1B,25,1,55,23);
      x=27;
    }
    m=f; y=2; z=0;
    while(memory[m]!=2) {
      if(memory[m]==1) {
        if(b==z++) y=2,x=42; else if(y>2) y++;
        if(memory[m+1]<ngtext) draw_text(x,y,gtext[memory[m+1]],0x1F,29);
        m+=2;
      } else {
        draw_text(x,y," ? ",y&1?0x70:0x30,3);
        v_char[x+1+y*80]=memory[m];
        if(memory[m+1]<ngtext) draw_text(x+4,y,gtext[memory[m+1]],0x1E,24);
        m+=4;
      }
      if(y>=22) y=2,x=42; else y++;
    }
    redisplay();
    do { if(!next_event()) return; } while(event.type!=SDL_KEYDOWN);
    k=event.key.keysym.unicode;
    if(!k) return;
    if(k>='a' && k<='z') k+='A'-'a';
    m=f;
    while(memory[m]!=k) {
      if(memory[m]==2) return;
      if(memory[m]==1) m+=2; else m+=4;
    }
  } else {
    m=f-1;
  }
  if(memory[m+3]&0x8000) {
    if(memory[m+3]&0xC000) {
      do_revealing_list(memory[m+2]);
      return;
    }
    parameter_edit(memory[m+2],memory[m+3]&0xFF,memory[m+3]&0x100?clip.stat:0,0);
    if((memory[m+3]&0x400) && clip.kind==b_main[ycur*board_info.width+xcur].kind) {
      f=memory[clip.kind+0x100];
      if(f>=0x200 && memory[f]==1) clip.param=b_main[ycur*board_info.width+xcur].param=memory[f+1];
    }
    if((memory[m+3]&0x200) && clip.kind==b_main[ycur*board_info.width+xcur].kind) {
      clip.param=b_main[ycur*board_info.width+xcur].param=parameter_edit(memory[clip.kind+0x100],clip.param,clip.stat,find_stat(xcur,ycur,clip.stat,2,2));
    }
    set_apparent_clip();
    return;
  }
  f=memory[(memory[m+2]&0xFF)+0x100];
  if(memory[m+3]&0x800) {
    if(maxstat==255) {
      alert_text("Too many stats");
      return;
    }
    clip.stat=new_stat();
    if(f>=0x200 && memory[f]==1) stats[clip.stat].speed=memory[f+1]>>8;
  } else {
    clip.stat=memory[m+2]>>8;
  }
  clip.kind=memory[m+2]&255;
  if(config.default_colors) {
    switch((memory[m+3]>>8)&7) {
      case 1: clip.color=(clip.color&0xF0)|(memory[m+3]&0x0F); break;
      case 2: clip.color=(clip.color&0x0F)|(memory[m+3]&0xF0); break;
      case 3: clip.color=(memory[m+3]&0xFF); break;
      case 4: if(clip.color<0x10 || (clip.color&0x0F)!=(memory[m+3]&0x0F)) clip.color=((clip.color&7)<<4)|(memory[m+3]&0x8F); break;
      case 5: clip.color=(clip.color&7)*0x11+(memory[m+3]&0x88); break;
    }
  }
  if(memory[m+3]&0x1000) {
    clip.param=0;
  } else if(f>=0x200) {
    if(memory[f]==1) clip.param=memory[f+1]; else clip.param=0;
  } else if(f>=0x100) {
    clip.param=ask_color_char(1,f&0xFF);
  } else {
    clip.param=f;
  }
  if(memory[m+3]&0x1000) {
    xcur2=xcur;
    if(emode=='t') emode=0; else emode='t';
  } else {
    place_at(xcur,ycur,clip);
    if(f>=0x200) clip.param=b_main[ycur*board_info.width+xcur].param=parameter_edit(f,clip.param,clip.stat,find_stat(xcur,ycur,clip.stat,2,2));
  }
  set_apparent_clip();
}

typedef struct {
  Uint8 n;
  Uint16 c[31];
} Gradient;
#define GRADIENT1(a) {9,{0x000+a,0x100+a,0x200+a,0x300+a,0x400+a,0x108+a*0x11,0x208+a*0x11,0x308+a*0x11,0x408+a*0x11}}

static const Gradient grads[]={
  GRADIENT1(0),
  GRADIENT1(1),
  GRADIENT1(2),
  GRADIENT1(3),
  GRADIENT1(4),
  GRADIENT1(5),
  GRADIENT1(6),
  GRADIENT1(7),
  {13,{0x008,0x108,0x208,0x308,0x408,0x187,0x287,0x387,0x487,0x17F,0x27F,0x37F,0x47F}},
  {17,{0x002,0x102,0x202,0x302,0x402,0x12E,0x22E,0x32E,0x42E,0x1EA,0x2EA,0x3EA,0x4EA,0x1A0,0x2A0,0x3A0,0x4A0}},
  {17,{0x004,0x104,0x204,0x304,0x404,0x14C,0x24C,0x34C,0x44C,0x1CE,0x2CE,0x3CE,0x4CE,0x1EF,0x2EF,0x3EF,0x4EF}},
  {17,{0x002,0x102,0x202,0x302,0x402,0x12A,0x22A,0x32A,0x42A,0x1AE,0x2AE,0x3AE,0x4AE,0x1EF,0x2EF,0x3EF,0x4EF}},
  {21,{0x001,0x101,0x201,0x301,0x401,0x119,0x219,0x319,0x419,0x193,0x293,0x393,0x493,0x13B,0x23B,0x33B,0x43B,0x1BF,0x2BF,0x3BF,0x4BF}},
  {16,{0x400,0x401,0x402,0x403,0x404,0x405,0x406,0x407,0x408,0x409,0x40A,0x40B,0x40C,0x40D,0x40E,0x40F}},
  {16,{0x300,0x301,0x302,0x303,0x304,0x305,0x306,0x307,0x308,0x309,0x30A,0x30B,0x30C,0x30D,0x30E,0x30F}},
  {8,{0x308,0x319,0x32A,0x33B,0x34C,0x35D,0x36E,0x37F}},
  {8,{0x208,0x219,0x22A,0x23B,0x24C,0x25D,0x26E,0x27F}},
  {8,{0x108,0x119,0x12A,0x13B,0x14C,0x15D,0x16E,0x17F}},
  {6,{0x011,0x011,0x112,0x213,0x213,0x313}},
  {6,{0x000,0x102,0x10A,0x202,0x20A,0x302}},
  {8,{0x102,0x202,0x302,0x402,0x126,0x226,0x326,0x426}},
  {5,{0x17C,0x276,0x274,0x268,0x260}},
  {4,{0x21A,0x232,0x212,0x112}},
  {9,{0x27E,0x27A,0x27B,0x21B,0x239,0x279,0x23D,0x27D,0x27C}},
  {7,{0x271,0x371,0x118,0x218,0x201,0x101,0x001}},
  {7,{0x274,0x374,0x148,0x248,0x204,0x104,0x004}},
  {7,{0x272,0x372,0x128,0x228,0x202,0x102,0x002}},
  {7,{0x32A,0x22A,0x372,0x228,0x202,0x102,0x002}},
  {6,{0x34E,0x24E,0x14E,0x148,0x248,0x240}},
  {5,{0x339,0x239,0x139,0x21B,0x31B}},
  {7,{0x32A,0x22A,0x12A,0x122,0x302,0x202,0x228}},
  {4,{0x224,0x262,0x228,0x202}},
  {4,{0x000,0x108,0x207,0x30F}},
  {4,{0x000,0x104,0x20C,0x30E}},
  {4,{0x000,0x102,0x20A,0x30F}},
  {4,{0x000,0x101,0x209,0x30B}},
  {4,{0x000,0x108,0x206,0x30E}},
  {7,{0x000,0x108,0x000,0x207,0x000,0x308,0x000}},
  {7,{0x200,0x208,0x288,0x287,0x277,0x27F,0x2FF}},
  {11,{0x319,0x219,0x119,0x014,0x114,0x214,0x314,0x414,0x3C4,0x2C4,0x1C4}},
  {11,{0x319,0x219,0x119,0x013,0x113,0x213,0x313,0x413,0x3B3,0x2B3,0x1B3}},
  {11,{0x32A,0x22A,0x12A,0x026,0x126,0x226,0x326,0x426,0x3E6,0x2E6,0x1E6}},
  {11,{0x32A,0x22A,0x12A,0x024,0x124,0x224,0x324,0x424,0x3C4,0x2C4,0x1C4}},
  {11,{0x319,0x219,0x119,0x015,0x115,0x215,0x315,0x415,0x3D5,0x2D5,0x1D5}},
  {11,{0x37F,0x27F,0x17F,0x075,0x175,0x275,0x375,0x475,0x3D5,0x2D5,0x1D5}},
  {11,{0x37F,0x27F,0x17F,0x073,0x173,0x273,0x373,0x473,0x3B3,0x2B3,0x1B3}},
  {11,{0x35D,0x25D,0x15D,0x053,0x153,0x253,0x353,0x453,0x3B3,0x2B3,0x1B3}},
  {25,{0x102,0x102,0x102,0x102,0x102,0x102,0x108,0x202,0x208,0x206,0x10A,0x20A,0x102,0x102,0x102,0x102,0x102,0x102,0x108,0x202,0x208,0x206,0x10A,0x20A,0x103}},
  {23,{0x2CC,0x2CE,0x2EE,0x2EA,0x2AA,0x2AB,0x2BB,0x2B9,0x299,0x29D,0x2DD,0x2DC,0x2CC,0x2CE,0x2EE,0x2EA,0x2AA,0x2AB,0x2BB,0x2B9,0x299,0x29D,0x2DD}},
  {4,{0x226,0x26A,0x16A,0x326}},
  {9,{0x20A,0x22A,0x228,0x220,0x120,0x320,0x12A,0x128,0x362}},
  {9,{0x106,0x206,0x306,0x406,0x168,0x268,0x376,0x346,0x246}},
  {9,{0x226,0x162,0x168,0x16A,0x320,0x228,0x328,0x222,0x328}},
  {9,{0x20B,0x37F,0x33F,0x13F,0x13B,0x23F,0x27B,0x11B,0x11F}},
  {9,{0x006,0x106,0x206,0x306,0x102,0x202,0x302,0x402,0x3A2}},
  {5,{0x30E,0x36E,0x32E,0x33E,0x37E}},
  {5,{0x20E,0x26E,0x22E,0x23E,0x27E}},
  {5,{0x12B,0x122,0x128,0x228,0x328}},
  {15,{0x201,0x219,0x21B,0x20B,0x22B,0x23A,0x22A,0x22E,0x27E,0x26E,0x24E,0x27C,0x24C,0x244,0x204}},
  {5,{0x30F,0x31F,0x32F,0x33F,0x37F}},
  {5,{0x20F,0x21F,0x22F,0x23F,0x27F}},
  {6,{0x012,0x112,0x212,0x312,0x232,0x21A}},
  {31,{0x04C,0x14C,0x24C,0x34C,0x44C,0x36C,0x26C,0x16C,0x06E,0x16E,0x26E,0x36E,0x46E,0x32E,0x22E,0x12E,0x02A,0x12A,0x22A,0x32A,0x42A,0x33A,0x23A,0x13A,0x03A,0x13B,0x23B,0x33B,0x43B,0x31B,0x21B}},
  {31,{0x02A,0x12A,0x22A,0x32A,0x42A,0x33A,0x23A,0x13A,0x03A,0x13B,0x23B,0x33B,0x43B,0x31B,0x21B,0x11B,0x019,0x119,0x219,0x319,0x419,0x359,0x259,0x159,0x05D,0x15D,0x25D,0x35D,0x45D,0x34D,0x24D}},
  {11,{0x202,0x302,0x402,0x12A,0x22A,0x32A,0x42A,0x36E,0x26E,0x16E,0x06E}},
  {14,{0x415,0x315,0x215,0x351,0x311,0x301,0x111,0x118,0x218,0x318,0x408,0x308,0x208,0x108}},
  {9,{0x468,0x368,0x268,0x168,0x068,0x306,0x206,0x106,0x006}},
  {9,{0x104,0x204,0x304,0x404,0x14E,0x24E,0x34E,0x44E,0x44F}},
  {4,{0x300,0x301,0x361,0x31E}},
  {2,{0x400,0x40F}},
  {4,{0x306,0x306,0x326,0x326}},
  {3,{0x36A,0x30A,0x34A}},
  {3,{0x26A,0x20A,0x24A}},
  {4,{0x20C,0x26C,0x36C,0x46C}},
  {5,{0x111,0x119,0x113,0x11B,0x11F}},
  {5,{0x302,0x342,0x362,0x372,0x322}},
  {7,{0x328,0x228,0x128,0x02A,0x12A,0x22A,0x32A}},
  {5,{0x27B,0x37B,0x47B,0x33B,0x23B}},
  {4,{0x268,0x168,0x068,0x16C}},
  {16,{0x42A,0x32A,0x22A,0x12A,0x02A,0x12E,0x22E,0x32E,0x42E,0x34E,0x24E,0x14E,0x404,0x304,0x204,0x104}},
  {8,{0x101,0x201,0x301,0x401,0x11E,0x21E,0x31E,0x41E}},
  {31,{0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x608,0x607,0x707,0x807,0x70F,0x60F}},
  {5,{0x570,0x670,0x770,0x570,0x778}},
  {31,{0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x708,0x609,0x60A,0x60B,0x60C,0x60D,0x60E,0x60F}},
};

static int gradient_preset_menu(void) {
  char buf[8];
  int g=0;
  int n=0;
  int i,j,k,x,y;
  const int m=(sizeof(grads)/sizeof(*grads))-1;
  page:
  memset(v_color,0,80*25);
  memset(v_char,0,80*25);
  draw_text(0,24,"<ESC> Cancel <0-9> Select <RET> Accept <\x1B/\x1A> Page",15,-1);
  for(i=0;i<=m-g && i<48;i++) {
    draw_text(x=i<24?0:40,y=i%24,buf,7,snprintf(buf,8,"%02d:()",i+g));
    x+=4;
    for(j=0;j<grads[i+g].n;j++) {
      v_color[y*80+x+j]=grads[i+g].c[j];
      v_char[y*80+x+j]="\x20\xB0\xB1\xB2\xDB\x20\xFA\xF9\x07\xFE"[grads[i+g].c[j]>>8];
    }
    v_color[y*80+x+grads[i+g].n]=7;
    v_char[y*80+x+grads[i+g].n]=41;
  }
  for(;;) {
    draw_text(78,24,buf,0x1F,snprintf(buf,8,"%02d",n));
    redisplay();
    do { if(!next_event()) return 255; } while(event.type!=SDL_KEYDOWN);
    switch(k=(!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym) {
      case 0x08: n/=10; break;
      case 0x0D: if(n<=m) return n; break;
      case 0x1B: return 255;
      case '0' ... '9': n=(10*n+k-'0')%100; break;
      case -SDLK_LEFT: case -SDLK_PAGEUP: case '[': if(g) g-=24; goto page;
      case -SDLK_RIGHT: case -SDLK_PAGEDOWN: case ']': if(g<m-24) g+=24; goto page;
    }
  }
}

static void gradient_menu(Uint8 lay) {
  Uint32 w=board_info.width;
  Tile*b;
  Tile t;
  float f,g;
  Uint8 aff=0x17;
  Uint8 pat=0;
  Uint8 patn=0;
  Uint8 pats[42]={};
  Uint8 patrev=0;
  Uint8 sh=1;
  Uint8 rep=0;
  Uint8 repm=0;
  Uint8 chess=0;
  Uint8 divis=1;
  Uint8 xi=1;
  Uint8 yi=1;
  Uint8 km=0;
  Uint16 patso=0;
  Uint16 pateo=0;
  Uint16 x1=xcur2;
  Uint16 y1=ycur2;
  Uint16 x2=xcur;
  Uint16 y2=ycur;
  Uint16 patran=0;
  Uint32 patmax=0;
  Uint32 k;
  Sint32 i,x,y;
  void set_patmax(void) {
    switch(pat) {
      case 0: patmax=14; break;
      case 1: patmax=strlen(pats)?:1; break;
      case 2: patmax=grads[patn].n; break;
      case 3: patmax=(abs(xcur2-xcur)+1)*(abs(ycur2-ycur)+1); break;
    }
  }
  Uint32 patconv(float m) {
    Sint32 u,v;
    m/=divis;
    if(patran) m+=1.e-4*(dice(patran+1)-0.5*(patran+1));
    if(m<=0.0) m=0.0; else if(m>=1.0) m=1.0;
    if(rep>1) {
      m*=rep;
      if(repm && (1&(int)m)) m=1.0-fmod(m,1.0); else m=fmod(m,1.0);
      if(m<=0.0) m=0.0; else if(m>=1.0) m=1.0;
    }
    if(patrev) m=1.0-m;
    if(chess && ((x^y)&1)) m=1.0-m;
    u=patmax-patso-pateo;
    if(u<1) u=1;
    v=m*u;
    return (v<0?0:v>=u?u-1:v)+patso;
  }
  restart:
  win_form("Gradient") {
    win_help("gradient",0);
    win_command('S',"Shape...") win_form("Gradient shape") {
      win_help("gradient","s");
      win_heading("Coordinates:");
      win_numeric('X',"X1: ",x1,0,board_info.width-1);
      win_numeric('1',"Y1: ",y1,0,board_info.height-1);
      win_numeric('2',"X2: ",x2,0,board_info.width-1);
      win_numeric('Y',"Y2: ",y2,0,board_info.height-1);
      win_command('R',"Reverse") {
        x=x1; y=y1; x1=x2; y1=y2; x2=x; y2=y;
      }
      win_blank();
      win_heading("Shape:");
      win_option('L',"Linear gradient",sh,1) win_refresh();
      win_option('u',"Circular gradient",sh,8) win_refresh();
      win_option('E',"Euclidean radius",sh,2) win_refresh();
      win_option('C',"Chebyshev radius",sh,3) win_refresh();
      win_option('h',"Manhattan radius",sh,4) win_refresh();
      win_option('D',"Distance from line",sh,5) win_refresh();
      win_option('p',"Distance from points",sh,6) win_refresh();
      win_option('I',"Integer",sh,7) win_refresh();
      win_option('m',"Random",sh,0) win_refresh();
      win_blank();
      if("   !  !! "[sh]&1) win_numeric('f',"X factor: ",xi,0,255);
      if("   !  !! "[sh]&1) win_numeric('a',"Y factor: ",yi,0,255);
      if(sh) win_numeric('v',"Divisor",divis,1,255);
      win_blank();
      win_command_esc(0,"Done") break;
    }
    win_command('P',"Pattern...") win_form("Gradient pattern") {
      win_help("gradient","p");
      win_heading("Pattern type:");
      win_option('c',"Current color",pat,0) win_refresh();
      win_option('w',"Current color with string",pat,1) win_refresh();
      win_option('P',"Preset",pat,2) win_refresh();
      win_option('B',"Block area",pat,3) win_refresh();
      win_command('e',"Preset menu") {
        if((patn=gradient_preset_menu())!=255) pat=2; else patn=0;
        win_refresh();
      }
      win_blank();
      switch(pat) {
        case 1: win_text('s',"Pattern string: ",pats) win_refresh(); break;
        case 2: win_numeric('S',"Select preset: ",patn,0,(sizeof(grads)/sizeof(*grads))-1) win_refresh(); break;
      }
      win_numeric('o',"Start offset: ",patso,0,75) win_refresh();
      win_numeric('f',"End offset: ",pateo,0,75) win_refresh();
      win_boolean('v',"Reverse pattern",patrev,1);
      win_numeric('R',"Repeats: ",rep,0,63);
      win_boolean('i',"Mirror repeats",repm,1);
      win_blank();
      win_picture(1) {
        set_patmax();
        memset(v_color+1,7,78);
        memset(v_char+1,'-',78);
        if(patso) v_char[patso]='(';
        if(pateo && pateo<patmax) v_char[patmax+1-pateo]=')';
        if(patso<patmax && patmax-patso-pateo>0) switch(pat) {
          case 0: case 1:
            memcpy(v_char+patso+1,(pat?pats:(Uint8*)"\x20\x20\x20\x20\xB0\xB0\xB1\xB1\xB2\xB2\xDB\xDB\xDB\xDB")+patso,patmax-patso-pateo);
            memset(v_color+patso+1,(lay==3?overclip.color:clip.color),patmax-patso-pateo);
            break;
          case 2:
            for(i=patso;i<patmax-pateo;i++) {
              v_char[i+1]="\x20\xB0\xB1\xB2\xDB\x20\xFA\xF9\x07\xFE"[grads[patn].c[i]>>8];
              v_color[i+1]=grads[patn].c[i];
            }
            break;
          case 3: /* ignored */ break;
        }
      }
      win_blank();
      win_numeric('m',"Randomization: ",patran,0,9999);
      win_boolean('h',"Chess",chess,1);
      win_blank();
      win_command_esc(0,"Done") break;
    }
    win_command('O',"Option...") win_form("Gradient option") {
      win_help("gradient","o");
      win_heading("Layer:");
      win_option('F',"Floor",lay,0);
      win_option('U',"Under",lay,1);
      win_option('M',"Main",lay,2);
      win_option('O',"Over",lay,3);
      win_blank();
      win_heading("Affect:");
      win_boolean('K',"Kind",aff,1);
      win_boolean('C',"Color",aff,2);
      win_boolean('P',"Parameter",aff,4);
      win_blank();
      win_boolean('s',"Clear stats",aff,0x10);
      win_boolean('A',"Avoid stats",aff,0x20);
      win_blank();
      win_boolean('R',"Retain marks",km,2);
      win_blank();
      win_command_esc(0,"Done") break;
    }
    win_blank();
    win_command('x',"Execute") break;
    win_command_esc(0,"Cancel") return;
  }
  t=(lay==3?overclip:clip);
  if(!divis) divis=1;
  set_patmax();
  if(patmax<=patso+pateo) {
    alert_text("Start offset and end offset are overlapping");
    goto restart;
  }
  switch(sh) {
    case 1: g=atan2(y2-y1,x2-x1); break;
    case 2: g=hypot(abs(x2-x1)+1,abs(y2-y1)+1); break;
    case 5: g=hypot(y2-y1,x2-x1); g*=g*0.5; break;
    case 8: g=atan2(y2-y1,x2-x1); break;
  }
  for(y=0;y<markheight;y++) for(x=0;x<markwidth;x++) {
    if(!set_mark(x,y,km)) continue;
    b=(lay==3?b_over:lay==2?b_main:b_under)+y*w+x;
    if(!lay) {
      if(b_under[y*w+x].kind) continue;
      if(elem_def[b_main[y*w+x].kind].attrib&A_FLOOR) continue;
      b=(b_main[y*w+x].kind?b_under:b_main)+y*w+x;
    }
    if(i=(lay?b->stat:(b_main[y*w+x].stat|b_under[y*w+x].stat))) {
      if(aff&0x10) {
        if(lay) {
          find_stat(x,y,i,lay,0);
          b->stat=0;
        } else {
          find_stat(x,y,b_main[y*w+x].stat,2,0);
          find_stat(x,y,b_under[y*w+x].stat,1,0);
          b_main[y*w+x].stat=b_under[y*w+x].stat=0;
        }
      }
      if(aff&0x20) continue;
    }
    if(sh==7) {
      k=(x-x1)*xi+(y-y1)*yi;
      if(patran) k+=dice(patran+1)>>3;
      if(patrev) k=-k;
      while(k<0) k+=patmax-patso-pateo;
      if(divis) k/=divis;
      k=k%(patmax-patso-pateo)+patso;
    } else if(sh) {
      switch(sh) {
        case 1:
          f=((x-x1)*cos(g)+(y-y1)*sin(g))/hypot(x2-x1,y2-y1);
          break;
        case 2:
          f=hypot(x-x1,y-y1)/g;
          break;
        case 3:
          f=fmax(xi*fabs(x-x1)/(fabs(x2-x1)+1.0),yi*fabs(y-y1)/(fabs(y2-y1)+1.0));
          break;
        case 4:
          f=0.5*fabs(x-x1)/(fabs(x2-x1)+1.0)+0.5*fabs(y-y1)/(fabs(y2-y1)+1.0);
          break;
        case 5:
          f=fabs((y2-y1)*1.0*x-(x2-x1)*1.0*y+x2*1.0*y1-y2*1.0*x1)/g;
          break;
        case 6:
          f=hypot(x-x1,y-y1)/hypot(x-x2,y-y2);
          if(isinf(f)) f=1.0; else f/=f+1.0;
          break;
        case 8:
          f=fmod(atan2(x-x1,y-y1)+M_PI,2*M_PI)/(2*M_PI);
          break;
      }
      k=patconv(f);
    } else {
      k=dice(patmax-patso-pateo)+patso;
    }
    switch(pat) {
      case 0: case 1:
        t.param=(pat?pats:(Uint8*)"\x20\x20\x20\x20\xB0\xB0\xB1\xB1\xB2\xB2\xDB\xDB\xDB\xDB")[k];
        break;
      case 2:
        t.param="\x20\xB0\xB1\xB2\xDB\x20\xFA\xF9\x07\xFE"[grads[patn].c[k]>>8];
        t.color=grads[patn].c[k];
        break;
      case 3:
        t=(lay==3?b_over:lay==1?b_under:b_main)[k%abs(xcur2+1-xcur)+(k/abs(ycur2+1-ycur))*w];
        break;
    }
    if(aff&1) b->kind=t.kind;
    if(aff&2) b->color=t.color;
    if(aff&4) b->param=t.param;
  }
}

Uint16 edit_board(Uint16 id) {
  int i;
  Sint32 k;
  if(status_on==255) {
    status_on=config.brd_edit_status;
    reseed(0);
  }
  set_apparent_clip();
  set_board_editor_screen();
  goto_board(id);
  scroll_x=scroll_y=0;
  v_status[1]='B';
  norm:
  for(emode=numprefix=0;;) {
    escroll();
    if(!vmode) {
      update_screen();
      if(markgrid) show_marks();
    } else {
      ed_update_screen();
    }
    if(emode=='v') show_selection();
    v_xcur=xcur-scroll_x; v_ycur=ycur-scroll_y;
    if(status_on) estatus();
    redisplay();
    do { if(!next_event()) goto exit; } while(event.type!=SDL_KEYDOWN);
    k=(!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym;
    switch(emode) {
      case 0: case 15: no_mode: switch(k) {
        case 0x08: numprefix/=10; break;
        case 0x09: if(emode=(emode?0:15)) place_at(xcur,ycur,clip); break;
        case 0x0D:
          if(!emode) {
            clip=b_main[xcur+ycur*board_info.width];
            if((i=memory[clip.kind+0x100])>=0x200) clip.param=b_main[xcur+ycur*board_info.width].param=parameter_edit(i,clip.param,clip.stat,find_stat(xcur,ycur,clip.stat,2,2));
            set_apparent_clip();
          }
          break;
        case 0x0F: stat_list(numprefix); numprefix=0; break;
        case 0x10:
          if((i=b_main[xcur+ycur*board_info.width].stat) && i<=maxstat) {
            stats[i-1].text=text_editor(stats[i-1].text);
            stats[i-1].length=(stats[i-1].text?strlen(stats[i-1].text):0);
          }
          break;
        case 0x16: vmode^=1; break;
        case 0x1A:
          if(!numprefix) numprefix=1;
          if(numprefix<=maxstat && stats[numprefix-1].count) {
            xcur=stats[numprefix-1].xy->x;
            ycur=stats[numprefix-1].xy->y;
          }
          numprefix=0;
          break;
        case 0x1B: if(numprefix) numprefix=0; else if(emode) emode=0; else goto exit; break;
        case '0' ... '9': if((i=numprefix*10+k-'0')<65536) numprefix=i; break;
        case -SDLK_i: edit_board_info(); break;
        case -SDLK_m: exchange_mark_grid(); break;
        case -SDLK_r: resize_board(); break;
        case -SDLK_t:
          if(boardnames) write_name_list("BRD.NAM",boardnames,maxboard);
          esave();
          run_test_game(brd_id);
          break;
        case -SDLK_u: free(markgrid); free(markgrid2); markwidth=markheight=markskip=markwidth2=markheight2=markskip2=0; markgrid=markgrid2=0; break;
        case -SDLK_z: numprefix=0xFFFF; break;
        case ' ': set_mark(xcur,ycur,1); break;
        case 'A': k=add_board(); if(k>0) goto_board(k); break;
        case 'c': case 0x03: clip.color=ask_color_char(0,clip.color); break;
        case 'd': case -SDLK_DELETE: delete_at(xcur,ycur); break;
        case 'e': edit_tile(1); break;
        case 'f': case 'F': emode=k; break;
        case 'g': clip.color=b_main[xcur+ycur*board_info.width].color; break;
        case 'h': case -SDLK_LEFT: cursor_move(-1,0); break;
        case 'i': do_colon_command("%toggle"); break;
        case 'j': case -SDLK_DOWN: cursor_move(0,1); break;
        case 'k': case -SDLK_UP: cursor_move(0,-1); break;
        case 'l': case -SDLK_RIGHT: cursor_move(1,0); break;
        case 'm': emode='m'; break;
        case 'n': find_next_marked(numprefix?:1); numprefix=0; break;
        case 'N': find_next_marked(-(numprefix?:1)); numprefix=0; break;
        case 'o': goto over; break;
        case 'p': place_at(xcur,ycur,clip); break;
        case 'q': write_at(xcur,ycur,clip); break;
        case 'Q': write_under(xcur,ycur,clip); break;
        case 'r': clip.color=(clip.color<<4)|(clip.color>>4); break;
        case 't': emode='t'; xcur2=xcur; break;
        case 'u': unmark: cc_unmark(0,0,0xFFFF,0xFFFF,""); emode=0; break;
        case 'v': emode='v'; xcur2=xcur; ycur2=ycur; break;
        case 'w': emode='w'; xcur2=ycur2=numprefix; numprefix=0; break;
        case 'y': clip=b_main[xcur+ycur*board_info.width]; set_apparent_clip(); break;
        case 'Y': clip=b_under[xcur+ycur*board_info.width]; set_apparent_clip(); break;
        case 'z': case 'Z': emode=k; break;
        case '<': case -SDLK_HOME: xcur=ycur=0; break;
        case '>': case -SDLK_END: xcur=board_info.width-1; ycur=board_info.height-1; break;
        case '[': switch_to_board(brd_id-(numprefix?:1)); numprefix=0; break;
        case ']': switch_to_board(brd_id+(numprefix?:1)); numprefix=0; break;
        case '{': switch_to_board(numprefix); numprefix=0; break;
        case '}': switch_to_board(maxboard); numprefix=0; break;
        case ':': ask_colon_command(); break;
        case -SDLK_SLASH: case -SDLK_QUESTION: online_help("editbrd",0); break;
        case -SDLK_F12 ... -SDLK_F1: f_menu((event.key.keysym.mod&KMOD_SHIFT?13:1)-k-SDLK_F1); break;
      } break;
      case 'f': case 'F': switch(k) {
        case 'c': flood(xcur,ycur,xcur,ycur,0x0003); break;
        case 'C': flood(xcur,ycur,xcur,ycur,0x0083); break;
        case 'k': flood(xcur,ycur,xcur,ycur,0x0001); break;
        case 'K': flood(xcur,ycur,xcur,ycur,0x0081); break;
        case 'p': flood(xcur,ycur,xcur,ycur,0x0007); break;
        case 'P': flood(xcur,ycur,xcur,ycur,0x0087); break;
        case ';': flood(xcur,ycur,xcur,ycur,0x0000); break;
        case -SDLK_SLASH: case -SDLK_QUESTION: online_help("editbrd","flood"); break;
      } emode=numprefix=0; break;
      case 'm': switch(k) {
        case 'c': do_colon_command("&color"); goto unmark;
        case 'C': do_colon_command("&color"); emode=0; break;
        case 'd': do_colon_command("&delete"); goto unmark;
        case 'D': do_colon_command("&delete"); emode=0; break;
        case 'f': do_colon_command("&floorplace"); goto unmark;
        case 'F': do_colon_command("&floorplace"); emode=0; break;
        case 'p': do_colon_command("&place"); goto unmark;
        case 'P': do_colon_command("&place"); emode=0; break;
        case 'h': case -SDLK_LEFT: mass_move(-(numprefix?:1),0,copy_cell); cursor_move(-1,0); break;
        case 'j': case -SDLK_DOWN: mass_move(0,numprefix?:1,copy_cell); cursor_move(0,1); break;
        case 'k': case -SDLK_UP: mass_move(0,-(numprefix?:1),copy_cell); cursor_move(0,-1); break;
        case 'l': case -SDLK_RIGHT: mass_move(numprefix?:1,0,copy_cell); cursor_move(1,0); break;
        default: goto no_mode;
      } break;
      case 't': switch(k) {
        case 0x08: if(xcur) --xcur; break;
        case 0x0A: case 0x0D: xcur=xcur2; if(ycur<board_info.height-1) ++ycur; break;
        case 0x10: if(k=ask_color_char(1,' ')) goto text; break;
        case 0x20 ... 0x7E: text: write_at(xcur,ycur,(Tile){.kind=clip.kind,.color=clip.color,.param=k,.stat=clip.stat}); if(xcur<board_info.width) ++xcur; break;
        default: goto no_mode;
      } break;
      case 'v': switch(k) {
        case 'c': do_colon_command("<:>unmark"); emode=0; break;
        case 'd': do_colon_command("<:>delete"); emode=0; break;
        case 'D': do_colon_command("<:>deletex"); emode=0; break;
        case 'f': block_tiling(1); emode=0; cc_unmark(0,0,markwidth,markheight,""); break;
        case 'F': block_tiling(1); emode=0; break;
        case 'g': gradient_menu(2); emode=0; break;
        case 'G': do_colon_command("<:>~&markonly"); gradient_menu(2); emode=0; break;
        case 'H': do_colon_command("<:>hflip"); emode=0; break;
        case 'i': case ' ': do_colon_command("<:>toggle"); emode=0; break;
        case 'm': do_colon_command("<:>mark"); emode='m'; break;
        case 'M': do_colon_command("<:>/b mark"); emode='m'; break;
        case 'p': do_colon_command("<:>~&place"); emode=0; break;
        case 's': case 0x0D: do_colon_command("<:>mark"); emode=0; break;
        case 'S': do_colon_command("<:>/b mark"); emode=0; break;
        case 't': block_tiling(0); emode=0; cc_unmark(0,0,markwidth,markheight,""); break;
        case 'T': block_tiling(0); emode=0; break;
        case 'V': do_colon_command("<:>vflip"); emode=0; break;
        case ';': i=xcur; xcur=xcur2; xcur2=i; i=ycur; ycur=ycur2; ycur2=i; break;
        default: goto no_mode;
      } break;
      case 'w': switch(k) {
        case 0x0D: emode=0; break;
        case -SDLK_e: board_info.exits[DIR_E]=0; break;
        case -SDLK_n: board_info.exits[DIR_N]=0; break;
        case -SDLK_s: board_info.exits[DIR_S]=0; break;
        case -SDLK_w: board_info.exits[DIR_W]=0; break;
        case 'A': k=add_board(); if(k>0) xcur2=k; k=0; break;
        case 'e': if(board_info.exits[DIR_E]) { xcur2=brd_id; goto_board(board_info.exits[DIR_E]); } break;
        case 'E': if(xcur2) board_info.exits[DIR_E]=xcur2; break;
        case 'm': xcur2=brd_id; break;
        case 'M': xcur2=numprefix; numprefix=0; break;
        case 'n': if(board_info.exits[DIR_N]) { xcur2=brd_id; goto_board(board_info.exits[DIR_N]); } break;
        case 'N': if(xcur2) board_info.exits[DIR_N]=xcur2; break;
        case 'q': ycur2=xcur2; xcur2=brd_id; switch_to_board(ycur2); break;
        case 's': if(board_info.exits[DIR_S]) { xcur2=brd_id; goto_board(board_info.exits[DIR_S]); } break;
        case 'S': if(xcur2) board_info.exits[DIR_S]=xcur2; break;
        case 'u': xcur2=0; break;
        case 'w': if(board_info.exits[DIR_W]) { xcur2=brd_id; goto_board(board_info.exits[DIR_W]); } break;
        case 'W': if(xcur2) board_info.exits[DIR_W]=xcur2; break;
        default: goto no_mode;
      } break;
      case 'z': case 'Z': switch(k) {
        case 'h': case -SDLK_LEFT: far_cursor_move(-1,0,emode=='Z',0); break;
        case 'j': case -SDLK_DOWN: far_cursor_move(0,1,emode=='Z',0); break;
        case 'k': case -SDLK_UP: far_cursor_move(0,-1,emode=='Z',0); break;
        case 'l': case -SDLK_RIGHT: far_cursor_move(1,0,emode=='Z',0); break;
      } emode=numprefix=0; break;
      default: emode=0;
    }
  }
  exit:
  v_status[1]=0;
  esave();
  return brd_id;
  over:
  for(emode=numprefix=0;;) {
    escroll();
    if(!vmode) {
      update_over_screen();
      if(markgrid) show_marks();
    } else {
      ed_update_over_screen();
    }
    if(emode=='v') show_selection();
    v_xcur=xcur-scroll_x; v_ycur=ycur-scroll_y;
    if(status_on) estatus_over();
    redisplay();
    do { if(!next_event()) goto exit; } while(event.type!=SDL_KEYDOWN);
    k=(!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym;
    switch(emode) {
      case 0: case '*': no_mode1: switch(k) {
        case 0x08: numprefix/=10; break;
        case 0x09: if(emode=(emode?0:'*')) over_place_at(xcur,ycur,overclip); break;
        case 0x0D:
          if(!emode) {
            overclip=b_over[xcur+ycur*board_info.width];
            b_over[xcur+ycur*board_info.width].param=overclip.param=ask_color_char(1,overclip.param);
          }
          break;
        case 0x0F: stat_list(numprefix); numprefix=0; break;
        case 0x10:
          if((i=b_over[xcur+ycur*board_info.width].stat) && i<=maxstat) {
            stats[i-1].text=text_editor(stats[i-1].text);
            stats[i-1].length=(stats[i-1].text?strlen(stats[i-1].text):0);
          }
          break;
        case 0x16: vmode^=1; break;
        case 0x1B: if(numprefix) numprefix=0; else if(emode) emode=0; else goto exit; break;
        case '0' ... '9': if((i=numprefix*10+k-'0')<65536) numprefix=i; break;
        case -SDLK_i: edit_board_info(); break;
        case -SDLK_m: exchange_mark_grid(); break;
        case -SDLK_r: resize_board(); break;
        case -SDLK_t:
          if(boardnames) write_name_list("BRD.NAM",boardnames,maxboard);
          esave();
          run_test_game(brd_id);
          break;
        case -SDLK_u: free(markgrid); free(markgrid2); markwidth=markheight=markskip=markwidth2=markheight2=markskip2=0; markgrid=markgrid2=0; break;
        case -SDLK_z: numprefix=0xFFFF; break;
        case ' ': set_mark(xcur,ycur,1); break;
        case 'c': case 0x03: overclip.color=ask_color_char(0,overclip.color); break;
        case 'd': case -SDLK_DELETE: over_delete_at(xcur,ycur); break;
        case 'e': edit_tile(2); break;
        case 'f': case 'F': emode=k; break;
        case 'g': overclip.color=b_over[xcur+ycur*board_info.width].color; break;
        case 'h': case -SDLK_LEFT: over_cursor_move(-1,0); break;
        case 'i': do_colon_command("%toggle"); break;
        case 'j': case -SDLK_DOWN: over_cursor_move(0,1); break;
        case 'k': case -SDLK_UP: over_cursor_move(0,-1); break;
        case 'l': case -SDLK_RIGHT: over_cursor_move(1,0); break;
        case 'm': emode='m'; break;
        case 'n': find_next_marked(numprefix?:1); numprefix=0; break;
        case 'N': find_next_marked(-(numprefix?:1)); numprefix=0; break;
        case 'o': goto norm; break;
        case 'p': over_place_at(xcur,ycur,overclip); break;
        case 'r': clip.color=(clip.color<<4)|(clip.color>>4); break;
        case 't': emode='t'; xcur2=xcur; break;
        case 'T': overclip.kind|=OVER_VISIBLE; emode='t'; xcur2=xcur; break;
        case 'u': unmark1: cc_unmark(0,0,0xFFFF,0xFFFF,""); emode=0; break;
        case 'v': emode='v'; xcur2=xcur; ycur2=ycur; break;
        case 'y': overclip=b_over[xcur+ycur*board_info.width]; break;
        case 'z': case 'Z': emode=k; break;
        case ';': overclip.param=ask_color_char(1,overclip.param); break;
        case '<': case -SDLK_HOME: xcur=ycur=0; break;
        case '>': case -SDLK_END: xcur=board_info.width-1; ycur=board_info.height-1; break;
        case '[': switch_to_board(brd_id-(numprefix?:1)); numprefix=0; break;
        case ']': switch_to_board(brd_id+(numprefix?:1)); numprefix=0; break;
        case '{': switch_to_board(numprefix); numprefix=0; break;
        case '}': switch_to_board(maxboard); numprefix=0; break;
        case ':': ask_colon_command(); break;
        case -SDLK_F1: overclip.kind^=0x01; break;
        case -SDLK_F2: overclip.kind^=0x02; break;
        case -SDLK_F3: overclip.kind^=0x04; break;
        case -SDLK_F4: overclip.kind^=0x08; break;
        case -SDLK_F5: overclip.kind^=OVER_SOLID; break;
        case -SDLK_F6: overclip.kind^=OVER_RESERVED; break;
        case -SDLK_F7: overclip.kind^=OVER_BG_THRU; break;
        case -SDLK_F8: overclip.kind^=OVER_VISIBLE; break;
        case -SDLK_F9: overclip.kind=0; break;
        case -SDLK_F10: overclip.kind=OVER_VISIBLE; break;
        case -SDLK_SLASH: case -SDLK_QUESTION: online_help("editbrd","over"); break;
      } break;
      case 'f': case 'F': switch(k) {
        case 'c': flood(xcur,ycur,xcur,ycur,0x0030); break;
        case 'k': flood(xcur,ycur,xcur,ycur,0x0010); break;
        case 'p': flood(xcur,ycur,xcur,ycur,0x0070); break;
        case ';': flood(xcur,ycur,xcur,ycur,0x0000); break;
        case -SDLK_SLASH: case -SDLK_QUESTION: online_help("editbrd","flood"); break;
      } emode=numprefix=0; break;
      case 'm': switch(k) {
        case 'c': do_colon_command("&overcolor"); goto unmark1;
        case 'C': do_colon_command("&overcolor"); emode=0; break;
        case 'd': do_colon_command("&overdelete"); goto unmark1;
        case 'D': do_colon_command("&overdelete"); emode=0; break;
        case 'p': do_colon_command("&overplace"); goto unmark1;
        case 'P': do_colon_command("&overplace"); emode=0; break;
        case 'h': case -SDLK_LEFT: mass_move(-(numprefix?:1),0,over_copy_cell); cursor_move(-1,0); break;
        case 'j': case -SDLK_DOWN: mass_move(0,numprefix?:1,over_copy_cell); cursor_move(0,1); break;
        case 'k': case -SDLK_UP: mass_move(0,-(numprefix?:1),over_copy_cell); cursor_move(0,-1); break;
        case 'l': case -SDLK_RIGHT: mass_move(numprefix?:1,0,over_copy_cell); cursor_move(1,0); break;
        default: goto no_mode1;
      } break;
      case 't': switch(k) {
        case 0x08: if(xcur) --xcur; break;
        case 0x0A: case 0x0D: xcur=xcur2; if(ycur<board_info.height-1) ++ycur; break;
        case 0x10: if(k=ask_color_char(1,' ')) goto text1; break;
        case 0x20 ... 0x7E: text1: over_place_at(xcur,ycur,(Tile){.kind=overclip.kind,.color=overclip.color,.param=k,.stat=0}); if(xcur<board_info.width) ++xcur; break;
        default: goto no_mode1;
      } break;
      case 'v': switch(k) {
        case 'c': do_colon_command("<:>unmark"); emode=0; break;
        case 'd': do_colon_command("<:>overdelete"); emode=0; break;
        case 'g': gradient_menu(3); emode=0; break;
        case 'G': do_colon_command("<:>~&markonly"); gradient_menu(3); emode=0; break;
        case 'H': do_colon_command("<:>ohflip"); emode=0; break;
        case 'i': case ' ': do_colon_command("<:>toggle"); emode=0; break;
        case 'm': do_colon_command("<:>mark"); emode='m'; break;
        case 'M': do_colon_command("<:>/b mark"); emode='m'; break;
        case 'p': do_colon_command("<:>~&overplace"); emode=0; break;
        case 's': case 0x0D: do_colon_command("<:>mark"); emode=0; break;
        case 'S': do_colon_command("<:>/b mark"); emode=0; break;
        case 't': block_tiling(2); emode=0; cc_unmark(0,0,markwidth,markheight,""); break;
        case 'T': block_tiling(2); emode=0; break;
        case 'V': do_colon_command("<:>ovflip"); emode=0; break;
        case ';': i=xcur; xcur=xcur2; xcur2=i; i=ycur; ycur=ycur2; ycur2=i; break;
        default: goto no_mode1;
      } break;
      case 'z': case 'Z': switch(k) {
        case 'h': case -SDLK_LEFT: far_cursor_move(-1,0,emode=='Z',1); break;
        case 'j': case -SDLK_DOWN: far_cursor_move(0,1,emode=='Z',1); break;
        case 'k': case -SDLK_UP: far_cursor_move(0,-1,emode=='Z',1); break;
        case 'l': case -SDLK_RIGHT: far_cursor_move(1,0,emode=='Z',1); break;
      } emode=numprefix=0; break;
      default: emode=0;
    }
  }
  goto exit;
}

