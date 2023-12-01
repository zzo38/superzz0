#if 0
gcc -s -O2 -c -Wno-unused-result -fwrapv window.c `sdl-config --cflags`
exit
#endif

// This file is public domain.
// See bottom for documentation.

#include "common.h"

static win_memo*cwin;

static inline int selected_key(Uint8 k) {
  Uint16 u=event.key.keysym.unicode;
  return (u==k || (k>='A' && k<='Z' && u==k+'a'-'A'));
}

win_memo win_begin_(void) {
  win_memo w={1,1,255,0};
  cwin=0;
  return w;
}

void win_step_(win_memo*wm,const char*title) {
  event.type=SDL_KEYUP;
  if(cwin!=wm) {
    cwin=wm;
    memset(v_char,0x00,80*25);
    memset(v_color,0x30,80);
    memset(v_color+80,0x00,80*24);
    strcpy(v_char,title);
    wm->state=1;
  } else {
    if(wm->ncur && wm->ncur<wm->line) wm->cur=wm->ncur;
    wm->state=0;
    redisplay();
    if(!wm->ncur) next_event();
    wm->ncur=0;
  }
  wm->line=1;
}

static inline int draw_label(int x,Uint8 key,const char*label) {
  const char*p;
  int z=cwin->line*80;
  v_char[z]=(cwin->line==cwin->cur?0x10:0xFA);
  v_color[z]=(cwin->line==cwin->cur?0x0B:0x08);
  if(cwin->state) {
    z=draw_text(x,cwin->line,label,0x07,-1);
    if(key && (p=strchr(label,key))) v_color[cwin->line*80+x+(p-label)]=0x0A;
    return z;
  } else {
    return x+strlen(label);
  }
}

int win_numeric_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 lo,Uint32 hi) {
  int r=0;
  char buf[12];
  int x;
  Uint32 n;
  if(cwin!=wm) return 0;
  if(!wm->cur) wm->cur=wm->line;
  x=draw_label(1,key,label);
  if(s==sizeof(Uint8)) n=*(Uint8*)v;
  if(s==sizeof(Uint16)) n=*(Uint16*)v;
  if(s==sizeof(Uint32)) n=*(Uint32*)v;
  if(event.type==SDL_KEYDOWN) {
    if(event.key.keysym.mod&(KMOD_ALT|KMOD_META)) {
      if(wm->cur==wm->line) {
        if(n<lo) r=1,n=lo; else if(n>hi) r=1,n=hi;
      }
      if(selected_key(key)) wm->ncur=wm->line;
    } else if(wm->cur==wm->line) {
      if(event.key.keysym.sym==SDLK_DOWN || (event.key.keysym.sym==SDLK_TAB && !(event.key.keysym.mod&KMOD_SHIFT))) {
        if(n<lo) r=1,n=lo; else if(n>hi) r=1,n=hi;
        wm->ncur=wm->line+1;
      } else if(event.key.keysym.sym==SDLK_LEFT) {
        if(n>lo) r=1,--n;
      } else if(event.key.keysym.sym==SDLK_RIGHT) {
        if(n<hi) r=1,++n;
      } else if(key=event.key.keysym.unicode) {
        if(key>='0' && key<='9') r=1,n=10*n+key-'0'; if(key==0x08) r=1,n/=10; else if(key==0x15) r=1,n=0;
        if(n>hi) n=hi;
      }
    } else if(event.key.keysym.sym==SDLK_UP || (event.key.keysym.sym==SDLK_TAB && (event.key.keysym.mod&KMOD_SHIFT))) {
      if(wm->cur>wm->line) wm->ncur=wm->line;
      if(n<lo) r=1,n=lo; else if(n>hi) r=1,n=hi;
    }
    if(s==sizeof(Uint8)) *(Uint8*)v=n;
    if(s==sizeof(Uint16)) *(Uint16*)v=n;
    if(s==sizeof(Uint32)) *(Uint32*)v=n;
  }
  hi=snprintf(buf,0,"%lu",(long)hi);
  hi=snprintf(buf,12,"%*lu",(int)hi,(long)n);
  draw_text(x,wm->line,buf,0x1F,hi);
  wm->line++;
  return r;
}

int win_boolean_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 b) {
  int r=0;
  Uint32 n;
  if(cwin!=wm) return 0;
  if(!wm->cur) wm->cur=wm->line;
  if(s==sizeof(Uint8)) n=*(Uint8*)v;
  if(s==sizeof(Uint16)) n=*(Uint16*)v;
  if(s==sizeof(Uint32)) n=*(Uint32*)v;
  draw_text(1,wm->line,"[?]",0x1F,3);
  draw_label(5,key,label);
  if(event.type==SDL_KEYDOWN) {
    if(event.key.keysym.mod&(KMOD_ALT|KMOD_META)) {
      if(selected_key(key)) wm->ncur=wm->line,r=1;
    } else if(wm->cur==wm->line) {
      if(event.key.keysym.sym==SDLK_DOWN || (event.key.keysym.sym==SDLK_TAB && !(event.key.keysym.mod&KMOD_SHIFT))) {
        wm->ncur=wm->line+1;
      } else if(event.key.keysym.sym==SDLK_LEFT) {
        if(n&b) r=1;
      } else if(event.key.keysym.sym==SDLK_RIGHT) {
        if(!(n&b)) r=1;
      } else if(event.key.keysym.sym==SDLK_SPACE || event.key.keysym.sym==SDLK_RETURN) {
        r=1;
      }
    } else if(event.key.keysym.sym==SDLK_UP || (event.key.keysym.sym==SDLK_TAB && (event.key.keysym.mod&KMOD_SHIFT))) {
      if(wm->cur>wm->line) wm->ncur=wm->line;
    }
  }
  if(r) {
    n^=b;
    if(s==sizeof(Uint8)) *(Uint8*)v=n;
    if(s==sizeof(Uint16)) *(Uint16*)v=n;
    if(s==sizeof(Uint32)) *(Uint32*)v=n;
  }
  v_char[wm->line*80+2]=(n&b?'X':' ');
  wm->line++;
  return r;
}

int win_command_(win_memo*wm,Uint8 key,const char*label) {
  int x;
  Uint32 n;
  int r=0;
  if(cwin!=wm) return 0;
  if(!wm->cur) wm->cur=wm->line;
  v_color[wm->line*80+1]=0x03;
  v_char[wm->line*80+1]='<';
  x=draw_label(2,key,label);
  v_color[wm->line*80+x]=0x03;
  v_char[wm->line*80+x]='>';
  if(event.type==SDL_KEYDOWN) {
    if(event.key.keysym.mod&(KMOD_ALT|KMOD_META)) {
      if(selected_key(key)) wm->ncur=wm->line,r=1;
    } else if(wm->cur==wm->line) {
      if(event.key.keysym.sym==SDLK_DOWN || (event.key.keysym.sym==SDLK_TAB && !(event.key.keysym.mod&KMOD_SHIFT))) {
        wm->ncur=wm->line+1;
      } else if(event.key.keysym.sym==SDLK_SPACE || event.key.keysym.sym==SDLK_RETURN) {
        r=1;
      }
    } else if(event.key.keysym.sym==SDLK_UP || (event.key.keysym.sym==SDLK_TAB && (event.key.keysym.mod&KMOD_SHIFT))) {
      if(wm->cur>wm->line) wm->ncur=wm->line;
    }
  }
  wm->line++;
  return r;
}

void win_heading_(win_memo*wm,const char*label) {
  if(cwin!=wm) return;
  if(label) draw_text(0,wm->line,label,0x0E,-1);
  if(wm->cur==wm->line) wm->cur++;
  if(wm->ncur==wm->line) wm->ncur++;
  wm->line++;
}

