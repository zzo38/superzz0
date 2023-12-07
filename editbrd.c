#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 editbrd.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

static Uint16 brd_id;
static Uint16 xcur,ycur;

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

void edit_board(Uint16 id) {
  goto_board(id);
  
}
