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
    //win_boolean('F',"Fill",b,1);
    win_blank();
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
    if(b&1) {
      //TODO: fill
    }
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
  draw_text(69,y,buf,0x19,snprintf(buf,80,"(%4d,%4d)",xcur,ycur));
}

static void cursor_move(Sint32 xd,Sint32 yd) {
  if(xcur+xd>=0 && xcur+xd<board_info.width) xcur+=xd;
  if(ycur+yd>=0 && ycur+yd<board_info.height) ycur+=yd;
}

void edit_board(Uint16 id) {
  int i;
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
    switch((!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym) {
      case 0x1B: goto exit;
      case -SDLK_i: edit_board_info(); break;
      case -SDLK_r: resize_board(); break;
      case -SDLK_t:
        if(boardnames) write_name_list("BRD.NAM",boardnames,maxboard);
        run_test_game(brd_id);
        break;
      case SDLK_e: edit_tile(); break;
      case SDLK_h: case -SDLK_LEFT: cursor_move(-1,0); break;
      case SDLK_j: case -SDLK_DOWN: cursor_move(0,1); break;
      case SDLK_k: case -SDLK_UP: cursor_move(0,-1); break;
      case SDLK_l: case -SDLK_RIGHT: cursor_move(1,0); break;
      case -SDLK_HOME: xcur=ycur=0; break;
      case -SDLK_END: xcur=board_info.width-1; ycur=board_info.height-1; break;
      case -SDLK_INSERT: clip=(event.key.keysym.mod&KMOD_SHIFT?b_under:b_main)[xcur+ycur*board_info.width]; set_apparent_clip(); break;
    }
  }
  exit:
  v_status[1]=0;
  esave();
}

