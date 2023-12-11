#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 editbrd.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

static Uint16 brd_id;
static Uint16 xcur,ycur;

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

void edit_board(Uint16 id) {
  goto_board(id);
  
}
