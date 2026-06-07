#if 0
gcc $CFLAGS -c -Wno-unused-result -std=gnu99 special.c `sdl-config --cflags`
exit
#endif

#include "common.h"

Uint16 nspecopt;
SpecialOption*specopt;

typedef struct {
  ASN1_Value id;
  Uint16 numb,value;
} SpConfig;

static SpConfig*spconf;
static Uint16 nspconf;

Uint16 spec_auto_value(Uint32 key) {
  return 0;
}

static Uint8 spec_possible_value(Uint32 key,Uint16 nv,const Uint16*val) {
  return 0;
}

static Uint8 spec_possible(const ASN1_Value*oids,Uint16 nv,const Uint16*val,SpecialOption*obj) {
  Uint32 key;
  Uint16 inf=obj-specopt;
  ASN1_Value v;
  int i,j;
  for(i=0;i<nspconf;i++) if(!spconf[i].id.type && spconf[i].numb==inf) {
    obj->key=SPECI_UNKNOWN;
    obj->flag&=~SPECF_VARIABLE;
    obj->value=spconf[i].value;
    if(nv) {
      for(j=0;j<nv;j++) if(val[j]==obj->value) return 0;
    } else {
      if(obj->value>=val[0] && obj->value<=val[1]) return 0;
    }
    errx(1,"Incorrect value in [Special] division");
  }
  if(!asn1_first_of(&v,oids)) do {
    for(i=0;i<nspconf;i++) if(v.class==ASN1_UNIVERSAL && spconf[i].id.type==v.type && spconf[i].id.length==v.length && !memcmp(spconf[i].id.data,v.data,v.length)) {
      if(nv) {
        for(j=0;j<nv;j++) if(val[j]==spconf[i].value) goto found;
      } else {
        if(spconf[i].value>=val[0] && spconf[i].value<=val[1]) goto found;
      }
      continue;
      found:
      obj->key=SPECI_UNKNOWN;
      obj->flag&=~SPECF_VARIABLE;
      obj->value=spconf[i].value;
      return 0;
    }
    if(v.class==ASN1_UNIVERSAL && v.type==ASN1_RELATIVE_OID && v.length>1 && v.length<6 && v.data[0]==8) {
      key=v.data[1]<<24;
      if(v.length>2) key|=v.data[2]<<16;
      if(v.length>3) key|=v.data[3]<<8;
      if(v.length>4) key|=v.data[4]<<0;
      if(spec_possible_value(key,nv,val)) {
        obj->value=spec_auto_value(obj->key=key);
        return 0;
      }
    }
  } while(!asn1_next_of(&v,oids));
  return 1;
}

static Uint8 draw_digit(Uint16 n,Uint8 p) {
  const NumericFormat*f=num_format+(p>>4);
  n/=f->div;
  p&=15;
  while(p--) n/=10;
  if(!n) return f->lead;
  if(f->code==NF_DECIMAL) return (n%10)+'0';
  if(f->code==NF_COMMA) return f->mark;
  return 0;
}

static void draw_sc(const WindowInfo*wind,Uint16 flag,Uint16 ln,Uint16 lc,Uint16 cs,Uint8 ok,Uint8 wc) {
  Uint8 cmd,col,chr;
  int i;
  for(i=0;i<80*25;i++) {
    cmd=cur_screen.command[i]; col=cur_screen.color[i]; chr=cur_screen.parameter[i];
    switch(cmd) {
      case SC_BACKGROUND+1 ... SC_BACKGROUND+15:
        v_font[i]=(cur_screen.flag&SF_ALT_MODE?VF_FRONT|VF_ALTERNATE:VF_FRONT);
        if(cmd&1) v_char[i]=chr;
        if(cmd&2) v_color[i]=col;
        break;
      case SC_NUMERIC ... SC_NUMERIC_SPECIAL+14: case SC_IND_CURSOR: num:
        v_font[i]=VF_FRONT;
        v_color[i]=col;
        if(cmd==SC_SPEC_TEXT_LINE_NUMBER) v_char[i]=draw_digit(ln,chr);
        else if(cmd==SC_SPEC_TEXT_LINE_COUNT) v_char[i]=draw_digit(lc,chr);
        else v_char[i]=0;
        break;
      case SC_SPEC_CONTEXT_SPECIFIC:
        if(!ok) goto num;
        v_font[i]=VF_FRONT;
        v_char[i]=draw_digit(cs,chr);
        v_color[i]=wc?:col;
        break;
      case SC_TEXT ... SC_TEXT+15:
        v_font[i]=VF_FRONT;
        v_char[i]=chr;
        v_color[i]=col;
        break;
      case 0x80 ... 0xFF:
        v_font[i]=VF_FRONT;
        v_char[i]=(flag&(1<<(cmd&15)))?chr:32;
        v_color[i]=col;
        break;
    }
  }
}

static void draw_menu_field(const ASN1_Value*v0,int at,int len,const WindowInfo*wind) {
  ASN1_Value v1,v2,v3;
  int end=at+len;
  int b;
  Uint16 m,n;
  Uint8 c=0;
  switch(v0->type) {
    case SPECT_OPTION:
      if(asn1_first_of(&v1,v0)) break;
      if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_PC_STRING && asn1_next_of(&v1,v0)) break;
      if(v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&n)) break;
      asn1_next_of(&v1,v0); // OIDs
      asn1_next_of(&v1,v0); // flags
      asn1_next_of(&v1,v0); // screen
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_CONTEXT_SPECIFIC) break;
      if(specopt[n].flag&SPECF_LOCKED) c=wind->wcolor[WC_FIXED_ITEM];
      if(!c) if(specopt[n].flag&SPECF_VARIABLE) c=wind->wcolor[WC_KEY_ITEM];
      for(b=at;b<end;b++) {
        if(cur_screen.command[b]==SC_SPEC_CONTEXT_SPECIFIC) {
          v_char[b]=draw_digit(specopt[n].value,cur_screen.parameter[b]);
          if(c) v_color[b]=c;
        }
      }
      if(v1.type==1 && !asn1_first_of(&v2,&v1)) do {
        if(v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_SEQUENCE || asn1_first_of(&v3,&v2)) continue;
        if(v3.class==ASN1_UNIVERSAL && v3.type==ASN1_PRINTABLE_STRING && asn1_next_of(&v3,&v2)) continue;
        if(v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_INTEGER || asn1_decode_number(&v3,ASN1_INTEGER,&m) || m!=specopt[n].value || asn1_next_of(&v3,&v2)) continue;
        if(v3.class==ASN1_UNIVERSAL && v3.type==ASN1_PC_STRING) {
          for(b=0;b<v3.length && b<len;b++) {
            if((cur_screen.command[b+at]&0xF0)==SC_TEXT) {
              v_char[b+at]=v3.data[b];
              v_color[b+at]=c?:((cur_screen.command[b+at]&0x0F)|(v_color[b+at]&0xF0));
            }
          }
          if(c) for(;b<len;b++) v_color[b+at]=c;
        }
        return;
      } while(!asn1_next_of(&v2,&v1));
      break;
  }
}

static Sint32 do1menu(const ASN1_Value*v0);

static void show1menu(const ASN1_Value*v0) {
  const char*e;
  ASN1_Value*vpag;
  Uint16*spag;
  Uint16 npag=0;
  ASN1_Value v1,v2;
  FILE*fp;
  WindowInfo wind={};
  Uint16 page=0;
  Sint32 re;
  int i,j;
  if(asn1_first_of(&v1,v0)) return;
  do {
    if(!v1.class && v1.type==ASN1_SEQUENCE) ++npag; else if(v1.class || v1.type!=ASN1_INTEGER) errx(1,"Error in OPTION.DER");
    if(npag==0xFFFE) errx(1,"Too many pages");
  } while(!asn1_next_of(&v1,v0));
  if(!npag) return;
  vpag=calloc(npag,sizeof(ASN1_Value));
  spag=calloc(npag,sizeof(Uint16));
  if(!vpag || !spag) err(1,"Allocation failed");
  for(asn1_first_of(&v1,v0),i=j=0;i<npag;asn1_next_of(&v1,v0)) {
    if(v1.type==ASN1_SEQUENCE) {
      spag[i]=re; vpag[i++]=v1;
    } else if(v1.type==ASN1_INTEGER) {
      asn1_decode_number(&v1,ASN1_INTEGER,&re);
    }
  }
  goto loadscreen;
  page:
  if(cur_screen_id!=spag[page]) {
    loadscreen:
    if(!(fp=open_lump_by_number(cur_screen_id=spag[page],"SCR","r"))) errx(1,"Cannot load screen #%d",cur_screen_id);
    if(e=load_screen(fp)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
    fclose(fp);
    work_varproperties(&cur_screen.varprop);
    if(fp=open_lump_by_number(cur_screen_id,"WIN","r")) {
      if(e=load_window(fp,&wind)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
      fclose(fp);
    } else {
      wind=(WindowInfo){};
    }
  }
  v0=vpag+page;
  draw:
  draw_sc(&wind,0,page+(wind.flag&WF_ZERO_BASED?0:1),npag,0,0,0);
  if(!asn1_first_of(&v1,v0)) do {
    if(!asn1_first_of(&v2,&v1) && v2.class==ASN1_UNIVERSAL && v2.type==ASN1_PRINTABLE_STRING) asn1_next_of(&v2,&v1);
    if(v2.class==ASN1_UNIVERSAL && v2.type==ASN1_INTEGER) {
      if(asn1_decode_number(&v2,ASN1_INTEGER,&i) || i<0 || i>1999) errx(1,"Improper screen position");
      if(asn1_next_of(&v2,&v1) || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_INTEGER || asn1_decode_number(&v2,ASN1_INTEGER,&j) || j<0 || j>80 || i+j>2000) errx(1,"Improper screen position");
      if(!asn1_next_of(&v2,&v1) && v2.class==ASN1_CONTEXT_SPECIFIC) draw_menu_field(&v2,i,j,&wind);
    }
  } while(!asn1_next_of(&v1,v0));
  redisplay();
  key:
  do { if(!next_event()) errx(0,"No events available."); } while(event.type!=SDL_KEYDOWN);
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: goto stop;
    case SDLK_TAB: page=(page+1)%npag; goto page;
    case SDLK_PAGEUP: if(page) --page; goto page;
    case SDLK_PAGEDOWN: if(page<npag-1) ++page; goto page;
    case SDLK_HOME: page=0; goto page;
    case SDLK_END: page=npag-1; goto page;
    case SDLK_a ... SDLK_z: i=event.key.keysym.sym+'A'-'a'; goto selection;
    case SDLK_0 ... SDLK_9: i=event.key.keysym.sym; goto selection;
    case SDLK_F8: goto draw;
    default: goto key;
  }
  goto draw;
  selection:
  if(!asn1_first_of(&v1,v0)) do {
    if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_SEQUENCE && !asn1_first_of(&v2,&v1)) {
      if(v2.class==ASN1_UNIVERSAL && v2.type==ASN1_PRINTABLE_STRING && v2.length==1 && v2.data[0]==i) {
        while(!(i=asn1_next_of(&v2,&v1)) && v2.class==ASN1_UNIVERSAL && v2.type==ASN1_INTEGER);
        if(!i) {
          re=do1menu(&v2);
          if(re==-2) goto stop;
          if(re>=0 && re<npag) page=re;
          if(re==-1 && !asn1_next_of(&v2,&v1) && v2.class==ASN1_UNIVERSAL && v2.type==ASN1_BOOLEAN && v2.length && v2.data[0]) goto stop;
        }
        goto page;
      }
    }
  } while(!asn1_next_of(&v1,v0));
  goto key;
  stop:
  for(i=0;i<npag;i++) asn1_free(vpag+i);
  free(vpag);
  free(spag);
}

typedef struct {
  Uint8 text[60];
  Uint8 key;
  Uint16 value;
} SpChoice;

static void show1option(const ASN1_Value*v0) {
  SpecialOption*op;
  ASN1_Value v1,v2,v3;
  SpChoice cho[32];
  Uint8 ncho=0;
  Uint8 cur=0;
  Uint8 wc,x,y;
  Uint16 a,mini,maxi,typ;
  FILE*fp;
  WindowInfo wind;
  int i,j,k;
  const char*e;
  if(asn1_first_of(&v1,v0) || v1.constructed) err: errx(1,"Error in OPTION.DER");
  *textbuf=ntextbuf=0;
  if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_PC_STRING && v1.length<79) {
    memcpy(textbuf,v1.data,ntextbuf=v1.length); textbuf[ntextbuf]=0;
    if(asn1_next_of(&v1,v0)) goto err;
  }
  if(v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&a) || a>nspecopt) goto err;
  op=specopt+a;
  asn1_next_of(&v1,v0); // OIDs
  asn1_next_of(&v1,v0); // flags
  if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&a)) goto err;
  if(!(fp=open_lump_by_number(cur_screen_id=a,"SCR","r"))) errx(1,"Cannot load screen #%d",cur_screen_id);
  if(e=load_screen(fp)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
  fclose(fp);
  work_varproperties(&cur_screen.varprop);
  if(fp=open_lump_by_number(cur_screen_id,"WIN","r")) {
    if(e=load_window(fp,&wind)) errx(1,"Error loading screen #%d: %s",cur_screen_id,e);
    fclose(fp);
  } else {
    wind=(WindowInfo){};
  }
  if(asn1_next_of(&v1,v0) || v1.class!=ASN1_CONTEXT_SPECIFIC || v1.type>1) goto err;
  if(typ=v1.type) {
    if(!asn1_first_of(&v2,&v1)) do {
      if(ncho==25 || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_SEQUENCE) goto err;
      if(asn1_first_of(&v3,&v2) || v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_PRINTABLE_STRING || v3.length!=1) goto err;
      cho[ncho].key=v3.data[0];
      if((v3.data[0]<'0' || v3.data[0]>'9') && (v3.data[0]<'A' || v3.data[0]>'Z')) goto err;
      if(asn1_next_of(&v3,&v2) || v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_INTEGER || asn1_decode_number(&v3,ASN1_INTEGER,&cho[ncho].value)) goto err;
      if(asn1_next_of(&v3,&v2) || v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_PC_STRING || v3.length>59) goto err;
      memcpy(cho[ncho].text,v3.data,v3.length); cho[ncho].text[v3.length]=0;
      if(cho[ncho].value==op->value) cur=ncho;
      ncho++;
    } while(!asn1_next_of(&v2,&v1));
    if(!ncho) goto err;
  } else {
    if(asn1_first_of(&v2,&v1) || asn1_decode_number(&v2,ASN1_AUTO,&mini)) goto err;
    if(asn1_next_of(&v2,&v1) || asn1_decode_number(&v2,ASN1_AUTO,&maxi)) goto err;
  }
  show:
  if(op->flag&SPECF_LOCKED) i=WC_FIXED_ITEM; else if(op->flag&SPECF_VARIABLE) i=WC_KEY_ITEM; else i=WC_NORMAL_ITEM;
  draw_sc(&wind,op->flag,typ?cur+(wind.flag&WF_ZERO_BASED?0:1):mini,typ?ncho:maxi,op->value,1,wc=wind.wcolor[i]);
  if((cur_screen.flag&SF_LEFT_ALIGN_MESSAGE) || cur_screen.message_x-ntextbuf/2<cur_screen.message_l) x=cur_screen.message_l;
  else x=cur_screen.message_x-ntextbuf/2;
  for(i=0;i<ntextbuf && x<=cur_screen.message_r && x<80;i++,x++) {
    k=cur_screen.message_y*80+x;
    v_char[k]=textbuf[i];
    v_color[k]=cur_screen.color[k];
  }
  if(typ) {
    i=wind.wcolor[WC_SELECTED_ITEM];
    if(i==0xFF) wc=(wc>>4)|(wc<<4); else if(wind.flag&WF_XOR_COLOR) wc^=i; else wc|=i;
    x=cur_screen.soft_edge[DIR_W];
    y=cur_screen.hard_edge[DIR_N];
    for(i=0;i<ncho && y+i<25;i++) {
      for(j=a=0;a<80;a++) {
        k=(y+i)*80+a;
        if(!j && a>=x && !cho[i].text[a-x]) j=1;
        if((cur_screen.command[k]&0xF0)==SC_TEXT) {
          v_char[k]=0;
          switch(wind.command[a]) {
            case 'A': case 'L': case 'P': indicator:
              if(wind.color[a]!=0x11) v_color[k]=(wind.color[a]==0x22?(v_color[k]&0xF0)|(cur_screen.command[k]&0x0F):wind.color[a]);
              v_char[k]=wind.parameter[a]?:cur_screen.parameter[k];
              break;
            case 'C':
              if(i==cur) goto indicator;
              break;
            case 'K':
              if(wind.color[a]!=0x11) v_color[k]=(wind.color[a]==0x22?(v_color[k]&0xF0)|(cur_screen.command[k]&0x0F):wind.color[a]);
              v_char[k]=cho[i].key;
              break;
            case 'T':
              if(i==cur && wc) v_color[k]=wc; else if(i==cur && wind.wcolor[WC_SELECTED_ITEM]==0xFF) v_color[k]=(v_color[k]<<4)|(v_color[k]>>4);
              if(a>=x && a<=cur_screen.soft_edge[DIR_E] && !j) {
                if(wind.color[a]!=0x11) v_color[k]=(wind.color[a]==0x22?(v_color[k]&0xF0)|(cur_screen.command[k]&0x0F):wind.color[a]);
                v_char[k]=cho[i].text[a-x];
              } else {
                v_char[k]=wind.parameter[a]?:cur_screen.parameter[k];
              }
              break;
            default:
              if(i==cur && wc) v_color[k]=wc;
              if(a>=x && a<=cur_screen.soft_edge[DIR_E] && !j) {
                v_color[k]=(v_color[k]&0xF0)|(cur_screen.command[k]&0x0F);
                v_char[k]=cho[i].text[a-x];
              }
          }
        } else if(cur_screen.command[k]==SC_IND_CURSOR && i==cur) {
          v_char[k]=cur_screen.parameter[k];
        }
      }
    }
    if(wind.flag&WF_SINGLE_ENDS) {
      for(k=(y+ncho+1)*80;k<80*25;k++) if((cur_screen.command[k]&0xF0)==SC_TEXT) v_char[k]=0;
    }
  }
  redisplay();
  key:
  do { if(!next_event()) errx(0,"No events available."); } while(event.type!=SDL_KEYDOWN);
  switch(i=event.key.keysym.sym) {
    case SDLK_ESCAPE: case SDLK_RETURN: case SDLK_SPACE:
      if(i!=SDLK_ESCAPE) op->flag&=~SPECF_VARIABLE;
      if(!typ) {
        if(op->value<mini) op->value=mini; else if(op->value>maxi) op->value=maxi;
      }
      return;
    case SDLK_TAB: if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE; if(typ) cur=(cur+1)%ncho; break;
    case SDLK_UP: if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE; if(typ && cur) --cur; else if(!typ && op->value<maxi) ++op->value; break;
    case SDLK_DOWN: if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE; if(typ && cur<ncho-1) ++cur; else if(!typ && op->value>mini) --op->value; break;
    case SDLK_HOME: if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE; if(typ) cur=0; else op->value=0; break;
    case SDLK_END: if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE; if(typ) cur=ncho-1; break;
    case SDLK_BACKSPACE: if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE; if(!typ) op->value/=10; break;
    case SDLK_0 ... SDLK_9:
      if(typ) {
        selection:
        for(a=0;a<ncho;a++) if(cho[a].key==i) {
          if((op->flag&SPECF_LOCKED) && a!=cur) break;
          op->flag&=~SPECF_VARIABLE;
          op->value=cho[a].value;
          return;
        }
      } else if(10*op->value+i-'0'<=maxi) {
        if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE;
        op->value=10*op->value+i-'0';
      }
      break;
    case SDLK_a ... SDLK_z: i+='A'-'a'; if(typ) goto selection; break;
    case SDLK_DELETE: if(op->flag&SPECF_LOCKED) break; else op->flag&=~SPECF_VARIABLE; if(!typ) op->value=0; break;
    default: goto key;
  }
  if(typ) op->value=cho[cur].value;
  goto show;
}

static Sint32 do1menu(const ASN1_Value*v0) {
  ASN1_Value v1,v2;
  Uint16 a,b,c;
  Sint32 d;
  if(v0->class!=ASN1_CONTEXT_SPECIFIC || !v0->constructed) errx(1,"Incorrect class in OPTION.DER lump");
  switch(v0->type) {
    case SPECT_MENU:
      show1menu(v0);
      break;
    case SPECT_OPTION:
      show1option(v0);
      break;
    case SPECT_HELP:
      v_status[1]=0;
      a=memory[MEM_TEXT_SCREEN];
      b=memory[MEM_WINDOW_KEY_EVENT];
      memory[MEM_WINDOW_KEY_EVENT]=0;
      ntextbuf=0;
      board_info.screen=cur_screen_id;
      if(asn1_first_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_AUTO,memory+MEM_TEXT_SCREEN)) goto err;
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_VISIBLE_STRING || v1.length<1 || v1.length>8) goto err;
      memcpy(textbuf,v1.data,ntextbuf=v1.length); textbuf[ntextbuf]=0;
      show_help_file();
      v_status[1]=21;
      memory[MEM_TEXT_SCREEN]=a;
      memory[MEM_WINDOW_KEY_EVENT]=b;
      break;
    case SPECT_GOTOPAGE:
      if(!asn1_first_of(&v1,v0) && v1.class==ASN1_UNIVERSAL && v1.type==ASN1_INTEGER && !asn1_decode_number(&v1,ASN1_AUTO,&a)) return a;
      break;
    case SPECT_CANCEL:
      return -2;
    case SPECT_SOUND:
      if(!asn1_first_of(&v1,v0) && v1.class==ASN1_UNIVERSAL && v1.type==ASN1_VISIBLE_STRING && v1.length && v1.length<79) {
        audio_set_sfx("@I0ZX");
        memcpy(textbuf,v1.data,v1.length); textbuf[v1.length]=0;
        audio_set_sfx(textbuf);
        ntextbuf=*textbuf=0;
      }
      break;
    default: errx(1,"Unknown type in OPTION.DER lump");
    err: errx(1,"Error in OPTION.DER lump");
  }
  return -1;
}

void special_option_menu(void) {
  FILE*fp;
  Uint8 sbuf[81];
  Uint8 snbuf,mode;
  Uint8 mname[9];
  Uint16 msong,scrid,scrb;
  ASN1_Value root={};
  ASN1_Value state={};
  ASN1_Encoder*enc;
  event.type=SDL_NOEVENT;
  if((v_status[1] && v_status[1]!=32) || !nspecopt) return;
  set_timer(0);
  if(fp=open_lump("OPTION.DER","r")) {
    scrb=board_info.screen;
    v_status[1]=21;
    if(asn1_read_item(fp,&root,0)) errx(1,"ASN.1 error in OPTION.DER lump (this shouldn't happen)");
    fclose(fp);
    scrid=cur_screen_id;
    memcpy(sbuf,textbuf,81);
    snbuf=ntextbuf;
    mode=v_mode;
    memcpy(mname,music_name,9);
    msong=music_song;
    enc=asn1_start_encoding_value(&state);
    if(!enc) err(1,"Allocation failed");
    save_fontpal_state(enc);
    do1menu(&root);
    board_info.screen=scrb;
    asn1_free(&root);
    v_mode=mode;
    load_fontpal_state(&state);
    asn1_free(&state);
    if(msong!=music_song || memcmp(mname,music_name,9)) audio_set_music(mname,msong);
    memcpy(textbuf,sbuf,81);
    ntextbuf=snbuf;
    if(scrid!=cur_screen_id) {
      fp=open_lump_by_number(cur_screen_id=scrid,"SCR","r");
      if(!fp) err(1,"Cannot open %04X.SCR",cur_screen_id);
      if(load_screen(fp)) errx(1,"Error restoring screen");
      fclose(fp);
    }
    v_status[1]=0;
  }
  redisplay();
}

static const char*load_special_option_1(const ASN1_Value*v) {
  ASN1_Value v1,v2,v3;
  if(v->class!=ASN1_CONTEXT_SPECIFIC) return "Incorrect class in OPTION.DER lump";
  if(v->type==SPECT_MENU) {
    const char*e;
    if(!asn1_first_of(&v1,v)) do {
      if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_SEQUENCE && !asn1_first_of(&v2,&v1)) do {
        if(asn1_first_of(&v3,&v2)) err: return "Error in OPTION.DER lump";
        if(v3.class==ASN1_UNIVERSAL && v3.type==ASN1_PRINTABLE_STRING && asn1_next_of(&v3,&v2)) goto err;
        while(v3.class==ASN1_UNIVERSAL && v3.type==ASN1_INTEGER) if(asn1_next_of(&v3,&v2)) goto err;
        if(e=load_special_option_1(&v3)) return e;
      } while(!asn1_next_of(&v2,&v1));
    } while(!asn1_next_of(&v1,v));
  } else if(v->type==SPECT_OPTION) {
    ASN1_Value v4;
    Uint16 n;
    SpecialOption*o;
    Uint16 val[32];
    if(asn1_first_of(&v1,v)) goto err;
    if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_PC_STRING && asn1_next_of(&v1,v)) goto err;
    if(v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&n) || (n&~0x7FFF)) goto err;
    if(nspecopt<=n) {
      specopt=realloc(specopt,(n+1)*sizeof(SpecialOption));
      if(!specopt) err(1,"Allocation failed");
      while(nspecopt<=n) specopt[nspecopt++]=(SpecialOption){.key=SPECI_VACANT};
    }
    o=specopt+n;
    if(o->key!=SPECI_VACANT) return "Duplicate special option number";
    if(asn1_next_of(&v1,v) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_SEQUENCE) goto err;
    v2=v1;
    if(asn1_next_of(&v1,v) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_BIT_STRING || v1.constructed || v1.length<1 || v1.length>3) goto err;
    if(v1.length>1) o->flag=v1.data[1]<<8;
    if(v1.length>2) o->flag|=v1.data[2];
    if(!config.variable_special_options) o->flag&=~SPECF_VARIABLE;
    if(asn1_next_of(&v1,v) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER) goto err;
    if(asn1_next_of(&v1,v) || v1.class!=ASN1_CONTEXT_SPECIFIC) return "Incorrect class in OPTION.DER lump";
    if(v1.type==0) {
      if(asn1_first_of(&v3,&v1) || v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_INTEGER || asn1_decode_number(&v3,ASN1_INTEGER,val+0)) goto err;
      if(asn1_next_of(&v3,&v1) || v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_INTEGER || asn1_decode_number(&v3,ASN1_INTEGER,val+1)) goto err;
      if(val[1]<val[0]) goto err;
      n=spec_possible(&v2,0,val,o);
    } else if(v1.type==1) {
      for(n=0;;n++) {
        if(n?asn1_next_of(&v3,&v1):asn1_first_of(&v3,&v1)) break; else if(n==32) return "Too many choices in special option definition";
        if(asn1_first_of(&v4,&v3) || v4.class!=ASN1_UNIVERSAL || v4.type!=ASN1_PRINTABLE_STRING || v4.length!=1) goto err;
        if(asn1_next_of(&v4,&v3) || v4.class!=ASN1_UNIVERSAL || v4.type!=ASN1_INTEGER || asn1_decode_number(&v4,ASN1_INTEGER,val+n)) goto err;
      }
      if(!n) goto err;
      n=spec_possible(&v2,n,val,o);
    } else goto err;
    if(n) {
      if(asn1_next_of(&v1,v) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&o->value)) goto err;
      o->key=SPECI_UNKNOWN;
      o->flag&=~SPECF_VARIABLE;
    }
  }
  return 0;
}

const char*load_special_options(FILE*f) {
  const char*e=0;
  ASN1_Value v={};
  if(asn1_read_item(f,&v,0)) return "ASN.1 error in OPTION.DER lump";
  e=load_special_option_1(&v);
  asn1_free(&v);
  if(e) {
    free(specopt);
    specopt=0;
    nspecopt=0;
    return e;
  }
  if(!nspecopt) {
    nspecopt=1;
    if(!(specopt=malloc(sizeof(SpecialOption)))) err(1,"Allocation failed");
    *specopt=(SpecialOption){.key=SPECI_VACANT};
  }
  return 0;
}

void config_special_options(char*line) {
  SpConfig*o;
  char*p;
  if(nspconf==0xFFFE) errx(1,"Too many definitions in [Special] division");
  spconf=realloc(spconf,(++nspconf)*sizeof(SpConfig));
  if(!spconf) err(1,"Allocation failed");
  o=spconf+nspconf-1;
  *o=(SpConfig){};
  p=strchr(line,'=');
  if(!p) errx(1,"Syntax error in [Special] division");
  *p++=0;
  o->value=strtol(p,0,10);
  if(!strncmp(line,"2.25.196954517921581521497869385664052876310.",45)) {
    line[42]=line[43]='.';
    if(asn1_make_oid(line+42,&o->id)) errx(1,"Syntax error in [Special] division");
  } else if(strchr(line,'.')) {
    if(asn1_make_oid(line,&o->id)) errx(1,"Syntax error in [Special] division");
  } else {
    o->numb=strtol(line,0,10);
  }
}

void end_config_special_options(void) {
  int i;
  for(i=0;i<nspconf;i++) asn1_free(&spconf[i].id);
  free(spconf);
  spconf=0;
}

static void spec_debug_callback(Uint16 n,int y,void*uz) {
  char buf[40];
  draw_text(2,y,buf,7,snprintf(buf,40,"%5d",n));
  draw_text(9,y,buf,14,snprintf(buf,40,"%5d",specopt[n].value));
  draw_text(19,y,buf,12,snprintf(buf,40,"%08llX",(long long)specopt[n].key));
  draw_text(29,y,"\xFA\xFA\xFA\xFA\xFA",5,5);
  n=specopt[n].flag;
  if(n&SPECF_LOCKABLE) v_char[y*80+29]='L';
  if(n&SPECF_VARIABLE) v_char[y*80+30]='V';
  if(n&SPECF_SAVE) v_char[y*80+31]='S';
  if(n&SPECF_LOCKED) v_char[y*80+32]=10;
}

void special_option_debug(void) {
  int n;
  win_form("Special option debug") {
    win_list(nspecopt,0,spec_debug_callback,n) {
      win_form("Special option debug") {
        win_numeric('V',"Value: ",specopt[n].value,0,0xFFFF);
        win_command_esc(0,"Set") break;
      }
    }
    win_blank();
    win_command_esc(0,"Done") break;
  }
}
