#if 0
gcc -s -O2 -c -Wno-unused-result -fwrapv window.c `sdl-config --cflags`
exit
#endif

// This file is public domain.
// See bottom for documentation.

#include "common.h"

static win_memo*cwin;

void draw_border(Uint8 c,Uint8 x0,Uint8 y0,Uint8 x1,Uint8 y1) {
  Uint8 x,y;
  for(y=y0;y<=y1;y++) {
    memset(v_color+y*80+x0,c,x1+1-x0);
    v_char[y*80+x0]=(y==y0?0xDA:y==y1?0xC0:0xB3);
    memset(v_char+y*80+x0+1,y==y0?0xC4:y==y1?0xC4:0x00,x1-1-x0);
    v_char[y*80+x1]=(y==y0?0xBF:y==y1?0xD9:0xB3);
  }
}

void ask_text(const char*prompt,Uint8*buf,int len) {
  int i,n;
  cwin=0;
  draw_border(0x2A,1,20,78,23);
  draw_text(3,21,prompt,0x2E,-1);
  for(;;) {
    for(i=0;i<len && buf[i];i++) v_color[i+22*80+3]=0x1F,v_char[i+22*80+3]=buf[i];
    n=i;
    if(i<len) v_color[i+22*80+3]=0x13,v_char[i+22*80+3]=177;
    for(i++;i<len;i++) v_color[i+22*80+3]=0x10,v_char[i+22*80+3]=0xFA;
    redisplay();
    if(!next_event()) break;
    if(event.type!=SDL_KEYDOWN) continue;
    if(event.key.keysym.sym==SDLK_RETURN) break;
    i=event.key.keysym.unicode;
    if(i==0x0D || i==0x0A) break;
    if(n<len && i>=0x20 && i<0x7F) buf[n++]=i,buf[n]=0;
    if(n && i==0x08) buf[n-1]=0;
    if(i==0x15) *buf=0;
  }
}

Uint8 ask_color_char(Uint8 m,Uint8 v) {
  int x;
  cwin=0;
  memset(v_color+2*80+29,0x1B,20);
  v_char[2*80+29]=0xDA;
  memset(v_char+2*80+30,0xC4,18);
  v_char[2*80+48]=0xBF;
  for(x=3;x<21;x++) {
    v_char[x*80+29]=v_char[x*80+48]=0xB3;
    v_color[x*80+29]=v_color[x*80+48]=0x1B;
  }
  memset(v_color+21*80+29,0x1B,20);
  v_char[21*80+29]=0xC0;
  memset(v_char+21*80+30,0xC4,18);
  v_char[21*80+48]=0xD9;
  for(;;) {
    memset(v_color+3*80+30,0x11,18);
    for(x=4;x<20;x++) v_color[x*80+30]=v_color[x*80+47]=0x11;
    if(m) {
      for(x=0;x<256;x++) v_char[(x>>4)*80+(x&15)+351]=x,v_color[(x>>4)*80+(x&15)+351]=0x07;
    } else {
      for(x=0;x<256;x++) v_char[(x>>4)*80+(x&15)+351]=0x07,v_color[(x>>4)*80+(x&15)+351]=x;
    }
    memset(v_color+20*80+30,0x11,18);
    x=(v>>4)*80+(v&15)+351;
    if(m) v_color[x]=0x0F;
    v_color[x-81]=v_color[x-80]=v_color[x-79]=v_color[x-1]=v_color[x+1]=v_color[x+79]=v_color[x+80]=v_color[x+81]=0x08;
    v_char[x-81]=0xDA;
    v_char[x-79]=0xBF;
    v_char[x+79]=0xC0;
    v_char[x+81]=0xD9;
    v_char[x-80]=v_char[x+80]=0xC4;
    v_char[x-1]=v_char[x+1]=0xB3;
    redisplay();
    if(!next_event()) break;
    if(event.type!=SDL_KEYDOWN) continue;
    switch(x=event.key.keysym.sym) {
      case SDLK_RETURN: case SDLK_SPACE: return v;
      case SDLK_LEFT: case SDLK_h: --v; break;
      case SDLK_RIGHT: case SDLK_l: ++v; break;
      case SDLK_UP: case SDLK_k: v-=16; break;
      case SDLK_DOWN: case SDLK_j: v+=16; break;
      case SDLK_0 ... SDLK_9: v<<=4; v+=x-SDLK_0; break;
      case SDLK_a ... SDLK_f: v<<=4; v+=x+10-SDLK_a; break;
    }
  }
  return v;
}

static inline int selected_key(Uint8 k) {
  Uint16 u=event.key.keysym.unicode;
  return k && (u==k || (k>='A' && k<='Z' && u==k+'a'-'A'));
}

static inline int visible(void) {
  return (cwin->line>=cwin->scroll+1 && cwin->line<cwin->scroll+25);
}

win_memo win_begin_(void) {
  win_memo w={1,0,0,0,1,1};
  cwin=0;
  return w;
}

#define Y (cwin->line-cwin->scroll)

void win_step_(win_memo*wm,const char*title) {
  event.type=SDL_KEYUP;
  restart:
  if(wm->cur && wm->scroll>wm->cur-1) {
    wm->scroll=wm->cur-1;
    goto scrolled;
  } else if(wm->scroll<wm->cur-24) {
    wm->scroll=wm->cur-24;
    goto scrolled;
  }
  if(cwin!=wm) {
    cwin=wm;
    scrolled:
    memset(v_char,0x00,80*25);
    memset(v_color,0x30,80);
    memset(v_color+80,0x00,80*24);
    strcpy(v_char,title);
    if(wm->scroll) v_char[159]=0x1E,v_color[159]=0x0D;
    wm->state=1;
  } else {
    if(wm->ncur && wm->ncur<wm->line) {
      wm->cur=wm->ncur;
      if(wm->scroll>wm->cur-1) {
        wm->scroll=wm->cur-1;
        wm->ncur=0;
        goto scrolled;
      } else if(wm->scroll<wm->cur-24) {
        wm->scroll=wm->cur-24;
        wm->ncur=0;
        goto scrolled;
      }
    }
    wm->state=0;
    if(wm->line>wm->scroll+25) v_char[1999]=0x1F,v_color[1999]=0x0D;
    redisplay();
    if(!wm->ncur) {
      next_event();
      if(event.type==SDL_KEYDOWN) {
        if(event.key.keysym.sym==SDLK_PAGEUP) {
          event.key.keysym.sym=SDLK_UP;
          wm->cur=(wm->cur>23?wm->cur-23:0);
          wm->scroll=(wm->scroll>24?wm->scroll-24:0);
          goto scrolled;
        } else if(event.key.keysym.sym==SDLK_PAGEDOWN) {
          wm->cur+=24;
          if(wm->cur>=wm->line) wm->cur=wm->line,event.key.keysym.sym=SDLK_UP;
          wm->scroll+=24;
          if(wm->line>25 && wm->scroll>wm->line-25) wm->scroll=wm->line-25;
          if(wm->scroll<wm->cur-24) wm->scroll=wm->cur-24;
          goto scrolled;
        } else if(event.key.keysym.sym==SDLK_F11) {
          goto scrolled;
        }
      }
    }
    wm->ncur=0;
  }
  wm->line=1;
  return;
}

static inline int draw_label(int x,Uint8 key,const char*label) {
  const char*p;
  int z=Y*80;
  if(Y<1 || Y>24) return 0;
  v_char[z]=(cwin->line==cwin->cur?0x10:0xFA);
  v_color[z]=(cwin->line==cwin->cur?0x0B:0x08);
  if(cwin->state) {
    z=draw_text(x,Y,label,0x07,-1);
    if(key && (p=strchr(label,key))) v_color[Y*80+x+(p-label)]=0x0A;
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
  if(visible()) {
    hi=snprintf(buf,0,"%lu",(long)hi);
    hi=snprintf(buf,12,"%*lu",(int)hi,(long)n);
    draw_text(x,Y,buf,0x1F,hi);
  }
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
  if(visible()) draw_text(1,Y,"[?]",0x1F,3);
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
  if(visible()) v_char[Y*80+2]=(n&b?'X':' ');
  wm->line++;
  return r;
}

int win_command_(win_memo*wm,Uint8 key,const char*label,Uint8 esc) {
  int x;
  Uint32 n;
  int r=0;
  if(cwin!=wm) return 0;
  if(!wm->cur) wm->cur=wm->line;
  if(visible()) {
    v_color[Y*80+1]=(esc?6:3);
    v_char[Y*80+1]='<';
    x=draw_label(2,key,label);
    v_color[Y*80+x]=(esc?6:3);
    v_char[Y*80+x]='>';
  }
  if(event.type==SDL_KEYDOWN) {
    if(event.key.keysym.mod&(KMOD_ALT|KMOD_META)) {
      if(selected_key(key)) wm->ncur=wm->line,r=1;
    } else if(esc && event.key.keysym.sym==SDLK_ESCAPE) {
      r=1;
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
  if(wm->state && label && visible()) draw_text(0,Y,label,0x0E,-1);
  if(wm->cur==wm->line) wm->cur++;
  if(wm->ncur==wm->line) wm->ncur++;
  wm->line++;
}

int win_picture_(win_memo*wm,int h) {
  int i;
  if(cwin!=wm) return 0;
  if(wm->state==1 && (wm->line+h-wm->scroll>0 || wm->line-wm->scroll<25)) {
    memcpy(sv_char,v_char,80*25);
    memcpy(sv_color,v_color,80*25);
    memset(v_color,0x00,80*h);
    wm->state=2;
  } else if(wm->state==2) {
    for(i=0;i<h;i++) if(Y+i>0 && Y+i<25) {
      memcpy(sv_char+(Y+i)*80,v_char+i*80,80);
      memcpy(sv_color+(Y+i)*80,v_color+i*80,80);
    }
    memcpy(v_char,sv_char,80*25);
    memcpy(v_color,sv_color,80*25);
    wm->state=1;
  }
  if(wm->cur>=wm->line && wm->cur<wm->line+h) wm->ncur=wm->line+h;
  if(wm->ncur>=wm->line && wm->ncur<wm->line+h) wm->ncur=wm->line+h;
  if(wm->state==2) return 1; else wm->line+=h;
  return 0;
}

int win_option_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 b) {
  int r=0;
  Uint32 n;
  if(cwin!=wm) return 0;
  if(!wm->cur) wm->cur=wm->line;
  if(s==sizeof(Uint8)) n=*(Uint8*)v;
  if(s==sizeof(Uint16)) n=*(Uint16*)v;
  if(s==sizeof(Uint32)) n=*(Uint32*)v;
  if(visible()) draw_text(1,Y,"(?)",0x1F,3);
  draw_label(5,key,label);
  if(event.type==SDL_KEYDOWN) {
    if(event.key.keysym.mod&(KMOD_ALT|KMOD_META)) {
      if(selected_key(key)) wm->ncur=wm->line,r=1;
    } else if(wm->cur==wm->line) {
      if(event.key.keysym.sym==SDLK_DOWN || (event.key.keysym.sym==SDLK_TAB && !(event.key.keysym.mod&KMOD_SHIFT))) {
        wm->ncur=wm->line+1;
      } else if(event.key.keysym.sym==SDLK_SPACE || event.key.keysym.sym==SDLK_RETURN) {
        r=1;
        wm->ncur=wm->cur;
      }
    } else if(event.key.keysym.sym==SDLK_UP) {
      if(wm->cur>wm->line) wm->ncur=wm->line;
    } else if(event.key.keysym.sym==SDLK_TAB) {
      if(event.key.keysym.mod&KMOD_SHIFT) {
        if(n==b && wm->cur>wm->line) wm->ncur=wm->line;
      } else {
        if(wm->ncur==wm->line && n!=b) wm->ncur++;
      }
    }
  }
  if(r) {
    n=b;
    if(s==sizeof(Uint8)) *(Uint8*)v=n;
    if(s==sizeof(Uint16)) *(Uint16*)v=n;
    if(s==sizeof(Uint32)) *(Uint32*)v=n;
  }
  if(visible()) v_char[Y*80+2]=(n==b?7:255);
  wm->line++;
  return r;
}

int win_text_(win_memo*wm,Uint8 key,const char*label,Uint8*v,size_t s,Uint8 q) {
  int r=0;
  int i,x;
  if(cwin!=wm) return 0;
  if(!wm->cur) wm->cur=wm->line;
  x=draw_label(1,key,label);
  if(event.type==SDL_KEYDOWN) {
    if(event.key.keysym.mod&(KMOD_ALT|KMOD_META)) {
      if(selected_key(key)) wm->ncur=wm->line;
    } else if(wm->cur==wm->line) {
      if(event.key.keysym.sym==SDLK_DOWN || (event.key.keysym.sym==SDLK_TAB && !(event.key.keysym.mod&KMOD_SHIFT))) {
        wm->ncur=wm->line+1;
      } else if(key=event.key.keysym.unicode) {
        if(key==0x08 && *v) {
          r=1;
          v[strlen(v)-1]=0;
        } else if(key>=0x20 && key<0x80) {
          if(q) {
            if(key>='0' && key<='9' && !*v) key=0;
            if(key>='a' && key<='z') key+='A'-'a';
            if((key<'A' || key>'Z') && (key<'0' || key>'9') && key!='_') key=0;
          }
          r=1;
          i=strlen(v);
          if(i<s-1) v[i]=key,v[i+1]=0;
        } else if(key==0x15 && *v) {
          r=1;
          *v=0;
        } else if(key==0x10 && *v && !q) {
          r=1;
          i=strlen(v);
          if(i<s-1) v[i]=ask_color_char(1,128),v[i+1]=0;
        }
      }
    } else if(event.key.keysym.sym==SDLK_UP || (event.key.keysym.sym==SDLK_TAB && (event.key.keysym.mod&KMOD_SHIFT))) {
      if(wm->cur>wm->line) wm->ncur=wm->line;
    }
  }
  if(visible()) {
    for(i=0;i<s-1 && v[i];i++) v_char[Y*80+x+i]=v[i],v_color[Y*80+x+i]=0x1F;
    if(wm->cur==wm->line && i<s-1) v_char[Y*80+x+i]=177,v_color[Y*80+x+i]=0x13,i++;
    for(;i<s-1;i++) v_char[Y*80+x+i]=0xFA,v_color[Y*80+x+i]=0x10;
  }
  wm->line++;
  return r;
}

int win_color_char_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,int m) {
  int r=0;
  char buf[5];
  int x;
  Uint8 n;
  if(cwin!=wm) return 0;
  if(!wm->cur) wm->cur=wm->line;
  x=draw_label(1,key,label);
  if(s==sizeof(Uint8)) n=*(Uint8*)v;
  if(s==sizeof(Uint16)) n=*(Uint16*)v;
  if(s==sizeof(Uint32)) n=*(Uint32*)v;
  if(event.type==SDL_KEYDOWN) {
    if(event.key.keysym.mod&(KMOD_ALT|KMOD_META)) {
      if(selected_key(key)) wm->ncur=wm->line;
    } else if(wm->cur==wm->line) {
      if(event.key.keysym.sym==SDLK_DOWN || (event.key.keysym.sym==SDLK_TAB && !(event.key.keysym.mod&KMOD_SHIFT))) {
        wm->ncur=wm->line+1;
      } else if(event.key.keysym.sym==SDLK_LEFT) {
        r=1; --n;
      } else if(event.key.keysym.sym==SDLK_RIGHT) {
        r=1; ++n;
      } else if(event.key.keysym.sym==SDLK_SPACE || event.key.keysym.sym==SDLK_RETURN) {
        r=1; n=ask_color_char(m,n);
      } else if(key=event.key.keysym.unicode) {
        if(m && key>=0x20 && key<0x7F) r=1,n=key;
      }
    } else if(event.key.keysym.sym==SDLK_UP || (event.key.keysym.sym==SDLK_TAB && (event.key.keysym.mod&KMOD_SHIFT))) {
      if(wm->cur>wm->line) wm->ncur=wm->line;
    }
    if(s==sizeof(Uint8)) *(Uint8*)v=n;
    if(s==sizeof(Uint16)) *(Uint16*)v=n;
    if(s==sizeof(Uint32)) *(Uint32*)v=n;
  }
  if(cwin && visible()) {
    if(m) {
      memset(v_color+Y*80+x+1,0x1F,3);
      v_char[Y*80+x+2]=n;
    } else {
      memset(v_color+Y*80+x+1,n,3);
      v_char[Y*80+x+2]=0xFE;
    }
    snprintf(buf,5,"(%02X)",(int)n);
    draw_text(x+6,Y,buf,0x07,4);
  }
  wm->line++;
  return r;
}

int win_list_(win_memo*wm,int n,void*u,void(*f)(Uint16,int,void*)) {
  int r=-1;
  int i,j;
  if(cwin!=wm) return -1;
  if(!wm->cur) wm->cur=wm->line;
  for(i=(wm->line-wm->scroll<1?1:wm->line-wm->scroll);i<(wm->line+n-wm->scroll) && i<25;i++) {
    v_char[i*80]=(i+wm->scroll==wm->cur?0x10:0xFA);
    v_color[i*80]=(i+wm->scroll==wm->cur?0x0B:0x08);
  }
  if(wm->state) {
    for(i=0;i<n;i++) {
      if(visible()) f(i,Y,u);
      wm->line++;
    }
  } else {
    if(event.type==SDL_KEYDOWN) {
      if(event.key.keysym.sym==SDLK_UP) {
        if(wm->cur>wm->line) wm->ncur=(wm->cur<wm->line+n?wm->cur-1:wm->line+n-1);
      } else if(event.key.keysym.sym==SDLK_TAB && (event.key.keysym.mod&KMOD_SHIFT)) {
        if(wm->cur>=wm->line+n) wm->ncur=wm->line;
      } else if(wm->cur>=wm->line && wm->cur<wm->line+n) {
        if(event.key.keysym.sym==SDLK_DOWN) {
          wm->ncur=wm->cur+1;
        } else if(event.key.keysym.sym==SDLK_SPACE || event.key.keysym.sym==SDLK_RETURN) {
          r=wm->cur-wm->line;
        } else if(event.key.keysym.sym==SDLK_TAB) {
          wm->ncur=wm->line+n;
        } else if(event.key.keysym.sym==SDLK_HOME) {
          wm->ncur=wm->line;
        } else if(event.key.keysym.sym==SDLK_END) {
          wm->ncur=wm->line+n-1;
        } else if(i=event.key.keysym.unicode) {
          if(i>='0' && i<='9') {
            j=wm->cur-wm->line;
            if(10*j+i-'0'<n) wm->ncur=wm->line+(10*j+i-'0');
          } else if(i==8) {
            j=wm->cur-wm->line;
            wm->ncur=wm->line+j/10;
          }
        }
      }
    }
    wm->line+=n;
  }
  return r;
}

void win_cursor_(win_memo*wm,int offset) {
  if(wm==cwin && wm->ready) {
    wm->ready=0;
    wm->ncur=wm->line+offset;
  }
}

