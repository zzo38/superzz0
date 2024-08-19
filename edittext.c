#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 edittext.c `sdl-config --cflags`
exit
#endif

#include "common.h"

typedef struct {
  Uint8 len,mem,own;
  Uint8*ptr;
} Line;

static Line*lines;
static Uint16 nlines,nchars;

static int ins_char(Uint16 ln,Uint8 xc,Uint8 ch) {
  Uint8*p;
  Line*li=lines+ln;
  if(!ch || ch=='\n' || li->len>=253) return 0;
  if(xc<li->len && !config.text_editor_insert) {
    li->ptr[xc]=ch;
    return 1;
  }
  if(nchars>=65530) return 0;
  ++nchars;
  if(li->len<=li->mem) {
    if(li->own) {
      li->ptr=realloc(li->ptr,li->mem=(li->len+32>255?255:li->len+32));
      if(!li->ptr) err(1,"Allocation failed");
    } else {
      li->own=1;
      p=li->ptr;
      li->ptr=malloc(li->mem=(li->len+32>255?255:li->len+32));
      if(!li->ptr) err(1,"Allocation failed");
      memcpy(li->ptr,p,xc);
      memcpy(li->ptr+xc+1,p+xc,li->len-xc);
      li->ptr[xc]=ch;
      li->len++;
      return 1;
    }
  }
  memmove(li->ptr+xc+1,li->ptr+xc,li->len-xc);
  li->len++;
  li->ptr[xc]=ch;
  return 1;
}

static void line_break(Uint8 xc,Uint16 yc) {
  Line*li;
  if(nchars>=65530) return;
  li=realloc(lines,(nlines+1)*sizeof(Line));
  if(!li) return;
  lines=li;
  memmove(li+yc+2,li+yc+1,(nlines-yc-1)*sizeof(Line));
  if(li[yc].own && xc!=li[yc].len) {
    li[yc+1].ptr=malloc(li[yc+1].mem=li[yc+1].len=li[yc].len-xc);
    if(!li[yc+1].ptr) err(1,"Allocation failed");
    li[yc+1].own=1;
    memcpy(li[yc+1].ptr,li[yc].ptr+xc,li[yc+1].len);
  } else {
    li[yc+1].ptr=li[yc].ptr+xc;
    li[yc+1].own=0;
    li[yc+1].mem=li[yc+1].len=li[yc].len-xc;
  }
  li[yc].len=xc;
  ++nlines;
  ++nchars;
}

static void line_destroy(Uint16 yc) {
  nchars-=lines[yc].len;
  if(nlines>1) {
    if(lines[yc].own) free(lines[yc].ptr);
    memmove(lines+yc,lines+yc+1,(nlines-yc-1)*sizeof(Line));
    --nlines; --nchars;
  } else {
    lines->len=0;
  }
}

static Uint8*text_editor_1(Uint8*text) {
  char buf[42];
  Line*li;
  Uint8 prefix,askch;
  Sint32 i,j,k;
  Uint16 xc,yc,scrol;
  Uint8*p=text;
  Uint8*q;
  if(!text) {
    p=text=strdup("");
    if(!p) err(1,"Allocation failed");
  }
  nchars=strlen(text);
  nlines=0;
  for(i=0;i<nchars;i++) if(text[i]=='\n') nlines++;
  if(!nchars || !nlines || text[nchars-1]!='\n') nlines++,nchars++;
  lines=calloc(nlines,sizeof(Line));
  if(!lines) err(1,"Allocation failed");
  for(i=0;i<nlines;i++) {
    lines[i].ptr=p;
    q=strchrnul(p,'\n');
    lines[i].len=lines[i].mem=q-p;
    p=q+(*q?1:0);
    lines[i].own=0;
  }
  xc=yc=scrol=prefix=askch=0;
  display:
  memset(v_color,0x00,80*24);
  memset(v_color+80*24,0x33,80);
  if(yc>=nlines) yc=nlines-1;
  if(yc<scrol) scrol=yc;
  if(yc>scrol+23) scrol=yc-23;
  for(i=0;i<24 && i+scrol<nlines;i++) {
    li=lines+i+scrol;
    j=i*80;
    if(li->len) {
      draw_text(0,i,p=li->ptr,0x07,li->len>80?80:li->len);
      if(*p=='@' || *p=='/' || *p=='#' || *p=='?' || *p==':' || *p=='\'' || *p=='$' || *p=='!') {
        v_color[j]=0x0A;
        if(*p=='@' && !i && !scrol) {
          for(k=1;k<80 && k<li->len && p[k]!='=';k++) v_color[j+k]=0x0E;
        } else if(*p=='!') {
          for(k=1;k<80 && k<li->len && p[k]!=';';k++);
          if(k<li->len && p[k]==';') v_color[j+k]=0x0D;
        }
      }
    }
    if(li->len<80) v_color[li->len+j]=0x03,v_char[li->len+j]=0x11;
  }
  loop:
  if(yc>=nlines || yc<scrol || yc>scrol+23) goto display;
  li=lines+yc;
  j=(i=yc-scrol)*80;
  memset(v_color+j,0,80);
  if(li->len) {
    draw_text(0,i,p=li->ptr,0x07,li->len>80?80:li->len);
    if(*p=='@' || *p=='/' || *p=='#' || *p=='?' || *p==':' || *p=='\'' || *p=='$' || *p=='!') {
      v_color[j]=0x0A;
      if(*p=='@' && !i && !scrol) {
        for(k=1;k<80 && k<li->len && p[k]!='=';k++) v_color[j+k]=0x0E;
      } else if(*p=='!') {
        for(k=1;k<80 && k<li->len && p[k]!=';';k++);
        if(k<li->len && p[k]==';') v_color[j+k]=0x0D;
      }
    }
  }
  if(li->len<80) v_color[li->len+j]=0x03,v_char[li->len+j]=0x11;
  if(xc>lines[yc].len) xc=lines[yc].len;
  draw_text(55,24,buf,0x30,snprintf(buf,11,"[%05d]",nchars));
  v_color[63+24*80]=v_color[64+24*80]=0x30;
  v_char[63+24*80]=prefix?'^':0;
  v_char[64+24*80]=" QK"[prefix];
  draw_text(66,24,config.text_editor_insert?"INS":"OVR",0x30,3);
  draw_text(70,24,buf,0x30,snprintf(buf,11,"%05d:%03d",yc+1,xc+1));
  v_xcur=xc;
  v_ycur=yc-scrol;
  redisplay();
  do { if(!next_event()) goto exit; } while(event.type!=SDL_KEYDOWN);
  k=(!(event.key.keysym.mod&(KMOD_ALT|KMOD_META))?event.key.keysym.unicode:0)?:-event.key.keysym.sym;
  if(k>0x7F) k=0; else if(prefix) k+=prefix*0x100,prefix=0;
#define case_CTRL(x) case x-'@'
#define case_CTRLQ(x) case 0x100+x-'@': case 0x100+x: case 0x100+x+'a'-'A'
#define case_CTRLK(x) case 0x200+x-'@': case 0x200+x: case 0x200+x+'a'-'A'
  switch(k) {
    case_CTRL('A'): if(xc) --xc; while(xc && lines[yc].ptr[xc]!=' ') --xc; break;
    case_CTRLQ('C'): xc=lines[yc=nlines-1].len; goto display;
    case_CTRL('D'): case -SDLK_RIGHT: if(xc<lines[yc].len) ++xc; break;
    case_CTRLQ('D'): case -SDLK_END: xc=lines[yc].len; break;
    case_CTRL('E'): case -SDLK_UP: if(yc) --yc; break;
    case_CTRLQ('E'): yc=scrol; if(xc>lines[yc].len) xc=lines[yc].len; goto display;
    case_CTRL('F'): if(xc<lines[yc].len) ++xc; while(xc<lines[yc].len && lines[yc].ptr[xc]!=' ') ++xc; break;
    case_CTRL('G'): case 0x7F: case -SDLK_DELETE: if(xc<lines[yc].len) { memmove(lines[yc].ptr+xc,lines[yc].ptr+xc+1,lines[yc].len-1-xc); --lines[yc].len; --nchars; } break;
    case_CTRL('H'): case -SDLK_BACKSPACE: if(xc) { if(xc<lines[yc].len) memmove(lines[yc].ptr+xc-1,lines[yc].ptr+xc,lines[yc].len-xc); --xc; --lines[yc].len; --nchars; } break;
    case_CTRL('J'): if(yc<nlines) yc++,xc=0; goto display;
    case_CTRL('K'): prefix=2; break;
    case_CTRL('M'): line_break(xc,yc); xc=0; ++yc; goto display;
    case_CTRL('P'): xc+=ins_char(yc,xc,ask_color_char(1,askch)); goto display;
    case_CTRL('Q'): prefix=1; break;
    case_CTRLQ('R'): xc=yc=0; goto display;
    case_CTRL('S'): case -SDLK_LEFT: if(xc) --xc; break;
    case_CTRLQ('S'): case -SDLK_HOME: xc=0; break;
    case_CTRL('V'): case -SDLK_INSERT: config.text_editor_insert^=1; break;
    case_CTRL('W'): if(scrol) --scrol; if(yc>scrol+23) --yc; goto display;
    case_CTRL('X'): case -SDLK_DOWN: if(yc<nlines) ++yc; break;
    case_CTRL('Y'): line_destroy(yc); if(yc==nlines) --yc; goto display;
    case_CTRLQ('Y'): nchars+=xc-lines[yc].len; lines[yc].len=xc; break;
    case_CTRL('Z'): if(scrol<nlines-1) ++scrol; if(yc<scrol) ++yc; goto display;
    case 0x1B: goto exit;
    case 0x20 ... 0x7E: case 0x300 ... 0x3FF: xc+=ins_char(yc,xc,k); break;
    case -SDLK_SLASH: case -SDLK_QUESTION: online_help("edittext",0); goto display;
    case -SDLK_BACKQUOTE:
      printf("xc=%d yc=%d nlines=%d nchars=%d\n",xc,yc,nlines,nchars);
      for(i=0;i<nlines;i++) printf("[%d] len=%d mem=%d own=%d ptr=%p\n",i,lines[i].len,lines[i].mem,lines[i].own,lines[i].ptr);
      goto display;
  }
#undef case_CTRL
#undef case_CTRLQ
#undef case_CTRLK
  goto loop;
  exit:
  if(nchars<=1) {
    for(i=0;i<nlines;i++) if(lines[i].own) free(lines[i].ptr);
    free(lines);
    free(text);
    return 0;
  }
  for(i=0;i<nlines;i++) if(lines[i].own || lines[i].len!=lines[i].mem) break;
  if(i!=nlines) {
    p=malloc(nchars+1);
    if(!p) err(1,"Allocation failed");
    for(i=j=0;i<nlines;i++) {
      if(lines[i].len) memcpy(p+j,lines[i].ptr,lines[i].len);
      p[j+lines[i].len]='\n';
      j+=lines[i].len+1;
      if(lines[i].own) free(lines[i].ptr);
    }
    p[nchars]=0;
    free(text);
    text=p;
  }
  free(lines);
  return text;
}

Uint8*text_editor(Uint8*t) {
  win_refresh();
  if(config.text_editor_command) {
    char tem[]="/tmp/XXXXXX";
    int fd=mkstemp(tem);
    off_t o;
    if(fd==-1) {
      warn("Cannot open temporary file");
      alert_text("Cannot open temporary file");
      return t;
    }
    if(t) write(fd,t,strlen(t));
    setenv("textfile",tem,1);
    system(config.text_editor_command);
    unsetenv("textfile");
    unlink(tem);
    free(t);
    o=lseek(fd,0,SEEK_END);
    if(o==-1) err(1,"Cannot seek temporary file");
    if(o>0xFFFE) o=0xFFFE;
    if(!o) {
      close(fd);
      return 0;
    }
    t=malloc(o+1);
    if(!t) err(1,"Allocation failed");
    lseek(fd,0,SEEK_SET);
    read(fd,t,o);
    close(fd);
    t[o]=0;
    return t;
  }
  return text_editor_1(t);
}

