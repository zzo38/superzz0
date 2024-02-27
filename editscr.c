#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 editscr.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

static Uint16 scr_id;
static Uint8 xcur,ycur,xcur2,ycur2;
static Uint16 numprefix;
static Uint8 emode;
static Uint8 markgrid[250];

static void goto_screen(Uint16 id) {
  FILE*fp=open_lump_by_number(id,"SCR","r");
  if(fp) {
    load_screen(fp);
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

Uint16 edit_screen(Uint16 id) {
  int i;
  Sint32 k;
  goto_screen(id);
  v_status[1]='S';
  emode=0;
  for(;;) {
    v_xcur=xcur; v_ycur=ycur;
    k=(!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym;
    switch(emode) {
      case 0: no_mode: switch(k) {
        case 0x08: numprefix/=10; break;
        case 0x1B: if(numprefix) numprefix=0; else if(emode) emode=0; else goto exit; break;
        case '0' ... '9': if((i=numprefix*10+k-'0')<65536) numprefix=i; break;
        case 'u': memset(markgrid,0,250); break;
      }
    }
  }
  exit:
  v_status[1]=0;
  esave();
  return scr_id;
}

