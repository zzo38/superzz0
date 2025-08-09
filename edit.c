#if 0
gcc $CFLAGS -c -Wno-unused-result -std=gnu99 edit.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

Uint8**screennames;
Uint16 maxscreen;

#define N_GENERAL_PARTS 2
static ASN1_Value general_der;
static ASN1_Value*general_parts;
static Uint32 n_general_oids;
static ASN1_Value*general_oids; // the "class" is used for one of the below constants; when stored in the file the class is always ASN1_UNIVERSAL
#define MANDATORY 16
#define OPTIONAL 17
#define REMOVED 18

void edit_varprop(VarPropertyList*vp) {
  char text[81];
  char name[9];
  Uint8 cur=0;
  int i,j,k,x,y;
  draw0:
  v_ycur=127;
  memset(v_char,0x20,80*25);
  memset(v_color,0x07,80*25);
  memset(v_font,VF_FRONT|VF_SYSTEM,80*25);
  draw_text(0,0," Variable Property List ",0x30,-1);
  draw_text(0,24,"<\x18\x19> Cursor  <INS> Insert  <DEL> Delete  <RET> Edit  <ESC> Done",7,-1);
  draw1:
  draw_text(40,0,text,0x07,snprintf(text,40,"%3d/%3d",cur,vp->count));
  if(cur>15) draw_text(0,1,"\x1EMORE\x1E",10,-1); else draw_text(0,1,"----- ",0,-1);
  if((cur&0xF0)+16<vp->count) draw_text(0,18,"\x1FMORE\x1F",10,-1); else draw_text(0,18,"----- ",0,-1);
  for(i=0;i<16;i++) {
    memset(v_color+80*i+160,0x00,80);
    if((k=i+(cur&0xF0))<vp->count) {
      v_char[80*i+160]=((cur&0x0F)==i?16:250);
      v_color[80*i+160]=((cur&0x0F)==i?14:0);
      switch(j=vp->item[k].type) {
        case 0x00: draw_text(1,i+2,"Once",7,-1); break;
        case 0x04: draw_text(1,i+2,text,7,snprintf(text,80,"Scroll to (%d,%d)",vp->item[k].data[0]|(vp->item[k].data[1]<<8),vp->item[k].data[2]|(vp->item[k].data[3]<<8))); break;
        case 0x11 ... 0x18: draw_text(1,i+2,text,7,snprintf(text,80,"Font: %*.*s",j&15,j&15,vp->item[k].data)); break;
        case 0x1F: draw_text(1,i+2,text,7,snprintf(text,80,"Edit font character %d",vp->item[k].data[0])); break;
        case 0x21 ... 0x28: draw_text(1,i+2,text,7,snprintf(text,80,"Palette: %*.*s",j&15,j&15,vp->item[k].data)); break;
        default: draw_text(1,i+2,"???",12,3);
      }
    } else if(k==vp->count) {
      v_char[80*i+160]=((cur&0x0F)==i?16:0);
      v_color[80*i+160]=((cur&0x0F)==i?14:0);
      draw_text(1,i+2,"<End>",8,-1);
    }
  }
  key:
  redisplay();
  for(;;) {
    if(!next_event()) return;
    if(event.type!=SDL_KEYDOWN) continue;
    switch(event.key.keysym.sym) {
      case SDLK_UP: if(cur) --cur; break;
      case SDLK_DOWN: if(cur<vp->count) ++cur; break;
      case SDLK_HOME: cur=0; break;
      case SDLK_END: cur=vp->count; break;
      case SDLK_PAGEUP: if(cur>16) cur-=16; else cur=0; break;
      case SDLK_PAGEDOWN: j=cur+16; cur=(j<vp->count?j:vp->count); break;
      case SDLK_DELETE:
        if(cur<vp->count) {
          if(cur!=vp->count-1) memmove(vp->item+cur,vp->item+cur+1,(vp->count-1-cur)*sizeof(VarProperty));
          --vp->count;
        }
        break;
      case SDLK_INSERT:
        if(vp->count==255) goto key;
        vp->item=realloc(vp->item,(vp->count+1)*sizeof(VarProperty));
        if(cur<vp->count) memmove(vp->item+cur+1,vp->item+cur,(vp->count-cur)*sizeof(VarProperty));
        memset(vp->item+cur,0,sizeof(VarProperty));
        ++vp->count;
        // fall through
      case SDLK_RETURN:
        if(cur==vp->count) goto key;
        *name=0; x=y=0;
        switch(vp->item[cur].type) {
          case 0x00: i=4; break;
          case 0x04: i=3; x=vp->item[cur].data[0]|(vp->item[cur].data[1]<<8); y=vp->item[cur].data[2]|(vp->item[cur].data[3]<<8); break;
          case 0x11 ... 0x18: i=1; snprintf(name,9,"%s",vp->item[cur].data); break;
          case 0x21 ... 0x28: i=2; snprintf(name,9,"%s",vp->item[cur].data); break;
          default: i=0;
        }
        win_form("Variable Property Edit") {
          win_option('F',"Font",i,1) win_refresh();
          win_option('P',"Palette",i,2) win_refresh();
          win_option('S',"Scroll",i,3) win_refresh();
          win_option('O',"Once",i,4) win_refresh();
          win_blank();
          if(i==1 || i==2) win_text_restrict('u',"Lump name: ",name);
          if(i==3) {
            win_numeric('X',"X: ",x,0,0xFFFF);
            win_numeric('Y',"Y: ",y,0,0xFFFF);
          }
          win_blank();
          win_command_esc(0,"Done") break;
        }
        switch(i) {
          case 1: case 2:
            vp->item[cur].type=(i<<4)+snprintf(vp->item[cur].data,9,"%s",name);
            break;
          case 3:
            vp->item[cur].type=0x04;
            vp->item[cur].data[0]=x; vp->item[cur].data[1]=x>>8;
            vp->item[cur].data[2]=y; vp->item[cur].data[3]=y>>8;
            break;
          case 4: vp->item[cur].type=0x00; break;
        }
        goto draw0;
      case SDLK_ESCAPE: return;
      case SDLK_SLASH: case SDLK_QUESTION: online_help("varprop",0); goto draw0;
      default: continue;
    }
    goto draw1;
  }
}

static void unload_general_der(void) {
  Uint32 n;
  asn1_free(&general_der);
  if(general_parts) for(n=0;n<N_GENERAL_PARTS;n++) if(general_parts[n].own) asn1_free(general_parts+n);
  free(general_parts);
  general_parts=0;
  if(general_oids) for(n=0;n<n_general_oids;n++) if(general_oids[n].own) asn1_free(general_oids+n);
  n_general_oids=0;
  general_oids=0;
}

static void load_general_oids(int class,const ASN1_Value*vo) {
  ASN1_Value v;
  if(!asn1_first_of(&v,vo)) do {
    general_oids=realloc(general_oids,(n_general_oids+1)*sizeof(ASN1_Value));
    if(!general_oids) err(1,"Allocation failed");
    v.class=class;
    general_oids[n_general_oids++]=v;
  } while(!asn1_next_of(&v,vo));
}

static ASN1_Value*find_general_oid(Uint32 type,const Uint8*oid,Uint32 size) {
  //TODO: deal with sequences of OIDs
  Uint32 n;
  for(n=0;n<n_general_oids;n++) if(general_oids[n].type==type && general_oids[n].length==size && !memcmp(oid,general_oids[n].data,size)) return general_oids+n;
  return 0;
}

static ASN1_Value*add_general_oid(Uint8 class,Uint32 type,const Uint8*oid,Uint32 size) {
  ASN1_Value*v=find_general_oid(type,oid,size);
  if(v) {
    if(class==REMOVED || v->class>class) v->class=class;
    return v;
  }
  if(class==REMOVED) return 0;
  general_oids=realloc(general_oids,(n_general_oids+1)*sizeof(ASN1_Value));
  if(!general_oids) err(1,"Allocation failed");
  v=general_oids+n_general_oids++;
  memset(v,0,sizeof(ASN1_Value));
  v->own=1;
  v->length=size;
  v->data=malloc(size);
  if(!v->data) err(1,"Allocation failed");
  memcpy((void*)v->data,oid,size);
  v->class=class;
  v->type=type;
  return v;
}

static void load_general_der(void) {
  ASN1_Value v;
  FILE*f=open_lump("GENERAL.DER","r");
  unload_general_der();
  general_parts=calloc(N_GENERAL_PARTS,sizeof(ASN1_Value));
  if(!general_parts) err(1,"Allocation failed");
  if(!f) return;
  if(asn1_read_item(f,&general_der,0)) {
    alert_text("Error loading GENERAL.DER");
    general_der.data=0;
    general_der.length=0;
  } else if(!asn1_first_of(&v,&general_der)) {
    load_general_oids(MANDATORY,&v);
    if(!asn1_next_of(&v,&general_der)) load_general_oids(OPTIONAL,&v); else errx(1,"Error loading GENERAL.DER");
    while(!asn1_next_of(&v,&general_der)) if(v.class==ASN1_CONTEXT_SPECIFIC && v.type<N_GENERAL_PARTS && !general_parts[v.type].data) general_parts[v.type]=v;
  }
  fclose(f);
}

static void save_general_der(void) {
  Uint32 n;
  ASN1_Value v;
  ASN1_Encoder*e;
  FILE*f=open_lump("GENERAL.DER","w");
  if(!f) errx(1,"Cannot open GENERAL.DER lump for writing");
  e=asn1_create_encoder(f);
  if(!e) errx(1,"Unexpected error");
  asn1_construct(e,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
  asn1_construct(e,ASN1_UNIVERSAL,ASN1_SET,ASN1_SORT);
  for(n=0;n<n_general_oids;n++) if(general_oids[n].class==MANDATORY) {
    general_oids[n].class=ASN1_UNIVERSAL;
    asn1_encode(e,general_oids+n);
    general_oids[n].class=MANDATORY;
  }
  asn1_end(e);
  asn1_construct(e,ASN1_UNIVERSAL,ASN1_SET,ASN1_SORT);
  for(n=0;n<n_general_oids;n++) if(general_oids[n].class==OPTIONAL) {
    general_oids[n].class=ASN1_UNIVERSAL;
    asn1_encode(e,general_oids+n);
    general_oids[n].class=OPTIONAL;
  }
  asn1_end(e);
  n=0;
  if(general_der.data) {
    asn1_first_of(&v,&general_der); asn1_next_of(&v,&general_der); // skip OID sets
    if(!asn1_next_of(&v,&general_der)) do {
      if(v.class!=ASN1_CONTEXT_SPECIFIC || v.type>=N_GENERAL_PARTS) asn1_encode(e,&v);
    } while(!asn1_next_of(&v,&general_der));
  }
  for(n=0;n<N_GENERAL_PARTS;n++) if(general_parts[n].class) asn1_encode(e,general_parts+n);
  asn1_end(e);
  asn1_finish_encoder(e);
  fclose(f);
}

static void write_start_lump(void) {
  int i;
  Uint16 v;
  FILE*fp=open_lump("START","w");
  if(!fp) errx(1,"Cannot open START lump for writing");
  write16(fp,start_mode);
  write16(fp,cur_board_id);
  v=0;
  for(i=0;i<16;i++) if(!status_vars[i]) v|=1<<i;
  write32(fp,v);
  write32(fp,0);
  for(i=0;i<16;i++) if(status_vars[i]) write32(fp,status_vars[i]);
  fclose(fp);
}

static void write_element_lump(void) {
  int i,j,b;
  ElementDef*e;
  FILE*fp=open_lump("ELEMENT","w");
  if(!fp) errx(1,"Cannot open ELEMENT lump for writing");
  fwrite(appearance_mapping,1,128,fp);
  for(i=0;i<4;i++) {
    fputc(animation[i].mode,fp);
    fputc(animation[i].step[0],fp);
    fputc(animation[i].step[1],fp);
    fputc(animation[i].step[2],fp);
    fputc(animation[i].step[3],fp);
  }
  for(i=0;i<256;i++) {
    e=elem_def+i;
    if(e->name[0]) {
      b=strlen(e->name);
      if(e->app[0] && !(e->app[0]==AP_PARAM && !e->app[1])) b|=0x80;
      if(e->app[1]) b|=0x40;
      if(!i || e->attrib!=e[-1].attrib || (e->attrib && !e[-1].name[0])) b|=0x20;
      for(j=0;j<16;j++) if(e->event[j]) b|=0x10;
      fputc(b,fp);
      fwrite(e->name,1,b&15,fp);
      if(b&0x80) fputc(e->app[0],fp);
      if(b&0x40) fputc(e->app[1],fp);
      if(b&0x20) write32(fp,e->attrib);
      if(b&0x10) {
        for(b=j=0;j<16;j++) if(e->event[j]) b|=1<<j;
        write16(fp,b);
        for(j=0;j<16;j++) if(e->event[j]) write16(fp,e->event[j]);
      }
    } else {
      for(j=1;j<16 && i+j<256;j++) if(e[j].name[0]) break;
      fputc((j-1)<<4,fp);
      i+=j-1;
    }
  }
  fclose(fp);
}

void write_name_list(const char*lump,Uint8**data,Uint16 count) {
  FILE*fp=open_lump(lump,"w");
  int i;
  if(!fp) return;
  write16(fp,count);
  for(i=0;i<=count;i++) if(data[i]) fwrite(data[i],1,strlen(data[i])+1,fp); else fputc(0,fp);
  fclose(fp);
}

void combine_assembled(void) {
  FILE*fp;
  char nam[16];
  Uint8 buf[0x1000];
  Uint32 len;
  int c,i;
  if(fp=open_lump("!SZ0","r+")) {
    time_t ti=time(0);
    fread(buf,1,7,fp);
    rewind(fp);
    buf[0]=0x01; buf[1]--;
    buf[3]=ti>>8; buf[4]=ti>>16; buf[5]=ti>>0; buf[6]=ti>>24;
    fwrite(buf,1,7,fp);
    fclose(fp);
  }
  if(fp=open_lump("CATALOG.DER","w")) fclose(fp);
  for(;;) {
    i=0;
    while((c=getchar())>0 && i<15) nam[i++]=(c>='a' && c<='z'?c+'A'-'a':c);
    if(c<0) break;
    nam[i]=0;
    if(c) while((c=getchar())>0);
    fread(buf,1,4,stdin);
    len=(buf[0]<<16)|(buf[1]<<24)|(buf[2]<<0)|(buf[3]<<8);
    if(!strcmp(nam,"MEMORY") || !strcmp(nam,"TEXT") || !strcmp(nam,"MEMORY.ED") || !strcmp(nam,"TEXT.ED")) {
      fp=open_lump(nam,"w");
      if(!fp) errx(1,"Cannot open %s lump for writing",nam);
      while(len) {
        if(len>0x1000) i=0x1000; else i=len;
        fread(buf,1,i,stdin);
        fwrite(buf,1,i,fp);
        len-=i;
      }
      fclose(fp);
    } else if(!strcmp(nam,"EVENT")) {
      if(len!=0x2000) errx(1,"Wrong length of EVENT lump from input");
      for(c=0;c<16;c++) for(i=0;i<256;i++) elem_def[i].event[c]=read16(stdin);
    } else {
      while(len) {
        if(len>0x1000) i=0x1000; else i=len;
        fread(buf,1,i,stdin);
        len-=i;
      }
    }
  }
  write_element_lump();
}

static void write_numform_lump(void) {
  int i;
  FILE*fp=open_lump("NUMFORM","w");
  if(!fp) errx(1,"Cannot open NUMFORM lump for writing");
  for(i=0;i<16;i++) {
    fputc(num_format[i].code,fp);
    fputc(num_format[i].lead,fp);
    fputc(num_format[i].mark,fp);
    fputc(num_format[i].div,fp);
  }
  fclose(fp);
}

static void board_list_callback(Uint16 n,int y,void*uz) {
  FILE*fp=open_lump_by_number(n,"BRD","r");
  char buf[80];
  snprintf(buf,80,"%5d:",n);
  draw_text(1,y,buf,fp?7:8,6);
  v_color[80*y+6]=8;
  if(n<=maxboard && boardnames[n]) draw_text(7,y,boardnames[n],15,61);
  if(fp) {
    n=read16(fp);
    v_color[80*y+77]=v_color[80*y+78]=2;
    v_char[80*y+77]="\xFA\x1A\x18=\x1B\x1D==\x19=\x12"[n&5];
    v_char[80*y+78]="\xFA\x1A\x18=\x1B\x1D==\x19=\x12"[n&10];
    fclose(fp);
  }
}

static void screen_list_callback(Uint16 n,int y,void*uz) {
  char buf[80];
  snprintf(buf,80,"%5d:",n);
  draw_text(1,y,buf,7,6);
  v_color[80*y+6]=8;
  if(n<=maxscreen && screennames[n]) draw_text(7,y,screennames[n],15,61);
}

static void element_list_callback(Uint16 n,int y,void*uz) {
  ElementDef*e=elem_def+n;
  char buf[80];
  char*s=buf;
  Uint32 x;
  snprintf(buf,80,"%3d:",n);
  draw_text(1,y,buf,e->name[0]?7:8,4);
  if(!e->name[0]) return;
  draw_text(5,y,e->name,0x0B,15);
  if(e->app[0]&0x20) {
    v_color[80*y+22]=0x08;
    v_char[80*y+22]='M';
  } else if(e->app[0]&0x0F) {
    v_color[80*y+22]=0x08;
    v_char[80*y+22]="FPOUS123LAC?????"[e->app[0]&0x0F];
  } else {
    v_color[80*y+22]=0x0F;
    v_char[80*y+22]=e->app[1];
  }
  snprintf(buf,2,"%X",e->attrib&15);
  draw_text(24,y,buf,0x20,1);
  v_color[80*y+25]=(e->attrib&A_PUSH_NS?0x0F:0x08);
  v_char[80*y+25]=(e->attrib&A_PUSH_NS?0x12:0xFA);
  v_color[80*y+26]=(e->attrib&A_PUSH_EW?0x0F:0x08);
  v_char[80*y+26]=(e->attrib&A_PUSH_EW?0x1D:0xFA);
  x=e->attrib&(A_OVER_COLOR|A_UNDER_COLOR);
  v_color[80*y+27]=(x?0x0D:0x08);
  v_char[80*y+27]="\xFAous"[x>>6];
  v_color[80*y+28]=(e->attrib&A_CRUSH?0x0C:0x08);
  v_char[80*y+28]=(e->attrib&A_CRUSH?'C':0xFA);
  v_color[80*y+29]=(e->attrib&A_FLOOR?0x09:0x08);
  v_char[80*y+29]=(e->attrib&A_FLOOR?'F':0xFA);
  v_color[80*y+30]=(e->attrib&A_UNDER_BGCOLOR?0x0D:0x08);
  v_char[80*y+30]=(e->attrib&A_UNDER_BGCOLOR?0x81:0xFA);
  v_color[80*y+31]=(e->attrib&A_LIGHT?0x0E:0x08);
  v_char[80*y+31]=(e->attrib&A_LIGHT?0x9D:0xFA);
  v_color[80*y+32]=(e->attrib&A_SENSOR?0x0F:0x08);
  v_char[80*y+32]=(e->attrib&A_SENSOR?'S':0xFA);
  v_color[80*y+33]=(e->attrib&A_TRANSPORTABLE?0x0F:0x08);
  v_char[80*y+33]=(e->attrib&A_TRANSPORTABLE?'t':0xFA);
  v_color[80*y+34]=(e->attrib&A_PERMANENT?0x0F:0x08);
  v_char[80*y+34]=(e->attrib&A_PERMANENT?'P':0xFA);
  for(x=0;x<8;x++) {
    v_color[80*y+x+35]=(e->attrib&(A_MOVE_C0<<x)?0x0A:0x08);
    v_char[80*y+x+35]=(e->attrib&(A_MOVE_C0<<x)?x+'0':0xFA);
    v_color[80*y+x+43]=(e->attrib&(A_MISC_A<<x)?0x06:0x08);
    v_char[80*y+x+43]=(e->attrib&(A_MISC_A<<x)?x+'A':0xFA);
  }
  for(x=0;x<16;x++) {
    v_color[80*y+x+53]=(e->event[x]?0x02:0x08);
    v_char[80*y+x+53]=(e->event[x]?x+(x<8?'A':'S'-8):0xFA);
  }
}

static void edit_element(Uint8 en) {
  ElementDef*e=elem_def+en;
  char title[80];
  Uint8 cla=e->attrib&15;
  Uint8 colo=(e->attrib&(A_OVER_COLOR|A_UNDER_COLOR))>>6;
  snprintf(title,80,"Edit element #%d ($%02X)",en,en);
  win_form(title) {
    win_help("element","at");
    win_text_restrict('m',"Name: ",e->name);
    win_numeric('l',"Class: ",cla,0,15);
    win_boolean('P',"Pushable \x12",e->attrib,A_PUSH_NS);
    win_boolean('u',"Pushable \x1D",e->attrib,A_PUSH_EW);
    win_boolean('r',"Crushable",e->attrib,A_CRUSH);
    win_boolean('o',"Floor",e->attrib,A_FLOOR);
    win_boolean('S',"Sensor",e->attrib,A_SENSOR);
    win_boolean('n',"Transportable",e->attrib,A_TRANSPORTABLE);
    win_boolean('t',"Permanent",e->attrib,A_PERMANENT);
    win_heading("Movement classes:");
    win_boolean('0',"Class 0",e->attrib,A_MOVE_C0);
    win_boolean('1',"Class 1",e->attrib,A_MOVE_C1);
    win_boolean('2',"Class 2",e->attrib,A_MOVE_C2);
    win_boolean('3',"Class 3",e->attrib,A_MOVE_C3);
    win_boolean('4',"Class 4",e->attrib,A_MOVE_C4);
    win_boolean('5',"Class 5",e->attrib,A_MOVE_C5);
    win_boolean('6',"Class 6",e->attrib,A_MOVE_C6);
    win_boolean('7',"Class 7",e->attrib,A_MOVE_C7);
    win_heading("User-defined:");
    win_boolean('A',"Misc. A",e->attrib,A_MISC_A);
    win_boolean('B',"Misc. B",e->attrib,A_MISC_B);
    win_boolean('C',"Misc. C",e->attrib,A_MISC_C);
    win_boolean('D',"Misc. D",e->attrib,A_MISC_D);
    win_boolean('E',"Misc. E",e->attrib,A_MISC_E);
    win_boolean('F',"Misc. F",e->attrib,A_MISC_F);
    win_boolean('G',"Misc. G",e->attrib,A_MISC_G);
    win_boolean('H',"Misc. H",e->attrib,A_MISC_H);
    win_blank();
    win_command('.',"Appearance...") {
      Uint8 m=(e->app[0]&0x20?:e->app[0]&0x1F);
      Uint8 lj=e->app[0]>>6;
      Uint8 ao=((e->app[1]>>4)&7);
      Uint8 ao1=(e->app[1]&0x7F)>>1;
      Uint8 ao2=(e->app[1]&0x7F)>>2;
      Uint8 as=e->app[1]&3;
      Uint8 bs=e->app[0]&7;
      Uint8 bi=((e->app[0]>>3)&3)+1;
      win_form(title) {
        win_help("element","ap");
        win_boolean('B',"Background of under layer",e->attrib,A_UNDER_BGCOLOR);
        win_boolean('k',"Visible in dark",e->attrib,A_LIGHT);
        win_numeric('j',"Line joining class: ",lj,0,3);
        win_heading("Color:");
        win_option('w',"Own color",colo,0);
        win_option('v',"Over layer",colo,1);
        win_option('y',"Under layer",colo,2);
        win_option('c',"Screen data",colo,3);
        win_heading("Character mode:");
        win_option('x',"Fixed",m,AP_FIXED) win_refresh();
        win_option('P',"Parameter (character)",m,AP_PARAM) win_refresh();
        win_option('m',"Parameter (mapped)",m,0x20) win_refresh();
        win_option('A',"Animation (ignore parameter)",m,AP_ANIMATE) win_refresh();
        win_option('L',"Line joining",m,AP_LINES) win_refresh();
        win_option('1',"Misc1",m,AP_MISC1) win_refresh();
        win_option('2',"Misc2",m,AP_MISC2) win_refresh();
        win_option('3',"Misc3",m,AP_MISC3) win_refresh();
        win_option('e',"Over layer",m,AP_OVER) win_refresh();
        win_option('U',"Under layer",m,AP_UNDER) win_refresh();
        win_option('S',"Screen data",m,AP_SCREEN) win_refresh();
        win_option('d',"Counter-based random",m,AP_CBRANDOM) win_refresh();
        win_heading("Character options:");
        switch(m) {
          case AP_FIXED:
            win_char('h',"Character: ",e->app[1]);
            break;
          case AP_PARAM:
            win_char('h',"Base character: ",e->app[1]);
            break;
          case AP_UNDER: case AP_MISC1: case AP_MISC2: case AP_MISC3:
            win_char('h',"Default character: ",e->app[1]);
            break;
          case AP_LINES:
            win_boolean('0',"Join class 0",e->app[1],0x01);
            win_boolean('o',"Join class 1",e->app[1],0x02);
            win_boolean('i',"Join class 2",e->app[1],0x04);
            win_boolean('n',"Join class 3",e->app[1],0x08);
            win_boolean('g',"Join edge",e->app[1],0x80);
            win_numeric('r',"Appearance mapping offset: 16x",ao,0,7);
            break;
          case AP_ANIMATE:
            win_numeric('n',"Animation select: ",as,0,3);
            win_numeric('r',"Appearance mapping offset: 4x",ao2,0,31);
            win_boolean('T',"Time-based",e->app[1],0x80);
            break;
          case AP_CBRANDOM:
            win_numeric('n',"Distribution select: ",as,0,3);
            win_numeric('r',"Appearance mapping offset: 4x",ao2,0,31);
            win_boolean('i',"Alternate distribution",e->app[1],0x80);
            break;
          case 0x20:
            win_numeric('h',"Parameter shift: ",bs,0,7);
            win_numeric('i',"Bits of parameter: ",bi,1,4);
            win_numeric('r',"Appearance mapping offset: 2x",ao1,0,63);
            win_boolean('o',"Enable animation",e->app[1],0x80);
            win_boolean('n',"Use animation 1",e->app[1],0x01);
            break;
          default:
            win_picture(1) draw_text(1,0,"(None)",8,-1);
            break;
        }
        win_blank();
        win_command_esc(0,"Back") break;
      }
      e->app[0]=m|(lj<<6);
      switch(m) {
        case AP_LINES: e->app[1]|=ao<<4; break;
        case AP_ANIMATE: case AP_CBRANDOM: e->app[1]&=0x80; e->app[1]|=as|(ao2<<2); break;
        case 0x20: e->app[0]|=bs|((bi-1)<<3); e->app[1]&=0x81; e->app[1]|=ao1<<1; break;
      }
    }
    win_command_esc(0,"Done") break;
  }
  e->attrib=(e->attrib&~(15|A_OVER_COLOR|A_UNDER_COLOR))|cla|(colo<<6);
}

static char edit_appearance_mapping(void) {
  char c=0;
  int i;
  int n=0;
  char buf[6];
  draw:
  memset(v_char,32,80*25);
  memset(v_color+80,0x07,80*24);
  memset(v_color,0x30,80);
  strcpy(v_char,"Appearance mapping");
  for(i=0;i<128;i++) {
    snprintf(buf,6,"%3d:",i);
    draw_text((i>>4)*9+2,(i&15)+2,buf,0x07,4);
  }
  draw_text(2,19,"<ESC> Done   <SPACE> Edit   <?> Help",7,-1);
  key:
  for(i=0;i<128;i++) {
    v_char[(i>>4)*9+(i&15)*80+161]=(n==i?0x10:0xFA);
    v_color[(i>>4)*9+(i&15)*80+161]=(n==i?0x0E:0x08);
    v_char[(i>>4)*9+(i&15)*80+166]=appearance_mapping[i];
    v_color[(i>>4)*9+(i&15)*80+166]=(n==i?0x2F:0x1F);
  }
  redisplay();
  for(;;) {
    if(!next_event()) return 0;
    if(event.type==SDL_KEYDOWN) switch(event.key.keysym.sym) {
      case SDLK_HOME: n=0; goto key;
      case SDLK_END: n=127; goto key;
      case SDLK_UP: n=(n-1)&127; goto key;
      case SDLK_DOWN: n=(n+1)&127; goto key;
      case SDLK_LEFT: n=(n-16)&127; goto key;
      case SDLK_RIGHT: n=(n+16)&127; goto key;
      case SDLK_SPACE: case SDLK_RETURN: c=1; appearance_mapping[n]=ask_color_char(1,appearance_mapping[n]); goto draw;
      case SDLK_ESCAPE: return c;
      case SDLK_SLASH: case SDLK_QUESTION: online_help("appmap",0); goto draw;
    }
  }
}

static void edit_one_help_lump(const char*name) {
  FILE*f=open_lump(name,"r");
  Uint8*t=0;
  if(f) {
    t=malloc(lump_size+1);
    if(!t) err(1,"Allocation failed");
    fread(t,1,lump_size,f);
    t[lump_size]=0;
    fclose(f);
  }
  t=text_editor(t);
  f=open_lump(name,"w");
  if(!f) errx(1,"Unexpected error");
  if(t || strcmp(name,"GLOBAL")) fputs(t?:(Uint8*)"\n",f);
  fclose(f);
}

static void lump_listing_menu(const char*fname,const char*text0,void(*call0)(const char*),const char*helpfile,const char*helptopic) {
  // This function expects that fname must be "*." and then exactly three letters.
  char buf[40];
  char name[9];
  FILE*f;
  const char**list=0;
  int cur,scr,i,j,n,count,xc;
  start:
  cur=scr=xc=0;
  free(list);
  list_lumps(fname,&list,&count);
  draw0:
  memset(v_char,32,80*25);
  memset(v_color+80,0x07,80*24);
  memset(v_color,0x30,80);
  strcpy(v_char,text0);
  memset(v_color+24*80,0x30,80);
  strcpy(v_char+24*80+2,"<RET> Edit  <INS> Add  <DEL> Delete  <ESC> Done");
  strcpy(v_char+24*80+72,fname+2);
  draw1:
  while(cur<scr*23) --scr;
  while(cur>=scr*23+115) ++scr;
  for(i=0;i<115;i++) {
    n=i+scr*23;
    if(n<count) {
      draw_text(14*(i/23)+3,i%23+1,"        ",n==cur?0x2E:0x0E,8);
      draw_text(14*(i/23)+3,i%23+1,list[n],n==cur?0x2E:0x0E,strlen(list[n])-4);
    } else {
      draw_text(14*(i/23)+3,i%23+1,"--------",n==cur?0x28:0x08,8);
    }
    j=14*(i/23)+80*(i%23)+82;
    if(n==cur) {
      v_char[j]=0x10; v_color[j]=0x0F;
      if(xc) memset(v_color+j+1,0x6F,xc);
    } else {
      v_char[j]=0xFA; v_color[j]=0x08;
    }
  }
  draw_text(52,24,buf,0x3B,snprintf(buf,20,"%5d/%5d",cur+1,count));
  redisplay();
  for(;;) {
    if(!next_event()) return;
    if(event.type==SDL_KEYDOWN) switch(event.key.keysym.sym) {
      case SDLK_ESCAPE: free(list); win_refresh(); return;
      case SDLK_HOME: cur=scr=xc=0; goto draw0;
      case SDLK_END: scr=xc=0; if(count) cur=count-1; goto draw0;
      case SDLK_UP: xc=0; if(cur) --cur; goto draw1;
      case SDLK_DOWN: xc=0; if(cur+1<count) ++cur; goto draw1;
      case SDLK_LEFT: xc=0; cur-=23; if(cur<0) cur=0; goto draw1;
      case SDLK_RIGHT: xc=0; cur+=23; if(cur>=count) cur=(count?count-1:0); goto draw1;
      case SDLK_PAGEUP: xc=0; scr-=115; if(scr<0) scr=0; cur-=115; if(cur<0) cur=0; goto draw1;
      case SDLK_PAGEDOWN: xc=0; scr+=115; cur+=115; if(cur>=count) cur=(count?count-1:0); goto draw1;
      case SDLK_RETURN: xc=0; if(cur<count) call0(list[cur]); goto draw0;
      case SDLK_INSERT:
        if(count>10000) {
          alert_text("Too many lumps");
        } else {
          *name=0;
          ask_text_restrict("Add new lump:",name,8);
          if(*name && snprintf(buf,16,"%s%s",name,fname+1)) call0(buf);
        }
        goto start;
      case SDLK_DELETE:
        if(cur>=count) break;
        snprintf(buf,40,"Delete %s?",list[cur]);
        if(ask_yn(buf,0)) {
          if(f=open_lump(list[cur],"w")) fclose(f);
          goto start;
        }
        goto draw0;
      case SDLK_SLASH: case SDLK_QUESTION: online_help(helpfile,helptopic); goto draw0;
      default:
        i=event.key.keysym.unicode;
        if(i==8 && xc) {
          --xc;
          while(cur && !memcmp(list[cur-1],list[cur],xc)) --cur;
          goto draw1;
        }
        if(i>='a' && i<='z') i+='A'-'a';
        if(xc<8 && i!='.' && i>32 && i<127) for(n=(xc?cur:0);n<count && !memcmp(list[n],list[cur],xc);n++) {
          if(list[n][xc]==i) {
            ++xc;
            cur=n;
            goto draw1;
          }
        }
    }
  }
}

typedef struct {
  Uint8 text[24];
  Uint16 data[16];
} JoystickConfig;

static void edit_joystick(void) {
  static const char*const js[]={
    "0Toggle pause",
    "1Display F1 menu",
    "2Toggle sound",
    "5Quick save",
    "7Quick restore",
    "9Message scrollback",
    "<Previous page",
    ">Next page",
    "AAdvance frame",
    "FFast speed while held",
    "TToggle speed",
    "XClear message",
    "ZCancel window",
    "[Top of window",
    "]Bottom of window",
  };
  JoystickConfig jc[32]={};
  JoystickConfig jc0;
  Uint8 q[64];
  ASN1_Encoder*enc;
  ASN1_Value u,v;
  int i,j,k,x,y,z;
  Uint8 xc=0;
  Uint8 yc=0;
  if(!general_parts) errx(1,"Internal confusion in edit_joystick function");
  if(general_parts[0].class && !asn1_first_of(&v,general_parts+0)) for(i=0;i<32;i++) {
    if(asn1_first_of(&u,&v) || u.class || u.type!=ASN1_PC_STRING || u.length>23) goto error;
    memcpy(jc[i].text,u.data,u.length);
    if(asn1_next_of(&u,&v) || u.class || u.type!=ASN1_OCTET_STRING) goto error;
    for(j=x=y=0;j<u.length && x<16;j++) switch(u.data[j]) {
      case 0 ... 3: x+=u.data[j]+1; break;
      case 7: jc[i].data[x++]=y; if(x<16)
      case 6: jc[i].data[x++]=y; if(x<16)
      case 5: jc[i].data[x++]=y; if(x<16)
      case 4: jc[i].data[x++]=y; if(x<16)
      /*   */ jc[i].data[x++]=y; break;
      case 8 ... 9: case 13: case 16 ... 17: case 24 ... 27: case 30 ... 126:
      case 8+128 ... 9+128: case 13+128: case 16+128 ... 17+128: case 24+128 ... 27+128: case 30+128 ... 126+128:
        jc[i].data[x++]=y=u.data[j]; break;
      case 12: if(j+1<u.length && x<16) jc[i].data[x++]=y=u.data[++j]+0x100; //
      case 11: if(j+1<u.length && x<16) jc[i].data[x++]=y=u.data[++j]+0x100; //
      case 10: if(j+1<u.length && x<16) jc[i].data[x++]=y=u.data[++j]+0x100; break;
      case 128 ... 135: jc[i].data[x++]=u.data[j]+0x181; break;
      default: alert_text("The joystick configuration contains unusable data; it will be discarded."); x=16;
    }
    if(asn1_next_of(&u,&v)!=ASN1_DONE) alert_text("The joystick configuration contains unusable data; it will be discarded.");
    j=asn1_next_of(&v,general_parts+0);
    if(j) {
      if(j!=ASN1_DONE) error: alert_text("Error reading joystick configuration from GENERAL.DER");
      break;
    }
  }
  redraw0:
  memset(v_color,0,80*25);
  memset(v_char,0,80*25);
  draw_text(0,0,"Joystick Edit",0x30,-1);
  memset(v_color,0x30,80);
  draw_text(16,2,"[00][01][02][03][04][05][06][07][08][09][10][11][12][13][14][15]",0x07,-1);
  redraw1:
  draw_text(0,1,"Page 1/2",0x0F,-1);
  if(yc&16) v_char[85]++;
  memset(v_char+3*80,0,16*80);
  memset(v_color+3*80,0,16*80);
  for(y=0;y<16;y++) {
    draw_text(0,y+3,jc[y+(yc&16)].text,0,16);
    memset(v_color+(y+3)*80,(y==(yc&15)&&!xc?0x2B:0x0B),16);
    for(x=0;x<16;x++) {
      z=(y+3)*80+x*4+16;
      i=jc[y+(yc&16)].data[x];
      if(!i) {
        v_color[z+1]=0x04; v_char[z+1]=0xFA;
      } else if(i<0x100) {
        switch(i&0x7F) {
          case 8: draw_text(x*4+16,y+3,"BS",0x0A,2); break;
          case 9: draw_text(x*4+16,y+3,"TAB",0x0A,3); break;
          case 13: draw_text(x*4+16,y+3,"RET",0x0A,3); break;
          case 32: draw_text(x*4+16,y+3,"SP",0x0A,2); break;
          default: v_color[z+1]=0x0A; v_char[z+1]=i&0x7F;
        }
        if(i&0x80) v_color[z+3]=0x0C,v_char[z+3]='!';
      } else if(i<0x200) {
        v_color[z+1]=v_color[z+2]=0x0E;
        v_char[z+1]='^'; v_char[z+2]=i&0x7F;
      } else if(i<0x300) {
        v_color[z+1]=v_color[z+2]=0x0D;
        v_char[z+1]='#'; v_char[z+2]=i+'0';
      }
      if(xc==x+1 && (yc&15)==y) {
        v_color[z]|=0x20; v_color[z+1]|=0x20; v_color[z+2]|=0x20; v_color[z+3]|=0x20;
      }
    }
  }
  draw_text(0,20,"<ESC> Done  <TAB> Page  <\x18\x19\x1A\x1B> Move Cursor  <SHIFT+\x1A\x1B> Copy",0x07,-1);
  draw_text(0,21,xc?"<N> Normal  <C> Command  <1-8> Shift  <X> Nothing  <A> Auto-fire":"<SPACE> Edit  <DEL> Erase Line  <SHIFT+\x18\x19> Exchange             ",0x07,-1);
  redisplay();
  input:
  if(!next_event()) return;
  if(event.type!=SDL_KEYDOWN) goto input;
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: goto stop;
    case SDLK_TAB: yc^=16; goto redraw1;
    case SDLK_UP:
      if(!xc && (event.key.keysym.mod&KMOD_SHIFT)) jc0=jc[(yc-1)&31],jc[(yc-1)&31]=jc[yc],jc[yc]=jc0;
      yc=(yc-1)&31; goto redraw1;
    case SDLK_DOWN:
      if(!xc && (event.key.keysym.mod&KMOD_SHIFT)) jc0=jc[(yc+1)&31],jc[(yc+1)&31]=jc[yc],jc[yc]=jc0;
      yc=(yc+1)&31; goto redraw1;
    case SDLK_LEFT:
      if(xc) {
        if(xc>1 && (event.key.keysym.mod&KMOD_SHIFT)) jc[yc].data[xc-2]=jc[yc].data[xc-1];
        --xc;
      } goto redraw1;
    case SDLK_RIGHT:
      if(xc<16) {
        if(xc && (event.key.keysym.mod&KMOD_SHIFT)) jc[yc].data[xc]=jc[yc].data[xc-1];
        ++xc;
      } goto redraw1;
    case SDLK_SPACE:
      if(!xc) ask_text("Button names:",jc[yc].text,23);
      goto redraw0;
    case SDLK_a:
      if(xc && jc[yc].data[xc-1]<0x100 && jc[yc].data[xc-1]>0) jc[yc].data[xc-1]^=0x80;
      goto redraw1;
    case SDLK_c:
      if(!xc) break;
      draw_border(0x1F,20,4,61,21);
      draw_text(22,5,"Select command to assign:",0x1F,-1);
      for(i=0;i<sizeof(js)/sizeof(*js);i++) {
        memset(v_color+(i+6)*80+22,i&1?0x70:0x30,3);
        v_char[(i+6)*80+23]=js[i][0];
        draw_text(26,i+6,js[i]+1,0x1E,-1);
      }
      redisplay();
      while(next_event() && event.type!=SDL_KEYDOWN);
      i=event.key.keysym.unicode;
      if(i>='a' && i<='z') i+='A'-'a';
      if(i>32 && i<127) jc[yc].data[xc-1]=i+0x100;
      goto redraw0;
    case SDLK_n:
      if(!xc) break;
      draw_border(0x1F,30,11,51,13);
      draw_text(32,12,"Push key to assign",0x1F,-1);
      redisplay();
      while(next_event() && event.type!=SDL_KEYDOWN);
      i=event.key.keysym.unicode;
      if((i>=32 && i<127) || i==8 || i==9 || i==13) {
        jc[yc].data[xc-1]=i;
      } else if(event.key.keysym.sym==SDLK_UP) {
        jc[yc].data[xc-1]=(event.key.keysym.mod&KMOD_SHIFT)?30:24;
      } else if(event.key.keysym.sym==SDLK_DOWN) {
        jc[yc].data[xc-1]=(event.key.keysym.mod&KMOD_SHIFT)?31:25;
      } else if(event.key.keysym.sym==SDLK_LEFT) {
        jc[yc].data[xc-1]=(event.key.keysym.mod&KMOD_SHIFT)?17:27;
      } else if(event.key.keysym.sym==SDLK_RIGHT) {
        jc[yc].data[xc-1]=(event.key.keysym.mod&KMOD_SHIFT)?16:26;
      }
      goto redraw0;
    case SDLK_x:
      if(xc) jc[yc].data[xc-1]=0;
      goto redraw1;
    case SDLK_1 ... SDLK_8:
      if(xc) jc[yc].data[xc-1]=event.key.keysym.sym+0x200-SDLK_0;
      goto redraw1;
    case SDLK_DELETE:
      if(xc) break;
      jc[yc].text[0]=0;
      for(i=0;i<16;i++) jc[yc].data[i]=0;
      goto redraw1;
    case SDLK_SLASH: case SDLK_QUESTION:
      online_help("editjoy",0);
      goto redraw0;
  }
  goto input;
  stop:
  win_refresh();
  // Encode the configuration as DER format
  asn1_free(general_parts+0);
  enc=asn1_start_encoding_constructed_value(general_parts+0,ASN1_CONTEXT_SPECIFIC,0,0);
  if(!enc) err(1,"Allocation failed");
  for(y=0;y<32;y++) if(jc[y].text[0]) {
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    asn1_encode_c_string(enc,ASN1_PC_STRING,jc[y].text);
    for(x=z=j=0;x<16;) {
      if(z && jc[y].data[x]==z && x!=15 && jc[y].data[x+1]==z) {
        for(k=2;k<6 && x+k<16 && !jc[y].data[x+k];k++);
        if(k==6) q[j++]=5,q[j++]=5; else q[j++]=k+2;
        x+=k;
      } else if(jc[y].data[x]) {
        z=jc[y].data[x++];
        if(z<0x100) {
          q[j++]=z;
        } else if(z<0x200) {
          if(x<14 && (jc[y].data[x]&jc[y].data[x+1]&0x100)) {
            q[j++]=12; q[j++]=z; q[j++]=z=jc[y].data[++x]; q[j++]=z=jc[y].data[++x];
          } else if(x<15 && (jc[y].data[x]&0x100)) {
            q[j++]=11; q[j++]=z; q[j++]=z=jc[y].data[++x];
          } else {
            q[j++]=10; q[j++]=z;
          }
        } else {
          q[j++]=z-0x181;
        }
      } else {
        for(k=x;k<16 && !jc[y].data[k];k++);
        if(k==16) break;
        for(k=1;k<4 && x+k<16 && !jc[y].data[x+k];k++);
        q[j++]=k-1; x+=k;
      }
    }
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,q,j);
    asn1_end(enc);
  }
  asn1_finish_encoder(enc);
}

static void oid_sets_callback(Uint16 n,int y,void*f) {
  char b[78];
  int c=general_oids[n].class;
  int e;
  rewind(f);
  if(e=asn1_print_decimal_oid(general_oids+n,general_oids[n].type,f)) fputc(e,f);
  fprintf(f,"%72s","");
  fflush(f);
  rewind(f);
  fread(b,1,72,f);
  draw_text(1,y,b,(c==MANDATORY?14:c==OPTIONAL?11:8),72);
}

static void edit_oid_sets(void) {
  int n=0;
  int e;
  char b[75]={};
  FILE*f=fmemopen(b,72,"r+b");
  win_form("OID sets") {
    win_help("editadv","oid");
    win_list(n_general_oids,f,oid_sets_callback,n) {
      rewind(f);
      if(e=asn1_print_decimal_oid(general_oids+n,general_oids[n].type,f)) fputc('?',f);
      fputc(0,f);
      fflush(f);
      win_form("Edit OID set item") {
        win_heading(b);
        win_option('M',"Mandatory",general_oids[n].class,MANDATORY);
        win_option('O',"Optional",general_oids[n].class,OPTIONAL);
        win_option('R',"Removed",general_oids[n].class,REMOVED);
        win_blank();
        win_command_esc(0,"Done") break;
      }
    }
    win_blank();
    win_command('A',"Add") {
      b[1]=0;
      ask_text("New OID:",b+1,72);
      if(b[1]) {
        ASN1_Value v={};
        Uint8 z[128];
        if(b[1]=='.' && b[2]=='.' && b[3]=='.') {
          b[0]=b[2]='0';
          e=asn1_make_static_oid(b,z,128,&v);
          v.data=z+1;
          v.length--;
          v.type=ASN1_RELATIVE_OID;
        } else if(!strncmp("2.25.196954517921581521497869385664052876310.",b+1,45)) {
          b[42]='0'; b[43]='.';
          e=asn1_make_static_oid(b+42,z,128,&v);
          v.data=z+1;
          v.length--;
          v.type=ASN1_RELATIVE_OID;
        } else {
          e=asn1_make_static_oid(b+1,z,128,&v);
        }
        if(!e) {
          if((e=ask_yn("Mandatory?",-1))>=0) add_general_oid(e?MANDATORY:OPTIONAL,v.type,v.data,v.length);
        } else {
          alert_text("Invalid OID");
        }
      }
    }
    win_command('D',"Delete all") if(ask_yn("Delete all OIDs?",0)) {
      if(general_oids) for(n=0;n<n_general_oids;n++) if(general_oids[n].own) asn1_free(general_oids+n);
      n_general_oids=0;
      general_oids=0;
    }
    win_blank();
    win_command_esc(0,"Exit") break;
  }
  fclose(f);
}

static int copy_board(Uint16 inb,Uint16 outb) {
  FILE*in=open_lump_by_number(inb,"BRD","r");
  Uint32 s=lump_size;
  FILE*out=open_lump_by_number(outb,"BRD","wx");
  if(!in || !out) {
    if(in) fclose(in);
    if(out) fclose(out);
    alert_text("Cannot copy board");
    return 0;
  }
  copy_stream(in,out,s);
  fclose(in);
  fclose(out);
  return 1;
}

#define M(N) if(rec && nmacro<128) macro[nmacro++]=N; M##N:
#define MM(N) case N: goto M##N;
static void edit_font(const char*name) {
  char adv=(name[strlen(name)-1]=='T');
  FILE*f=open_lump(name,"r");
  FILE*g;
  Uint8*t=0;
  char b[70]={};
  int i=0;
  int j,k;
  Uint8 x,y;
  Uint8 cch=0;
  char mode=0;
  char draw=0;
  Uint8 batch[256/8]={};
  Uint8 batch2[256/8];
  Uint8 macro[128];
  Uint8 nmacro=0;
  char rec=0;
  Uint8 play=255;
  static Uint8 clip[14];
  Sint16 nclip=-1;
  char bplay=0;
  // Add new font if necessary
  if(f && lump_size) {
    fclose(f);
  } else {
    if(f) fclose(f);
    win_form(adv?"Add new font (advanced)":"Add new font (simple)") {
      win_help("editgr","chr");
      win_heading("Initialize new font:");
      win_option('B',"Blank",i,0) win_refresh();
      win_option('P',"PC",i,1) win_refresh();
      win_option('A',"ASCII",i,2) win_refresh();
      win_option('I',"Import",i,3) win_refresh();
      if(font) win_option('C',"Current",i,4) win_refresh();
      if(i==3) win_text('F',"File: ",b) win_refresh();
      win_blank();
      if(i!=3 || *b) win_command('O',"OK") break;
      win_command_esc(0,"Cancel") return;
    }
    f=open_lump(name,"w");
    if(!f) errx(1,"Unexpected error when opening simple font for writing");
    switch(i) {
      case 0:
        for(i=0;i<3584;i++) fputc(0,f);
        break;
      case 1:
        fwrite(pcfont,14,256,f);
        break;
      case 2:
        for(i=0;i<0x20*14;i++) fputc(0,f);
        fwrite(pcfont+0x20*14,14,0x7F-0x20,f);
        for(i=0x7F*14;i<0x100*14;i++) fputc(0,f);
        break;
      case 3:
        if(g=(*b=='|')?popen(b+1,"r"):fopen(b,"r")) {
          copy_stream(g,f,14*256);
          if(*b=='|') pclose(g); else fclose(g);
        } else {
          warn("Error importing font");
        }
        break;
      case 4:
        fwrite(font,14,256,f);
        break;
    }
    fclose(f);
  }
  if(!load_font(name,LOADFONT_BASE)) {
    alert_text("Cannot load");
    return;
  }
  // Font editing
  x=y=0;
  v_status[1]='c';
  draw0:
  memset(v_font,VF_SYSTEM|VF_FRONT,80*25);
  memset(v_color,0x07,80*25);
  memset(v_char,0x20,80*25);
  draw_text(0,0,name,0x0E,-1);
  draw_border(0x0A,0,1,17,18);
  for(i=0;i<256;i++) {
    v_font[(i>>4)*80+(i&15)+161]=0;
    v_char[(i>>4)*80+(i&15)+161]=i;
  }
  if(!mode) draw_border(0x0A,41,1,58,16);
  if(mode<2) draw_text(19,1,"Character code:",0x0F,-1);
  if(mode==0) {
    draw_text(0,19,"<\x18\x19\x1A\x1B> Cursor",7,-1);
    draw_text(0,20,"<SHIFT+\x18\x19\x1A\x1B> Shift",7,-1);
    draw_text(0,21,"<SPACE> Plot",7,-1);
    draw_text(0,22,"<TAB> Draw",7,-1);
    draw_text(20,19,"<I> Inverse",7,-1);
    draw_text(20,20,"<F> Flip",7,-1);
    draw_text(20,21,"<M> Mirror",7,-1);
    draw_text(20,22,"<DEL> Erase",7,-1);
    draw_text(20,23,"<INS> PC",7,-1);
    draw_text(40,18,"<Y> Memory",7,-1);
    draw_text(40,19,"<1-9> Pattern",7,-1);
    draw_text(40,20,"<Z> Exchange",7,-1);
    draw_text(40,21,"<P> Replace",7,-1);
    draw_text(40,22,"<A> AND",7,-1);
    draw_text(40,23,"<O> OR",7,-1);
    draw_text(40,24,"<X> XOR",7,-1);
    draw_text(60,18,"<ALT+\x18\x19\x1A\x1B> Select",7,-1);
    draw_text(60,19,"<G> Go to",7,-1);
    draw_text(60,20,"<B> Batch",7,-1);
    draw_text(60,23,"<F1> Record",7,-1);
    draw_text(60,24,"<F2> Play",7,-1);
  } else if(mode==1) {
    draw_text(0,19,"<\x18\x19\x1A\x1B> Cursor",7,-1);
    draw_text(0,20,"<CTRL+\x18\x19\x1A\x1B> Cur+Mrk",7,-1);
    draw_text(0,21,"<SPACE> Mark/Unmark",7,-1);
    draw_text(0,22,"<N> Invert Mark",7,-1);
    draw_text(0,23,"<U> Unmark All",7,-1);
    draw_text(20,19,"<I> Inverse",7,-1);
    draw_text(20,20,"<F> Flip",7,-1);
    draw_text(20,21,"<M> Mirror",7,-1);
    draw_text(20,22,"<DEL> Erase",7,-1);
    draw_text(20,23,"<INS> PC",7,-1);
    draw_text(40,21,"<P> Replace",7,-1);
    draw_text(40,22,"<A> AND",7,-1);
    draw_text(40,23,"<O> OR",7,-1);
    draw_text(40,24,"<X> XOR",7,-1);
    draw_text(60,19,"<RETURN> Edit Char",7,-1);
    draw_text(60,24,"<F2> Play Marked",7,-1);
  }
  draw1:
  if(play<nmacro) {
    switch(macro[play++]) {
      MM(1)MM(2)MM(3)MM(4)MM(5)MM(6)MM(7)MM(8)MM(9)MM(10)MM(11)MM(12)MM(13)MM(14)MM(15)
      MM(16)MM(17)MM(18)MM(19)MM(20)MM(21)MM(22)MM(23)MM(24)MM(25)
      case 49 ... 58: event.key.keysym.sym=macro[play-1]; goto M49;
    }
  } else if(bplay) {
    bplay=0;
    for(k=cch+1;k<256;k++) if(batch[k>>3]&(1<<(k&7))) {
      cch=k,bplay=1,play=0;
      batch[k>>3]&=~(1<<(k&7));
      goto draw1;
    }
  }
  if(mode<2) {
    draw_text(20,2,b,0x0B,snprintf(b,30,"%3d",cch));
    draw_text(20,3,b,0x0B,snprintf(b,30,"$%02X",cch));
  }
  if(mode==1) draw_text(20,4,"*Batch*",0x0C,7); else draw_text(20,4,"*Draw*",draw?0x0C:0x00,6);
  draw_text(19,6,"\x07Record\x07",rec?4:0,8);
  draw_text(27,6,b,2,snprintf(b,4,"%3d",nmacro));
  if(!mode) {
    for(i=0;i<256;i++) v_color[(i>>4)*80+(i&15)+161]=(i==cch?0x1E:0x07);
  } else if(mode==1) {
    for(i=0;i<256;i++) v_color[(i>>4)*80+(i&15)+161]=(i==cch?0x1E:0x06)+(batch[i>>3]&(1<<(i&7))?0x21:0x00);
  }
  if(!mode) for(i=0;i<14;i++) for(j=0;j<8;j++) {
    v_color[i*80+202+j*2]=v_color[i*80+203+j*2]=(x==j && y==i)?0x1B:0x07;
    v_char[i*80+202+j*2]=v_char[i*80+203+j*2]=(font[cch*14+i]&(128>>j))?177:250;
  }
  redisplay();
  input:
  if(!next_event()) return;
  if(event.type!=SDL_KEYDOWN) goto input;
  if(mode==1) goto mode1;
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: goto exit;
    case SDLK_F12: goto draw0;
    case SDLK_SPACE: M(1); font[cch*14+y]^=128>>x; break;
    case SDLK_LEFT: case SDLK_KP4: case SDLK_h:
      if(event.key.keysym.mod&KMOD_ALT) {
        M(2); cch--;
      } else if(event.key.keysym.mod&KMOD_SHIFT) {
        M(3); for(i=0;i<14;i++) font[14*cch+i]=(font[14*cch+i]<<1)|(font[14*cch+i]>>7);
      } else {
        M(4); x=(x-1)&7;
      }
      break;
    case SDLK_RIGHT: case SDLK_KP6: case SDLK_l:
      if(event.key.keysym.mod&KMOD_ALT) {
        M(5); cch++;
      } else if(event.key.keysym.mod&KMOD_SHIFT) {
        M(6); for(i=0;i<14;i++) font[14*cch+i]=(font[14*cch+i]<<7)|(font[14*cch+i]>>1);
      } else {
        M(7); x=(x+1)&7;
      }
      break;
    case SDLK_UP: case SDLK_KP8: case SDLK_k:
      if(event.key.keysym.mod&KMOD_ALT) {
        M(8); cch-=16;
      } else if(event.key.keysym.mod&KMOD_SHIFT) {
        M(9); i=font[14*cch];
        memmove(font+14*cch,font+14*cch+1,13);
        font[14*cch+13]=i;
      } else {
        M(10); y=(y+13)%14;
      }
      break;
    case SDLK_DOWN: case SDLK_KP2: case SDLK_j:
      if(event.key.keysym.mod&KMOD_ALT) {
        M(11); cch+=16;
      } else if(event.key.keysym.mod&KMOD_SHIFT) {
        M(12); i=font[14*cch+13];
        memmove(font+14*cch+1,font+14*cch,13);
        font[14*cch]=i;
      } else {
        M(13); y=(y+1)%14;
      }
      break;
    case SDLK_DELETE: case SDLK_BACKSPACE: M(14); memset(font+14*cch,0,14); break;
    case SDLK_INSERT: M(15); memcpy(font+14*cch,pcfont+14*cch,14); break;
    case SDLK_TAB: M(16); draw^=1; break;
    case SDLK_a: M(17); for(i=0;i<14;i++) font[14*cch+i]&=clip[i]; break;
    case SDLK_b: mode=1; rec=draw=0; goto draw0;
    case SDLK_f: M(18); for(i=0;i<7;i++) j=font[14*cch+i],font[14*cch+i]=font[14*cch+13-i],font[14*cch+13-i]=j; break;
    case SDLK_g:
      b[0]=b[1]=b[2]=0;
      ask_text("Go to:",b,2);
      if(*b) cch=(b[1]?strtol(b,0,16):*b);
      goto draw0;
    case SDLK_i: M(19); for(i=0;i<14;i++) font[14*cch+i]^=-1; break;
    case SDLK_m: M(20); for(i=0;i<14;i++) font[14*cch+i]=((font[14*cch+i]*0x0202020202ULL)&0x010884422010ULL)%0x3FF; break;
    case SDLK_o: M(21); for(i=0;i<14;i++) font[14*cch+i]|=clip[i]; break;
    case SDLK_p: M(22); memcpy(font+14*cch,clip,14); break;
    case SDLK_x: M(23); for(i=0;i<14;i++) font[14*cch+i]^=clip[i]; break;
    case SDLK_y: M(24); memcpy(clip,font+14*cch,14); nclip=cch; break;
    case SDLK_z: M(25); if((nclip&~255) || nclip==cch) break; memcpy(b,font+14*nclip,14); memcpy(font+14*nclip,font+14*cch,14); memcpy(font+14*cch,b,14); break;
    case SDLK_1 ... SDLK_9:
      if(rec && nmacro<128) macro[nmacro++]=event.key.keysym.sym; M49:
      memcpy(clip,pcfont+14*(Uint8)"\xB0\xB1\xB2\xDB\xDC\xDD\xDE\xDF\xFE"[event.key.keysym.sym-SDLK_1],14); nclip=-1;
      break;
    case SDLK_F1: if(rec) rec=0; else rec=1,nmacro=0,play=255; break;
    case SDLK_F2: rec=play=0; break;
    default:
      if(event.key.keysym.unicode==27) goto exit;
      goto input;
  }
  if(draw) font[cch*14+y]|=128>>x;
  goto draw1;
  mode1:
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: goto exit;
    case SDLK_F12: goto draw0;
    case SDLK_RETURN: mode=0; goto draw0;
    case SDLK_SPACE: batch[cch>>3]^=1<<(cch&7); break;
    case SDLK_LEFT: case SDLK_KP4: case SDLK_h:
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      cch--;
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      break;
    case SDLK_RIGHT: case SDLK_KP6: case SDLK_l:
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      cch++;
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      break;
    case SDLK_UP: case SDLK_KP8: case SDLK_k:
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      cch-=16;
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      break;
    case SDLK_DOWN: case SDLK_KP2: case SDLK_j:
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      cch+=16;
      if(event.key.keysym.mod&KMOD_CTRL) batch[cch>>3]|=1<<(cch&7);
      break;
    case SDLK_DELETE: case SDLK_BACKSPACE: for(i=0;i<256;i++) if(batch[i>>3]&(1<<(i&7))) memset(font+14*i,0,14); break;
    case SDLK_INSERT: for(i=0;i<256;i++) if(batch[i>>3]&(1<<(i&7))) memcpy(font+14*i,pcfont+14*i,14); break;
    case SDLK_a: for(k=0;k<256;k++) if(batch[k>>3]&(1<<(k&7))) for(i=0;i<14;i++) font[14*k+i]&=clip[i]; break;
    case SDLK_b: mode=0; goto draw0;
    case SDLK_f: for(k=0;k<256;k++) if(batch[k>>3]&(1<<(k&7))) for(i=0;i<7;i++) j=font[14*k+i],font[14*k+i]=font[14*k+13-i],font[14*k+13-i]=j; break;
    case SDLK_i: for(k=0;k<256;k++) if(batch[k>>3]&(1<<(k&7))) for(i=0;i<14;i++) font[14*k+i]^=-1; break;
    case SDLK_m: for(k=0;k<256;k++) if(batch[k>>3]&(1<<(k&7))) for(i=0;i<14;i++) font[14*k+i]=((font[14*k+i]*0x0202020202ULL)&0x010884422010ULL)%0x3FF; break;
    case SDLK_n: for(i=0;i<256/8;i++) batch[i]^=-1; break;
    case SDLK_o: for(k=0;k<256;k++) if(batch[k>>3]&(1<<(k&7))) for(i=0;i<14;i++) font[14*k+i]|=clip[i]; break;
    case SDLK_p: for(k=0;k<256;k++) if(batch[k>>3]&(1<<(k&7))) memcpy(font+14*k,clip,14); break;
    case SDLK_u: memset(batch,0,256/8); break;
    case SDLK_x: for(k=0;k<256;k++) if(batch[k>>3]&(1<<(k&7))) for(i=0;i<14;i++) font[14*k+i]^=clip[i]; break;
    case SDLK_F2: for(k=0;k<256 && !bplay;k++) if(batch[k>>3]&(1<<(k&7))) cch=k,bplay=1,play=0; batch[cch>>3]&=~(1<<(cch&7)); break;
    default:
      if(event.key.keysym.unicode==27) goto exit;
      goto input;
  }
  goto draw1;
  exit:
  memset(v_font,VF_SYSTEM|VF_FRONT,80*25);
  v_ycur=127;
  v_status[1]=0;
  f=open_lump(name,"w");
  if(!f) errx(1,"Unexpected error when opening font for writing");
  fwrite(font,14,256,f);
  fclose(f);
}
#undef M
#undef MM

static void edit_palette(const char*name) {
  FILE*f=open_lump(name,"r");
  char b[64];
  Uint8 kind=1; // 1(16),2(64),4(128)
  Uint8 red[256];
  Uint8 grn[256];
  Uint8 blu[256];
  int i,j;
  Uint8 x=0;
  Uint8 y=0;
  Uint8 page;
  Uint8 lo=0x00;
  Uint8 hi=0x0F;
  Uint8 pre=177;
  if(f) {
    if(lump_size) {
      i=fgetc(f); rewind(f);
      if(i&~63) goto unknown;
      switch(lump_size) {
        case 16: kind=1; lo=0x00; hi=0x0F; goto ega;
        case 48: kind=1; lo=0x00; hi=0x0F; goto vga;
        case 64: kind=2; lo=0x40; hi=0x7F; goto ega;
        case 128: kind=4; lo=0x80; hi=0xFF; goto ega;
        case 192: kind=2; lo=0x40; hi=0x7F; goto vga;
        case 384: kind=4; lo=0x80; hi=0xFF; goto vga;
        ega:
          for(i=lo;i<=hi;i++) {
            j=fgetc(f);
            red[i]=(j&040?0x15:0)+(j&04?0x2A:0);
            grn[i]=(j&020?0x15:0)+(j&02?0x2A:0);
            blu[i]=(j&010?0x15:0)+(j&01?0x2A:0);
          }
          break;
        vga:
          for(i=lo;i<=hi;i++) red[i]=fgetc(f),grn[i]=fgetc(f),blu[i]=fgetc(f);
          break;
        unknown:
          alert_text("This file uses an unimplemented palette type");
          fclose(f);
          return;
      }
    }
    fclose(f);
  } else {
    win_form("New palette") {
      win_heading("Palette type:");
      win_option('1',"16 colors",kind,1);
      win_option('4',"64 colors",kind,2);
      win_option('8',"128 colors",kind,4);
      win_blank();
      win_command('O',"OK") break;
      win_command_esc(0,"Cancel") return;
    }
    lo="\x00\x40\x80"[kind>>1]; hi="\x0F\x7F\xFF"[kind>>1];
    if(kind==1) for(i=0;i<16;i++) red[i]=(i&4?0x2A:0)+(i&8?0x15:0),grn[i]=(i&2?0x2A:0)+(i&8?0x15:0),blu[i]=(i&1?0x2A:0)+(i&8?0x15:0);
    if(kind==2) for(i=0x40;i<0x80;i++) {
      red[i]=(i&040?0x15:0)+(i&04?0x2A:0);
      grn[i]=(i&020?0x15:0)+(i&02?0x2A:0);
      blu[i]=(i&010?0x15:0)+(i&01?0x2A:0);
    }
    if(kind==4) {
      memset(red,0,256);
      memset(grn,0,256);
      memset(blu,0,256);
    }
  }
  page=lo;
  if(kind==1) set_palette_vga_multi(0,16,red,grn,blu); else set_palette_vga_multi(0x40,0xC0,red+0x40,grn+0x40,blu+0x40);
  // Palette editing
  v_status[1]='p';
  if(kind==1) {
    v_mode=VIDEO_80COLUMNS;
  } else {
    v_mode=VIDEO_80COLUMNS|VIDEO_SMZX;
    if(!font) font=malloc(0xE00);
    if(!font) err(1,"Allocation failed");
    memset(font+0,0x00,14);
    memset(font+14,0x55,14);
    memset(font+28,0xAA,14);
    memset(font+42,0xFF,14);
    memcpy(font+176*14,pcfont+176*14,42);
  }
  draw0:
  memset(v_font,VF_SYSTEM|VF_FRONT,80*25);
  memset(v_color,0x07,80*25);
  memset(v_char,0x20,80*25);
  draw_text(0,0,name,0x0E,-1);
  draw_text(17,0,kind==1?"(16)":kind==2?"(64)":"(128)",0x06,-1);
  for(i=0;i<16;i++) {
    v_color[i*80+162]=v_color[i*80+163]=v_color[i*80+164]=0x8F;
    v_char[i*80+163]="0123456789ABCDEF"[i];
    if(kind==1) {
      memset(v_color+i*80+165,i*17,5);
      memset(v_font+i*80+165,VF_FRONT,5);
    } else if(kind==2) {
      memset(v_char+i*80+165,page/16-4,5);
      memset(v_color+i*80+165,i*17,5);
      memset(v_font+i*80+165,VF_FRONT|VF_ALTERNATE,5);
    } else if(kind==4) {
      memset(v_char+i*80+165,1,5);
      memset(v_color+i*80+165,i+page,5);
      memset(v_font+i*80+165,VF_FRONT,5);
    }
  }
  draw_text(55,1,"<\x18\x19> Select Index",7,-1);
  draw_text(55,2,"<\x1A\x1B> Select Channel",7,-1);
  draw_text(55,3,"<U> Decrease",7,-1);
  draw_text(55,4,"<I> Increase",7,-1);
  draw_text(55,5,"<T> Decrease All",7,-1);
  draw_text(55,6,"<Y> Increase All",7,-1);
  draw_text(55,7,"<0-9> Direct Entry",7,-1);
  draw_text(55,8,kind==1?"<P> Set Preview":"<PgUp/PgDn> Page",7,-1);
  draw_text(55,9,"<C> Copy from",7,-1);
  draw_text(55,10,"<V> Copy to",7,-1);
  if(kind==2) {
    draw_text(4,19,b,0x07,snprintf(b,40,"\xFE Page I%c%c"," IIV"[page/16-4],page==0x60?'I':' '));
    draw_border(8,24,1,41,6);
    for(i=0;i<64;i++) {
      j=(i>>4)*80+(i&15)+185;
      v_char[j]=(i>>4)&3;
      v_color[j]=(i&15)*17;
      v_font[j]=VF_FRONT|VF_ALTERNATE;
    }
  } else if(kind==4) {
    draw_text(4,19,b,0x07,snprintf(b,40,"\xFE Page %c",(page>>4)+0x28));
    draw_border(8,24,1,41,10);
    for(i=0;i<128;i++) {
      j=(i>>4)*80+(i&15)+185;
      v_char[j]=1;
      v_color[j]=i+128;
      v_font[j]=VF_FRONT;
    }
  }
  draw1:
  for(i=0;i<16;i++) {
    v_char[i*80+162]=(y==i?'<':32); v_char[i*80+164]=(y==i?'>':32);
    draw_text(10,i+2,b,0x4C,snprintf(b,64," %02d ",red[i+page]));
    draw_text(14,i+2,b,0x2A,snprintf(b,64," %02d ",grn[i+page]));
    draw_text(18,i+2,b,0x19,snprintf(b,64," %02d ",blu[i+page]));
  }
  v_char[y*80+x*4+170]='<'; v_char[y*80+x*4+173]='>';
  if(kind==1) for(i=0;i<16;i++) {
    v_font[2*i+1760]=v_font[2*i+1761]=v_font[2*i+1840]=v_font[2*i+1841]=VF_FRONT;
    v_color[2*i+1760]=v_color[2*i+1761]=i*16+y; v_color[2*i+1840]=v_color[2*i+1841]=y*16+i;
    v_char[2*i+1760]=v_char[2*i+1761]=v_char[2*i+1840]=v_char[2*i+1841]=pre;
  }
  redisplay();
  input:
  if(!next_event()) return;
  if(event.type!=SDL_KEYDOWN) goto input;
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: goto exit;
    case SDLK_F12: goto draw0;
    case SDLK_BACKSPACE:
      if(x==0) red[y+page]/=10;
      if(x==1) grn[y+page]/=10;
      if(x==2) blu[y+page]/=10;
      set_palette_vga(y+page,red[y+page],grn[y+page],blu[y+page]);
      break;
    case SDLK_0 ... SDLK_9: numbers:
      if(x==0) j=red[y+page]*10+event.key.keysym.sym-SDLK_0,red[y+page]=j>63?63:j;
      if(x==1) j=grn[y+page]*10+event.key.keysym.sym-SDLK_0,grn[y+page]=j>63?63:j;
      if(x==2) j=blu[y+page]*10+event.key.keysym.sym-SDLK_0,blu[y+page]=j>63?63:j;
      set_palette_vga(y+page,red[y+page],grn[y+page],blu[y+page]);
      break;
    case SDLK_TAB: x=(x+1)%3; break;
    case SDLK_LEFT: case SDLK_h: if(x>0) x--; break;
    case SDLK_RIGHT: case SDLK_l: if(x<2) x++; break;
    case SDLK_UP: case SDLK_k: y=(y-1)&15; break;
    case SDLK_DOWN: case SDLK_j: y=(y+1)&15; break;
    case SDLK_KP_PLUS: case SDLK_i:
      if(x==0 && red[y+page]<63) ++red[y+page];
      if(x==1 && grn[y+page]<63) ++grn[y+page];
      if(x==2 && blu[y+page]<63) ++blu[y+page];
      set_palette_vga(y+page,red[y+page],grn[y+page],blu[y+page]);
      break;
    case SDLK_KP_MINUS: case SDLK_u:
      if(x==0 && red[y+page]) --red[y+page];
      if(x==1 && grn[y+page]) --grn[y+page];
      if(x==2 && blu[y+page]) --blu[y+page];
      set_palette_vga(y+page,red[y+page],grn[y+page],blu[y+page]);
      break;
    case SDLK_y:
      if(red[y+page]<63) ++red[y+page];
      if(grn[y+page]<63) ++grn[y+page];
      if(blu[y+page]<63) ++blu[y+page];
      set_palette_vga(y+page,red[y+page],grn[y+page],blu[y+page]);
      break;
    case SDLK_t:
      if(red[y+page]) --red[y+page];
      if(grn[y+page]) --grn[y+page];
      if(blu[y+page]) --blu[y+page];
      set_palette_vga(y+page,red[y+page],grn[y+page],blu[y+page]);
      break;
    case SDLK_p: if(kind==1) pre=ask_color_char(1,pre); goto draw0;
    case SDLK_c: red[16]=red[y+page]; grn[16]=grn[y+page]; blu[16]=blu[y+page]; break;
    case SDLK_v: set_palette_vga(y+page,red[y+page]=red[16],grn[y+page]=grn[16],blu[y+page]=blu[16]); break;
    case SDLK_PAGEUP: if(kind==2) page=0x40|(page-16)&0x70; else if(kind==4) page=0x80|(page-16); goto draw0;
    case SDLK_PAGEDOWN: if(kind==2) page=0x40|(page+16)&0x70; else if(kind==4) page=0x80|(page+16); goto draw0;
    default:
      if(event.key.keysym.unicode>='0' && event.key.keysym.unicode<='9') {
        event.key.keysym.sym=event.key.keysym.unicode;
        goto numbers;
      }
      if(event.key.keysym.unicode==27) goto exit;
      goto input;
  }
  goto draw1;
  exit:
  memset(v_font,VF_SYSTEM|VF_FRONT,80*25);
  v_ycur=127;
  v_status[1]=0;
  v_mode=VIDEO_80COLUMNS;
  f=open_lump(name,"w");
  if(!f) errx(1,"Unexpected error when opening palette for writing");
  for(i=lo,j=0;i<=hi && !j;i++) j=red[i]%0x15+grn[i]%0x15+blu[i]%0x15;
  for(i=lo;i<=hi;i++) {
    if(j) fputc(red[i],f),fputc(grn[i],f),fputc(blu[i],f); else fputc(+(red[i]&1?040:0)+(grn[i]&1?020:0)+(blu[i]&1?010:0)+(red[i]&2?4:0)+(grn[i]&2?2:0)+(blu[i]&2?1:0),f);
  }
  fclose(f);
}

static void graphics_global_options(void) {
  ASN1_Encoder*enc;
  ASN1_Value v={};
  ASN1_Value*vv;
  char mfont[9]={};
  char mpal[9]={};
  Uint8 bit=0;
  int i;
  load_general_der();
  if(vv=find_general_oid(ASN1_RELATIVE_OID,"\x04\x00\x0E",3)) {
    if(vv->class==MANDATORY) bit=1;
    if(vv->class==OPTIONAL) bit=2;
  }
  if(general_parts[1].class) {
    if(!asn1_first_of(&v,general_parts+1)) for(i=0;i<2;i++) {
      if(v.class!=ASN1_UNIVERSAL) {
        bad1: alert_text("Invalid data in GENERAL.DER will be removed"); break;
      }
      if(v.type!=ASN1_NULL) {
        if(v.type!=ASN1_VISIBLE_STRING || v.length>8 || v.length<1) goto bad1;
        memcpy(i?mpal:mfont,v.data,v.length);
      }
      if(!i && asn1_next_of(&v,general_parts+1)) goto bad1;
    }
  }
  win_form("Graphics - global options") {
    win_help("editgr","glo");
    win_boolean('4',"40 columns",start_mode,0x0002);
    win_boolean('M',"Mandatory fonts/palettes",bit,1);
    win_boolean('R',"Recommended fonts/palettes",bit,2);
    win_text_restrict('f',"Main font: ",mfont);
    win_text_restrict('p',"Main palette: ",mpal);
    win_blank();
    win_command_esc(0,"Done") break;
  }
  asn1_free(general_parts+1);
  if(*mfont || *mpal) {
    enc=asn1_start_encoding_constructed_value(general_parts+1,ASN1_CONTEXT_SPECIFIC,1,0);
    if(!enc) err(1,"Allocation failed");
    if(*mfont) asn1_encode_c_string(enc,ASN1_VISIBLE_STRING,mfont); else asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_NULL,0,0);
    if(*mpal) asn1_encode_c_string(enc,ASN1_VISIBLE_STRING,mpal); else asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_NULL,0,0);
    asn1_finish_encoder(enc);
  } else {
    general_parts[1].class=0;
  }
  add_general_oid(REMOVED,ASN1_RELATIVE_OID,"\x04\x00\x0E",3);
  if(bit&3) add_general_oid(bit&1?MANDATORY:OPTIONAL,ASN1_RELATIVE_OID,"\x04\x00\x0E",3);
  save_general_der();
  unload_general_der();
}

int run_editor(void) {
  int i,n,lo,hi;
  char c,b;
  Uint16 lbrd=cur_board_id;
  Uint16 lscr=0;
  v_status[0]='E';
  config.debug=1;
  win_form("Editor") {
    win_help("edit",0);
    win_numeric('t',"Starting board: ",cur_board_id,0,65535);
    win_command('B',"Boards...") {
      boards_form:
      win_form("Boards") {
        win_help("blist",0);
        win_cursor(lbrd);
        if(boardnames) win_list(maxboard+1,0,board_list_callback,n) {
          lbrd=edit_board(n);
          goto boards_form;
        }
        win_blank();
        if(boardnames) win_command('F',"Find") {
          char buf[61]="";
          ask_text("Find:",buf,60);
          b=strlen(buf);
          for(n=0;n<=maxboard;n++) {
            if(boardnames[n] && !strncmp(boardnames[n],buf,b)) {
              lbrd=n;
              goto boards_form;
            }
          }
        }
        if(maxboard!=65535) {
          win_command('A',"Add new board") {
            char buf[61]="";
            ask_text("Add new board:",buf,60);
            if(*buf) {
              set_board_name(lbrd=maxboard+(boardnames?1:0),buf);
              edit_board(lbrd);
              goto boards_form;
            }
          }
          win_command('C',"Copy board...") {
            char buf[61]="";
            n=lbrd;
            win_form("Copy board") {
              win_help("blist","copy");
              win_text('N',"Name: ",buf);
              win_numeric('S',"Source: ",n,0,maxboard);
              win_blank();
              win_command('x',"Execute") {
                if(*buf) {
                  set_board_name(lbrd=maxboard+(boardnames?1:0),buf);
                  if(copy_board(n,lbrd)) {
                    edit_board(lbrd);
                    goto boards_form;
                  }
                }
              }
              win_command_esc(0,"Cancel") break;
            }
          }
        }
        win_command_esc(0,"Done") break;
      }
      if(boardnames) write_name_list("BRD.NAM",boardnames,maxboard);
    }
    win_command('c',"Screens...") {
      screens_form:
      win_form("Screens") {
        win_help("slist",0);
        win_cursor(lscr);
        if(screennames) win_list(maxscreen+1,0,screen_list_callback,n) {
          lscr=edit_screen(n);
          goto screens_form;
        }
        win_blank();
        if(maxscreen!=65535) {
          win_command('A',"Add new screen") {
            char buf[61]="";
            ask_text("Add new screen:",buf,60);
            if(*buf) {
              set_screen_name(lscr=maxscreen+(screennames?1:0),buf);
              edit_screen(lscr);
              goto screens_form;
            }
          }
        }
        win_command_esc(0,"Done") break;
      }
      if(screennames) write_name_list("SCR.NAM",screennames,maxscreen);
    }
    win_command('v',"Status variables...") {
      win_form("Status variables") {
        int i;
        win_help("stvar",0);
        for(i=0;i<16;i++) {
          char b[3]={(i&7)+(i&8?'S':'A'),'=',0};
          win_numeric(*b,b,status_vars[i],0,999999999);
        }
        win_blank();
        win_command_esc(0,"Done") break;
      }
    }
    win_command('E',"Elements...") {
      c=0;
      win_form("Elements") {
        win_help("element","list");
        win_list(256,0,element_list_callback,n) {
          c=1;
          edit_element(n);
        }
        win_blank();
        win_command('C',"Copy attributes...") {
          n=0; lo=0; hi=255; b=1;
          win_form("Copy attributes") {
            win_help("element","copy");
            win_numeric('S',"Source: ",n,0,255);
            win_numeric('L',"Low target: ",lo,0,255);
            win_numeric('H',"High target: ",hi,0,255);
            win_boolean('E',"Exclude if already defined",b,1);
            win_blank();
            win_command('x',"Execute") {
              c=1;
              for(i=lo;i<=hi;i++) if(!b || !elem_def[i].name[0]) {
                elem_def[i].app[0]=elem_def[n].app[0];
                elem_def[i].app[1]=elem_def[n].app[1];
                elem_def[i].attrib=elem_def[n].attrib;
              }
              break;
            }
            win_command_esc(0,"Cancel") break;
          }
        }
        win_command_esc(0,"Done") break;
      }
      if(c) write_element_lump();
    }
    win_command('p',"Appearance mapping...") {
      if(edit_appearance_mapping()) write_element_lump();
      win_refresh();
    }
    win_command('i',"Animations...") {
      n=i=c=0;
      win_form("Animations") {
        win_help("anima",0);
        win_numeric('m',"Animation edit: ",n,0,3) win_refresh();
        win_blank();
        win_numeric('S',"Step I:   ",animation[n].step[0],0,127) win_refresh(),c=1;
        win_numeric('t',"Step II:  ",animation[n].step[1],0,127) win_refresh(),c=1;
        win_numeric('e',"Step III: ",animation[n].step[2],0,127) win_refresh(),c=1;
        win_numeric('p',"Step IV:  ",animation[n].step[3],0,127) win_refresh(),c=1;
        win_boolean('X',"X1",animation[n].mode,AM_X1) win_refresh(),c=1;
        win_boolean('2',"X2",animation[n].mode,AM_X2) win_refresh(),c=1;
        win_boolean('1',"Y1",animation[n].mode,AM_Y1) win_refresh(),c=1;
        win_boolean('Y',"Y2",animation[n].mode,AM_Y2) win_refresh(),c=1;
        win_boolean('W',"SLOW",animation[n].mode,AM_SLOW) c=1;
        win_blank();
        win_numeric('v',"Preview from: ",i,0,127) win_refresh();
        win_picture(6) {
          for(lo=1;lo<12;lo++) for(hi=0;hi<6;hi++) {
            v_color[hi*80+lo]=v_color[hi*80+lo+20]=v_color[hi*80+lo+40]=v_color[hi*80+lo+60]=7;
            v_char[hi*80+lo]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3))&3]+i)&127];
            v_char[hi*80+lo+20]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3)+1)&3]+i)&127];
            v_char[hi*80+lo+40]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3)+2)&3]+i)&127];
            v_char[hi*80+lo+60]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3)+3)&3]+i)&127];
          }
        }
        win_blank();
        win_command_esc(0,"Done") break;
      }
      if(c) write_element_lump();
    }
    win_command('N',"Numeric formats...") {
      n=c=0;
      win_form("Numeric formats") {
        win_help("numform",0);
        win_numeric('t',"Numeric format edit: ",n,0,15) win_refresh();
        win_blank();
        win_option('D',"Decimal",num_format[n].code,NF_DECIMAL) c=1;
        win_option('U',"Uppercase hex",num_format[n].code,NF_HEX_UPPER) c=1;
        win_option('w',"Lowercase hex",num_format[n].code,NF_HEX_LOWER) c=1;
        win_option('O',"Octal",num_format[n].code,NF_OCTAL) c=1;
        win_option('C',"Comma",num_format[n].code,NF_COMMA) c=1;
        win_option('R',"Roman",num_format[n].code,NF_ROMAN) c=1;
        win_option('y',"\x9Csd money",num_format[n].code,NF_LSD_MONEY) c=1;
        win_option('e',"Meter (full blocks)",num_format[n].code,NF_METER) c=1;
        win_option('h',"Meter (half blocks)",num_format[n].code,NF_METER_HALF) c=1;
        win_option('f',"Meter ext. (full blocks)",num_format[n].code,NF_METER_EXT) c=1;
        win_option('k',"Meter ext. (half blocks)",num_format[n].code,NF_METER_HALF_EXT) c=1;
        win_option('i',"Binary",num_format[n].code,NF_BINARY) c=1;
        win_option('x',"Binary ext.",num_format[n].code,NF_BINARY_EXT) c=1;
        win_option('a',"Character",num_format[n].code,NF_CHARACTER) c=1;
        win_option('z',"Nonzero",num_format[n].code,NF_NONZERO) c=1;
        win_option('B',"Board name",num_format[n].code,NF_BOARD_NAME) c=1;
        win_option('n',"Board name ext.",num_format[n].code,NF_BOARD_NAME_EXT) c=1;
        win_blank();
        win_char('L',"Lead: ",num_format[n].lead) c=1;
        win_char('M',"Mark: ",num_format[n].mark) c=1;
        win_numeric('v',"Division: ",num_format[n].div,0,255) c=1;
        win_blank();
        win_command_esc(0,"Done") break;
      }
      if(c) write_numform_lump();
    }
    win_command('H',"Help lumps...") {
      lump_listing_menu("*.HLP","Help lumps",edit_one_help_lump,"edithelp",0);
    }
    win_command('G',"Global script...") {
      edit_one_help_lump("GLOBAL");
      win_refresh();
    }
    win_command('l',"Script library...") {
      lump_listing_menu("*.LIB","Script library",edit_one_help_lump,"sclib",0);
    }
    win_command('a',"Graphics...") {
      win_form("Graphics") {
        win_help("editgr",0);
        win_command('s',"Font (simple)") lump_listing_menu("*.CHR","Fonts (simple)",edit_font,"editgr","chr");
        win_command('a',"Font (advanced)") alert_text("Not implemented");
        win_command('P',"Palette") lump_listing_menu("*.PAL","Palettes",edit_palette,"editgr","pal");
        win_blank();
        win_command('G',"Graphics global options...") graphics_global_options();
        win_blank();
        win_command_esc(0,"Go back") break;
      }
    }
    win_command('.',"More...") {
      load_general_der();
      start_mode^=0x0001;
      win_form("Editor") {
        win_help("edit","more");
        win_boolean('p',"Auto pause",start_mode,0x0001);
        win_boolean('4',"40 columns",start_mode,0x0002);
        win_command('J',"Joystick configuration...") edit_joystick();
        win_command('.',"Advanced...") {
          win_form("Advanced editor") {
            win_help("editadv",0);
            win_command('O',"OID sets...") edit_oid_sets();
            win_blank();
            win_command_esc(0,"Go back") break;
          }
        }
        win_blank();
        win_command_esc(0,"Go back") break;
      }
      start_mode^=0x0001;
      save_general_der();
      unload_general_der();
    }
    win_blank();
    win_command('R',"Run") {
      write_start_lump();
      run_test_game(-1);
      win_refresh();
    }
    win_command('S',"Save") {
      FILE*fp=open_lump("!SZ0","w");
      time_t ti=time(0);
      if(fp) {
        fputc(0x01,fp); // (unpublished world file)
        fputc(42,fp); fputc(SDL_GetTicks(),fp);
        fputc(ti>>8,fp); fputc(ti>>16,fp); fputc(ti>>0,fp); fputc(ti>>24,fp);
        fclose(fp);
      }
      if(fp=open_lump("CATALOG.DER","w")) fclose(fp);
      write_start_lump();
      if(config.version_auto==255) config.version_auto=version_is_release;
      if(config.version_auto) {
        config.version_auto=0;
        load_general_der();
        // If any older version numbers must be kept for compatibility, then this must be changed to avoid removing those ones.
        // In that case, a compatibility menu should be added into the editor in order to manually add and remove those version numbers.
        for(i=0;i<n_general_oids;i++) if(general_oids[i].type==ASN1_RELATIVE_OID && general_oids[i].length && !general_oids[i].data[0]) general_oids[i].class=REMOVED;
        if(version_rel_oid_length_2>version_rel_oid_length && version_rel_oid[version_rel_oid_length]) add_general_oid(OPTIONAL,ASN1_RELATIVE_OID,version_rel_oid,version_rel_oid_length);
        add_general_oid(MANDATORY,ASN1_RELATIVE_OID,version_rel_oid,version_rel_oid_length_2);
        save_general_der();
        unload_general_der();
      }
      save_world(0);
    }
    win_command('Q',"Quit") break;
    win_blank();
    win_command(0,"Help (ALT+?)") online_help("edit",0);
  }
}
