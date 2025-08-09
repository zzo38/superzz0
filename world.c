#if 0
gcc $CFLAGS -c -Wno-unused-result world.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

static int check_feature(const ASN1_Value*v) {
  // Returns 0 if feature is valid, nonzero if feature is not valid.
  // Will also enable the feature if necessary (currently this has no effect)
  // (Note: This is not really tested properly yet, but is partally tested)
  int i,j;
  if(v->type==ASN1_RELATIVE_OID && v->length<=version_rel_oid_length && !memcmp(v->data,version_rel_oid,v->length)) return 0;
  if(v->type==ASN1_RELATIVE_OID && v->length>2 && !v->data[0] && !version_rel_oid[0]) {
    // (This currently assumes that the pieces of the version number cannot exceed 16383, but it is unlikely to exceed 16383 anyways)
    i=j=(v->data[1]>127?3:2);
    // Major
    if(v->data[1]!=version_rel_oid[1]) return 1;
    if(v->data[1]>127 && v->data[2]!=version_rel_oid[2]) return 1;
    // Minor, patch, others
    if(v->length>i) {
      if(version_rel_oid_length-i<=0) return 1;
      if(version_rel_oid_length>=v->length && memcmp(v->data+i,version_rel_oid+i,v->length-i)>0) return 1;
      if(version_rel_oid_length<v->length && memcmp(v->data+i,version_rel_oid+i,version_rel_oid_length-i)>=0) return 1;
    }
    // OK
    return 0;
  }
  if(v->type==ASN1_RELATIVE_OID && v->length==3 && !memcmp(v->data,"\x04\x00\x0E",3)) return 0;
  return -1;
}

static int do_joystick_config(const ASN1_Value*v) {
  char buf[256];
  ASN1_Value a,b;
  ASN1_Iterator it;
  int c,d,i,q;
  Sint32 z;
  asn1_foreach(q,&it,v,&a) {
    if(!a.class && a.type==ASN1_SEQUENCE && a.constructed) {
      if(asn1_first_of(&b,&a) || b.class || b.type!=ASN1_PC_STRING || b.length>255) return 1;
      memcpy(buf,b.data,b.length);
      buf[b.length]=0;
      if(asn1_next_of(&b,&a) || b.class || b.type!=ASN1_OCTET_STRING) return 1;
      z=configure_joystick(1,buf);
      if(z<0) continue;
      for(d=i=q=0;i<b.length && q<16;) {
        switch(c=b.data[i++]) {
          case 3: joystat->map[z].a[q++]=0; if(q<16)
          case 2: joystat->map[z].a[q++]=0; if(q<16)
          case 1: joystat->map[z].a[q++]=0; if(q<16)
          case 0: joystat->map[z].a[q++]=0; break;
          case 7: joystat->map[z].a[q++]=d; if(q<16)
          case 6: joystat->map[z].a[q++]=d; if(q<16)
          case 5: joystat->map[z].a[q++]=d; if(q<16)
          case 4: joystat->map[z].a[q++]=d; if(q<16)
          /*   */ joystat->map[z].a[q++]=d; break;
          case 8 ... 9: case 13: case 16 ... 17: case 24 ... 27: case 30 ... 126:
          case 8+128 ... 9+128: case 13+128: case 16+128 ... 17+128: case 24+128 ... 27+128: case 30+128 ... 126+128:
            joystat->map[z].a[q++]=d=c; break;
          case 12: if(i<b.length && q<16) joystat->map[z].a[q++]=d=b.data[i++]+0x100; //
          case 11: if(i<b.length && q<16) joystat->map[z].a[q++]=d=b.data[i++]+0x100; //
          case 10: if(i<b.length && q<16) joystat->map[z].a[q++]=d=b.data[i++]+0x100; break;
          case 128 ... 135: joystat->map[z].a[q++]=d=c+0x181; break;
          default: fprintf(stderr,"Incorrect byte (%02X) in joystick configuration in GENERAL.DER",c); return 1;
        }
      }
      for(i=16;i<24;i++) if(joystat->map[z].a[i]&0x8000) joystat->map[z].a[i]=0x300;
    }
  }
  return q!=ASN1_DONE;
}

static int do_font_palette(const ASN1_Value*v) {
  char m[12];
  ASN1_Value a;
  if(asn1_first_of(&a,v) || a.class!=ASN1_UNIVERSAL || a.constructed || a.length>8) return 1;
  if(a.type==ASN1_VISIBLE_STRING && a.length>0) {
    memcpy(m,a.data,a.length);
    m[a.length]=0;
    if(!load_font(m,LOADFONT_BASE)) return 1;
  } else if(a.type!=ASN1_NULL) {
    return 1;
  }
  if(asn1_next_of(&a,v) || a.class!=ASN1_UNIVERSAL || a.constructed || a.length>8) return 1;
  if(a.type==ASN1_VISIBLE_STRING && a.length>0) {
    memcpy(m,a.data,a.length);
    m[a.length]=0;
    if(!load_palette(m,LOADPAL_BASE)) return 1;
  } else if(a.type!=ASN1_NULL) {
    return 1;
  }
  return 0;
}

const char*init_world(void) {
  // Returns 0 if OK, error message if error
  int i,j;
  Uint32 u,v;
  FILE*fp;
  // "!SZ0"
  if(config.version_check) {
    fp=open_lump("!SZ0","r");
    if(!fp) return "Cannot open !SZ0 lump";
    if(lump_size!=7) {
      fclose(fp);
      return "Wrong size of !SZ0 lump";
    }
    i=fgetc(fp);
    fclose(fp);
    if((i^1)&0xF1) return "Wrong file type";
  }
  // "MEMORY"
  if(!editor) {
    fp=open_lump("MEMORY","r");
    if(!fp) return "Cannot open MEMORY lump";
    u=lump_size>>1;
    if(u>0x10000) u=0x10000;
    for(i=0;i<u;i++) memory[i]=read16(fp);
    for(;i<0x10000;i++) memory[i]=0;
    fclose(fp);
  } else if(fp=open_lump("MEMORY.ED","r")) {
    for(i=0;i<0x10000;i++) memory[i]=0;
    i=0x100;
    while(i<0x10000) {
      j=fgetc(fp);
      if(j==EOF) break;
      switch(j>>6) {
        case 0: i+=j+1; break;
        case 1: j=(j&0x3F)+1; while(j-- && i<0x10000) memory[i++]=read8(fp); break;
        case 2: j=(j&0x3F)+1; while(j-- && i<0x10000) memory[i++]=read16(fp); break;
        case 3: return "Improper file format";
      }
    }
    fclose(fp);
  }
  // "START"
  fp=open_lump("START","r");
  if(!fp) return "Cannot open START lump";
  start_mode=u=read16(fp);
  if(!(u&0x0001)) config.pause|=128;
  if((u&0x0002) && !editor) v_mode&=~VIDEO_80COLUMNS;
  if(u&~0x0003) return "Unrecognized data in START lump";
  cur_screen.message_l=222;
  cur_board_id=read16(fp);
  v=read32(fp);
  if((v&~0xFFFF) || read32(fp)) return "Unrecognized data in START lump";
  for(i=0;i<16;i++) status_vars[i]=(v&(1<<i))?0:read32(fp);
  for(i=0;i<16;i++) namedflag[i].name[0]=0;
  fclose(fp);
  // "GENERAL.DER"
  if(fp=open_lump("GENERAL.DER","r")) {
    // (Note: This is not really tested properly yet)
    ASN1_Value a1,a2,a3,a4;
    ASN1_Iterator i1,i2,i3;
    if(asn1_read_item(fp,&a1,0)) {
      fclose(fp);
      return "ASN.1 error in GENERAL.DER lump";
    }
    fclose(fp);
    asn1_rewind(&i1,&a1);
    // Mandatory features
    if(asn1_next(&i1,&a2)) return "ASN.1 error in GENERAL.DER lump";
    if(a2.class || a2.type!=ASN1_SET || !a2.constructed) return "Improper type in GENERAL.DER lump";
    asn1_foreach(j,&i2,&a2,&a3) {
      if(a3.class || (a3.type!=ASN1_OID && a3.type!=ASN1_RELATIVE_OID && a3.type!=ASN1_SEQUENCE)) return "Improper type in GENERAL.DER lump";
      if(a3.type==ASN1_SEQUENCE) {
        i=0;
        asn1_foreach(j,&i3,&a3,&a4) {
          if(a4.class || (a4.type!=ASN1_OID && a4.type!=ASN1_RELATIVE_OID)) return "Improper type in GENERAL.DER lump";
          if(!(i=check_feature(&a4))) break;
        }
        if(i && (config.version_check || config.version_warn || !editor)) {
          fprintf(stderr,"Unrecognized sequence of OIDs in mandatory set: ");
          asn1_foreach(j,&i3,&a3,&a4) fputc(' ',stderr),asn1_print_decimal_oid(&a4,ASN1_AUTO,stderr);
          goto gen1;
        }
      } else if(check_feature(&a3) && (config.version_check || config.version_warn || !editor)) {
        fprintf(stderr,"Unrecognized OID in mandatory set: ");
        asn1_print_decimal_oid(&a3,ASN1_AUTO,stderr);
        gen1:
        fputc('\n',stderr);
        config.version_warn|=4;
        if(config.version_check && !editor) return "Unimplemented feature in mandatory set";
      }
    }
    // Optional features
    if(asn1_next(&i1,&a2)) return "ASN.1 error in GENERAL.DER lump";
    if(a2.class || a2.type!=ASN1_SET || !a2.constructed) return "Improper type in GENERAL.DER lump";
    asn1_foreach(j,&i2,&a2,&a3) {
      if(a3.class || (a3.type!=ASN1_OID && a3.type!=ASN1_RELATIVE_OID && a3.type!=ASN1_SEQUENCE)) return "Improper type in GENERAL.DER lump";
      if(a3.type==ASN1_SEQUENCE) {
        i=0;
        asn1_foreach(j,&i3,&a3,&a4) {
          if(a4.class || (a4.type!=ASN1_OID && a4.type!=ASN1_RELATIVE_OID)) return "Improper type in GENERAL.DER lump";
          if(!(i=check_feature(&a4))) break;
        }
        if(i && config.version_warn) {
          fprintf(stderr,"Unrecognized sequence of OIDs in optional set:");
          asn1_foreach(j,&i3,&a3,&a4) fputc(' ',stderr),asn1_print_decimal_oid(&a4,ASN1_AUTO,stderr);
          goto gen2;
        }
      } else if(check_feature(&a3) && config.version_warn) {
        fprintf(stderr,"Unrecognized OID in optional set: ");
        asn1_print_decimal_oid(&a3,ASN1_AUTO,stderr);
        gen2:
        fputc('\n',stderr);
        config.version_warn|=2;
      }
    }
    // Others
    while(!asn1_next(&i1,&a2)) if(a2.class==ASN1_CONTEXT_SPECIFIC && a2.constructed) switch(a2.type) {
      case 0: // Joystick configuration
        if(joystat && do_joystick_config(&a2)) return "Error in joystick configuration in world file";
        break;
      case 1: // Font/palette
        if(do_font_palette(&a2) && !editor) return "World specification of font/palette is incorrect";
        break;
    }
    // Done
    asn1_free(&a1);
  }
  // "NUMFORM"
  if(fp=open_lump("NUMFORM","r")) {
    for(i=0;i<16;i++) {
      num_format[i].code=read8(fp);
      num_format[i].lead=read8(fp);
      num_format[i].mark=read8(fp);
      num_format[i].div=read8(fp);
    }
    fclose(fp);
  } else {
    num_format[0].code=num_format[1].code='d';
    num_format[0].lead=' '; num_format[1].lead='0';
    num_format[0].div=num_format[1].div=1;
  }
  // "ELEMENT"
  memset(elem_def,0,sizeof(elem_def));
  fp=open_lump("ELEMENT","r");
  if(!fp) return "Cannot open ELEMENT lump";
  fread(appearance_mapping,1,128,fp);
  for(i=0;i<4;i++) {
    animation[i].mode=read8(fp);
    fread(animation[i].step,1,4,fp);
  }
  for(i=0;i<256;i++) {
    u=read8(fp);
    if(u&15) {
      fread(elem_def[i].name,1,u&15,fp);
      if(u&0x80) elem_def[i].app[0]=fgetc(fp);
      if(u&0x40) elem_def[i].app[1]=fgetc(fp);
      if(!(u&0xC0)) elem_def[i].app[0]=AP_PARAM;
      if(u&0x20) elem_def[i].attrib=read32(fp); else elem_def[i].attrib=i?elem_def[i-1].attrib:0;
      if(u&0x10) {
        u=read16(fp);
        for(j=0;j<16;j++) if(u&(1<<j)) elem_def[i].event[j]=read16(fp);
      }
    } else {
      i+=u>>4;
    }
  }
  fclose(fp);
  // "TEXT"
  free(vgtext),vgtext=0;
  free(gtext),gtext=0;
  if(fp=open_lump(editor?"TEXT.ED":"TEXT","r")) {
    Uint8*p;
    vgtext=malloc(lump_size+1);
    if(!vgtext) err(1,"Allocation failed");
    fread(vgtext,1,lump_size,fp);
    vgtext[lump_size]=0;
    ngtext=1;
    for(i=0;i<lump_size;i++) if(!vgtext[i]) ++ngtext;
    gtext=malloc(ngtext*sizeof(Uint8*));
    if(!gtext) err(1,"Allocation failed");
    for(i=1,u=0,p=vgtext;i<ngtext;i++) {
      gtext[i]=p;
      p+=strlen(p)+1;
    }
    fclose(fp);
  } else {
    gtext=malloc(sizeof(Uint8*));
    if(!gtext) err(1,"Allocation failed");
    ngtext=1;
  }
  *gtext="";
  // "BRD.NAM"
  if(boardnames) {
    free(*boardnames);
    if(editor) for(u=1;u<=maxboard;u++) free(boardnames[u]);
  }
  free(boardnames),boardnames=0;
  if(fp=open_lump("BRD.NAM","r")) {
    Uint8*s;
    Uint8*ss;
    maxboard=read16(fp);
    boardnames=calloc(maxboard+1,sizeof(Uint8*));
    if(!boardnames) err(1,"Allocation failed");
    if(editor) {
      size_t z;
      for(j=0;j<=maxboard;j++) {
        s=0; z=0; if(getdelim((char**)&s,&z,0,fp)<=0) break;
        boardnames[j]=s;
      }
    } else {
      u=(lump_size<3?1:lump_size-2);
      s=malloc(u+1);
      if(!s) err(1,"Allocation failed");
      fread(s,1,u,fp);
      s[u]=0;
      ss=s+u;
      for(j=0;j<=maxboard && s<ss;j++) {
        i=strlen(s);
        boardnames[j]=s;
        s+=i+1;
      }
    }
    fclose(fp);
  }
  // "SCR.NAM"
  if(screennames) {
    free(*screennames);
    if(editor) for(u=1;u<=maxscreen;u++) free(screennames[u]);
  }
  free(screennames),screennames=0;
  if(editor && (fp=open_lump("SCR.NAM","r"))) {
    Uint8*s;
    size_t z;
    maxscreen=read16(fp);
    screennames=calloc(maxscreen+1,sizeof(Uint8*));
    for(j=0;j<=maxscreen;j++) {
      s=0; z=0; if(getdelim((char**)&s,&z,0,fp)<=0) break;
      screennames[j]=s;
    }
    fclose(fp);
  }
  // "GLOBAL"
  if(global_text) free(global_text);
  global_text=0;
  global_length=0;
  if(!editor && (fp=open_lump("GLOBAL","r"))) {
    if(lump_size>=0xFFFE) errx(1,"Global script is too long");
    global_length=lump_size;
    global_text=malloc(lump_size+1);
    if(!global_text) err(1,"Allocation failed");
    fread(global_text,1,lump_size,fp);
    global_text[lump_size]=0;
    fclose(fp);
  }
  // "DYNASTR"
  free(dynastr);
  dynastr=0;
  ndynastr=0;
  if(fp=open_lump("DYNASTR","r")) {
    warnx("This world file contains a DYNASTR lump but it is not supposed to");
    config.version_warn|=4;
    fclose(fp);
    if(fp=open_lump("DYNASTR","w")) fclose(fp);
  }
  // done
  return 0;
}

static void load_varproperties(FILE*fp,Uint32 on,VarPropertyList*vp) {
  int i;
  free(vp->item);
  vp->item=0;
  vp->count=0;
  if(!on) return;
  vp->count=fgetc(fp);
  if(!vp->count) return;
  vp->item=calloc(vp->count,sizeof(VarProperty));
  if(!vp->item) err(1,"Allocation failed");
  for(i=0;i<vp->count;i++) if((vp->item[i].type=fgetc(fp))&15) fread(vp->item[i].data,1,vp->item[i].type&15,fp);
}

static void save_varproperties(FILE*fp,const VarPropertyList*vp) {
  int i;
  fputc(vp->count,fp);
  for(i=0;i<vp->count;i++) fputc(vp->item[i].type,fp),fwrite(vp->item[i].data,1,vp->item[i].type&15,fp);
}

static inline void fill_layer(Tile*p,Tile t,Uint32 c) {
  while(c--) *p++=t;
}

static void layer_inversion_stat(Uint32 at,Uint8 lay) {
  Uint32 x=at%board_info.width;
  Uint32 y=at/board_info.width;
  Uint32 n=b_under[at].stat|b_main[at].stat;
  Stat*s=stats+n-1;
  if(n>maxstat) return;
  for(n=0;n<s->count;n++) if(s->xy[n].x==x && s->xy[n].y==y && (s->xy[n].layer&3)==lay) {
    s->xy[n].layer^=3;
    return;
  }
}

static void layer_inversion(void) {
  Uint32 tc=board_info.width*board_info.height;
  Uint32 i;
  Tile t;
  for(i=0;i<tc;i++) {
    if(b_under[i].kind && b_main[i].kind) {
      if(b_under[i].stat && !b_main[i].stat) layer_inversion_stat(i,1); else if(!b_under[i].stat && b_main[i].stat) layer_inversion_stat(i,2);
      t=b_under[i];
      b_under[i]=b_main[i];
      b_main[i]=t;
    }
  }
}

static inline Uint8 make_guess(const Tile*pt,Uint8 v,Uint8*g) {
  return v?g[pt->kind]:pt->stat?*g:0;
}

static inline void update_guess(const Tile*pt,Uint8 v,Uint8*g) {
  // If the guess is correct then this does not actually make any changes.
  if(v) g[pt->kind]=pt->values[v]; else if(pt->stat) *g=pt->kind;
}

static Uint32 load_board_run(FILE*fp,Tile*pt,Tile*end,Uint8 v,Uint8*g) {
  Uint8 c=fgetc(fp);
  Uint8 m=c>>6;
  Uint16 r=c&63;
  Uint16 n=0;
  if(!r) {
    r=127+fgetc(fp);
    if(r>254) r+=(fgetc(fp)<<7)+63;
  }
  if(r>end-pt) r=end-pt;
  if(pt==b_under && m>1) m=0;
  if(pt<b_under+board_info.width && m==3) m=0;
  while(n<r) {
    pt[n].values[v]=(m==0?make_guess(pt+n,v,g):m==1?fgetc(fp):m==2?pt[-1].values[v]:pt[n-board_info.width].values[v]);
    update_guess(pt+n,v,g);
    n++;
  }
  return r;
}

static inline void hetero_board_run(FILE*fp,const Tile*pt,Uint16 n,Uint8 v) {
  Uint16 i;
  for(i=0;i<n;i++) fputc(pt[i].values[v],fp);
}

static Uint32 save_board_run(FILE*fp,const Tile*pt,const Tile*end,Uint8 v,Uint8*g) {
  Uint8 g0[256];
  Uint8 m;
  Uint16 w=board_info.width;
  Uint16 r0,r2,r3,n,i;
  for(r0=r2=r3=n=0;n<end-pt && (n==r0 || n==r2 || n==r3) && n<33085;n++) {
    if(n==r0 && pt[n].values[v]==make_guess(pt+n,v,g)) r0++;
    if(n==r2 && pt>b_under && pt[n-1].values[v]==pt[n].values[v]) r2++;
    if(n==r3 && pt>=b_under+w && pt[n-w].values[v]==pt[n].values[v]) r3++;
  }
  if(!r0 && !r2 && !r3) {
    memcpy(g0,g,256);
    for(n=1,i=0;n<end-pt && n<33085;n++) {
      if(pt>b_under && pt[n-1].values[v]==pt[n].values[v]) i|=010;
      if(pt>=b_under+w && pt[n-w].values[v]==pt[n].values[v]) i|=020;
      if(pt[n].values[v]==make_guess(pt+n,v,g0)) i|=040;
      if(i&(i>>3)) {
        n--;
        break;
      }
      i>>=3;
      update_guess(pt+n,v,g0);
    }
    m=1;
  } else if(r0>r2 && r0>r3) {
    m=0; n=r0;
  } else if(r2>=r3) {
    m=2; n=r2;
  } else {
    m=3; n=r3;
  }
  if(n>63 && n<127) n=63;
  if(n>254 && n<445) n=254;
  if(m) for(i=0;i<n;i++) update_guess(pt+i,v,g);
  if(n<64) {
    fputc(n|(m<<6),fp);
  } else if(n<255) {
    fputc(m<<6,fp);
    fputc(n-127,fp);
  } else {
    fputc(m<<6,fp);
    fputc((n-318)|128,fp);
    fputc((n-318)>>7,fp);
  }
  if(m==1) hetero_board_run(fp,pt,n,v);
  return n;
}

const char*load_board(FILE*fp) {
  Uint8 guess[256];
  Uint8 c,sf;
  Uint16 ef=read16(fp);
  Uint32 at,tc,n;
  Tile*pt;
  Tile*end;
  StatXY*r;
  int i,j;
  if(ef&0x70C0) return "Unrecognized file format";
  free(b_under);
  b_under=b_main=b_over=0;
  for(i=0;i<maxstat;i++) {
    free(stats[i].text);
    free(stats[i].xy);
  }
  free(stats);
  stats=0;
  maxstat=0;
  memset(&board_info,0,sizeof(BoardInfo));
  board_info.flag=(ef&0x100?read16(fp):read8(fp));
  board_info.screen=read16(fp);
  for(i=0;i<4;i++) if(ef&(1<<i)) board_info.exits[i]=read16(fp);
  if(ef&0x10) {
    board_info.width=read16(fp);
    board_info.height=read16(fp);
    if(!board_info.width || !board_info.height) return "Board size is zero";
  } else {
    board_info.width=read8(fp)+1;
    board_info.height=read8(fp)+1;
  }
  if(ef&0x20) board_info.userdata=read16(fp);
  maxstat=read8(fp)?:1;
  // Variable property list
  load_varproperties(fp,ef&0x800,&board_info.varprop);
  // Board grid
  if(board_info.width*(unsigned long long)board_info.height>0x100000) return "Board size is too big";
  b_under=calloc(3*sizeof(Tile),tc=board_info.width*board_info.height);
  if(!b_under) err(1,"Allocation failed");
  b_main=b_under+tc;
  b_over=b_main+tc;
  end=b_over+tc;
  // Stats
  stats=calloc(maxstat,sizeof(Stat));
  if(!stats) err(1,"Allocation failed");
  for(i=0;i<maxstat;i++) {
    sf=(ef&0x0200?read8(fp):0x0F);
    stats[i].misc1=(sf&0x02)?read16(fp):0;
    stats[i].misc2=(sf&0x04)?read16(fp):0;
    stats[i].misc3=(sf&0x08)?read16(fp):0;
    if(stats[i].length=(sf&0x01?read16(fp):0)) {
      stats[i].text=malloc(stats[i].length+1);
      if(!stats[i].text) err(1,"Allocation failed");
      fread(stats[i].text,1,stats[i].length,fp);
      stats[i].text[stats[i].length]=0;
    } else {
      stats[i].text=0;
    }
    stats[i].speed=read8(fp);
    stats[i].frame=(sf&0x10)?read16(fp):0;
    stats[i].mode=(sf&0x40)?read8(fp):0;
    if(stats[i].frame && stats[i].frame>stats[i].length-4) return "Incorrect frame offset";
    if(stats[i].count=read16(fp)) {
      r=stats[i].xy=calloc(stats[i].count,sizeof(StatXY));
      if(!r) err(1,"Allocation failed");
      for(j=0;j<stats[i].count;j++) {
        c=read8(fp);
        if(!j && (c&15)!=15) return "File format error";
        r[j].x=((c&3)==3?((board_info.width>256 || (ef&0x8000) || (stats[i].mode&STAT_INDEPENDENT))?read16(fp):read8(fp)):r[j-1].x+(c&3)-1);
        r[j].y=(((c>>2)&3)==3?((board_info.height>256 || (ef&0x8000) || (stats[i].mode&STAT_INDEPENDENT))?read16(fp):read8(fp)):r[j-1].y+((c>>2)&3)-1);
        r[j].instptr=(((c>>4)&3)==0?0:((c>>4)&3)==1?65535:((c>>4)&3)==2?r[j?j-1:0].instptr:read16(fp));
        r[j].layer=(c&0x40?read8(fp):j?r[j-1].layer:2);
        r[j].delay=(c&0x80?read8(fp):j?r[j-1].delay:0);
        r[j].sensor=(Tile){};
        r[j].frame=r[j].extra=0;
        c=r[j].layer&0x23;
        if(c && c<4 && r[j].x<board_info.width && r[j].y<board_info.height) (c==1?b_under:c==2?b_main:b_over)[r[j].y*board_info.width+r[j].x].stat=i+1;
        if(sf&0x20) {
          c=read8(fp);
          if(c&0x01) r[j].sensor.kind=read8(fp);
          if(c&0x02) r[j].sensor.color=read8(fp);
          if(c&0x04) r[j].sensor.param=read8(fp);
          if(c&0x08) r[j].sensor.stat=read8(fp);
          if(c&0x10) {
            r[j].frame=read16(fp);
            if(r[j].frame>stats[i].length || !stats[i].frame) return "Improper frame pointer";
          }
        }
      }
    } else {
      stats[i].xy=0;
    }
  }
  // Stat grid
  if(ef&0x8000) {
    pt=b_under;
    while(pt<end) {
      pt++->stat=c=read8(fp);
      if(!c) {
        n=read16(fp);
        while(n-- && pt<end) pt++->stat=0;
      }
    }
  }
  // Board grid
  *guess=1;
  for(pt=b_under;pt<end;) pt+=load_board_run(fp,pt,end,0,guess);
  memset(guess,0,256);
  for(pt=b_under;pt<end;) pt+=load_board_run(fp,pt,end,1,guess);
  memset(guess,0,256);
  for(pt=b_under;pt<end;) pt+=load_board_run(fp,pt,end,2,guess);
  if(ef&0x0400) layer_inversion();
  return 0;
}

const char*save_board(FILE*fp,int m) {
  Uint8 guess[256];
  Uint16 ef=(m?0x8600:0x0600);
  Uint16 w=board_info.width;
  Uint32 at;
  Uint32 tc=board_info.width*board_info.height;
  Tile*pt=b_under;
  Tile*end=pt+tc*3;
  int i,j;
  Uint8 c,sf;
  StatXY*r;
  Stat savedstat1={};
  Uint16 stat1frame=0;
  // Header
  if(board_info.flag&~255) ef|=0x100;
  if(board_info.exits[0]) ef|=1;
  if(board_info.exits[1]) ef|=2;
  if(board_info.exits[2]) ef|=4;
  if(board_info.exits[3]) ef|=8;
  if(board_info.width>256 || board_info.height>256) ef|=0x10;
  if(board_info.userdata) ef|=0x20;
  if(board_info.varprop.count) ef|=0x800;
  write16(fp,ef);
  if(ef&0x100) write16(fp,board_info.flag); else write8(fp,board_info.flag);
  write16(fp,board_info.screen);
  if(ef&1) write16(fp,board_info.exits[0]);
  if(ef&2) write16(fp,board_info.exits[1]);
  if(ef&4) write16(fp,board_info.exits[2]);
  if(ef&8) write16(fp,board_info.exits[3]);
  if(ef&0x10) {
    write16(fp,board_info.width);
    write16(fp,board_info.height);
  } else {
    write8(fp,board_info.width-1);
    write8(fp,board_info.height-1);
  }
  if(ef&0x20) write16(fp,board_info.userdata);
  write8(fp,maxstat);
  if(ef&0x0400) layer_inversion();
  // Variable property list
  if(ef&0x0800) save_varproperties(fp,&board_info.varprop);
  // Stats
  if(!editor && global_text && maxstat && stats->text==global_text) {
    savedstat1=*stats;
    if(stats->count) stat1frame=stats->xy->frame;
    stats->text=0;
    stats->length=0;
    stats->frame=0;
    if(stats->count) stats->xy->frame=0;
  }
  for(i=0;i<maxstat;i++) {
    if(ef&0x200) {
      sf=0;
      if(stats[i].length) sf|=0x01;
      if(stats[i].misc1) sf|=0x02;
      if(stats[i].misc2) sf|=0x04;
      if(stats[i].misc3) sf|=0x08;
      if(stats[i].frame) sf|=0x10;
      if(stats[i].mode) sf|=0x40;
      for(j=0;j<stats[i].count;j++) if(stats[i].xy[j].sensor.kind || stats[i].xy[j].sensor.color || stats[i].xy[j].sensor.param || stats[i].xy[j].sensor.stat || stats[i].xy[j].frame) {
        sf|=0x20;
        break;
      }
      write8(fp,sf);
    } else {
      sf=0x0F;
    }
    if(sf&0x02) write16(fp,stats[i].misc1);
    if(sf&0x04) write16(fp,stats[i].misc2);
    if(sf&0x08) write16(fp,stats[i].misc3);
    if(sf&0x01) {
      write16(fp,stats[i].length);
      if(stats[i].length) fwrite(stats[i].text,1,stats[i].length,fp);
    }
    write8(fp,stats[i].speed);
    if(sf&0x10) write16(fp,stats[i].frame);
    if(sf&0x40) write8(fp,stats[i].mode);
    write16(fp,stats[i].count);
    r=stats[i].xy;
    for(j=0;j<stats[i].count;j++) {
      if(j) {
        if(r[j].x==r[j-1].x) c=1; else if(r[j].x==r[j-1].x-1) c=0; else if(r[j].x==r[j-1].x+1) c=2; else c=3;
        if(r[j].y==r[j-1].y) c+=4; else if(r[j].y==r[j-1].y-1) c+=0; else if(r[j].y==r[j-1].y+1) c+=8; else c+=12;
        if(r[j].layer!=r[j-1].layer) c|=0x40;
        if(r[j].delay!=r[j-1].delay) c|=0x80;
      } else {
        c=15;
        if(r[j].layer!=2) c|=0x40;
        if(r[j].delay) c|=0x80;
      }
      if(r[j].instptr==65535) c+=0x10; else if(j && r[j].instptr==r[j-1].instptr) c+=0x20; else if(r[j].instptr) c+=0x30;
      write8(fp,c);
      if((c&0x03)==0x03) (board_info.width>256 || (ef&0x8000) || (stats[i].mode&STAT_INDEPENDENT))?write16(fp,r[j].x):write8(fp,r[j].x);
      if((c&0x0C)==0x0C) (board_info.height>256 || (ef&0x8000) || (stats[i].mode&STAT_INDEPENDENT))?write16(fp,r[j].y):write8(fp,r[j].y);
      if((c&0x30)==0x30) write16(fp,r[j].instptr);
      if(c&0x40) write8(fp,r[j].layer);
      if(c&0x80) write8(fp,r[j].delay);
      if(sf&0x20) {
        c=0;
        if(r[j].sensor.kind) c|=0x01;
        if(r[j].sensor.color) c|=0x02;
        if(r[j].sensor.param) c|=0x04;
        if(r[j].sensor.stat) c|=0x08;
        if(r[j].frame) c|=0x10;
        write8(fp,c);
        if(c&0x01) write8(fp,r[j].sensor.kind);
        if(c&0x02) write8(fp,r[j].sensor.color);
        if(c&0x04) write8(fp,r[j].sensor.param);
        if(c&0x08) write8(fp,r[j].sensor.stat);
        if(c&0x10) write16(fp,r[j].frame);
      }
    }
  }
  // Stat grid
  if(ef&0x8000) {
    pt=b_under;
    while(pt<end) {
      write8(fp,c=pt++->stat);
      if(c) continue;
      for(i=0;i<65535 && pt<end && !pt->stat;i++,pt++);
      write16(fp,i);
    }
  }
  // Board grid
  *guess=1;
  for(pt=b_under;pt<end;) pt+=save_board_run(fp,pt,end,0,guess);
  memset(guess,0,256);
  for(pt=b_under;pt<end;) pt+=save_board_run(fp,pt,end,1,guess);
  memset(guess,0,256);
  for(pt=b_under;pt<end;) pt+=save_board_run(fp,pt,end,2,guess);
  if(ef&0x0400) layer_inversion();
  // Restore saved stat1
  if(savedstat1.text) {
    *stats=savedstat1;
    if(stats->count) stats->xy->frame=stat1frame;
  }
  return 0;
}

const char*load_screen(FILE*fp) {
  Uint8 vp;
  Uint8 c;
  Uint32 at=0;
  int i,n;
  memset(&cur_screen,0,sizeof(Screen));
  vp=fgetc(fp);
  if(vp&0x7F) return "Unrecognized file format";
  cur_screen.flag=fgetc(fp);
  cur_screen.border_color=fgetc(fp);
  fread(cur_screen.border,1,4,fp);
  fread(cur_screen.soft_edge,1,4,fp);
  fread(cur_screen.hard_edge,1,4,fp);
  cur_screen.view_x=fgetc(fp);
  cur_screen.view_y=fgetc(fp);
  cur_screen.message_x=fgetc(fp);
  cur_screen.message_y=fgetc(fp);
  cur_screen.message_l=fgetc(fp);
  cur_screen.message_r=fgetc(fp);
  // Variable property list
  load_varproperties(fp,vp&0x80,&cur_screen.varprop);
  // Screen grid
  for(at=0;at<80*25;) {
    c=fgetc(fp);
    if(c<80) {
      c++;
      if(at+c>80*25) return "Out of bounds access";
      memset(cur_screen.command+at,at?cur_screen.command[at-1]:0,c);
      memset(cur_screen.color+at,at?cur_screen.color[at-1]:0,c);
      memset(cur_screen.parameter+at,at?cur_screen.parameter[at-1]:0,c);
      at+=c;
    } else if(c<160) {
      c-=79;
      if(at<80 || at+c>80*25) return "Out of bounds access";
      for(i=0;i<c;i++) {
        cur_screen.command[at+i]=cur_screen.command[at+i-80];
        cur_screen.color[at+i]=cur_screen.color[at+i-80];
        cur_screen.parameter[at+i]=cur_screen.parameter[at+i-80];
      }
      at+=c;
    } else if(c<240) {
      c-=159;
      if(at+c>80*25) return "Out of bounds access";
      memset(cur_screen.command+at,fgetc(fp),c);
      memset(cur_screen.color+at,fgetc(fp),c);
      fread(cur_screen.parameter+at,1,c,fp);
      at+=c;
    } else {
      if(c<248 && !at) return "Out of bounds access";
      cur_screen.command[at]=(c&1)?fgetc(fp):(c&8)?0:cur_screen.command[at-1];
      cur_screen.color[at]=(c&2)?fgetc(fp):(c&8)?0:cur_screen.color[at-1];
      cur_screen.parameter[at]=(c&4)?fgetc(fp):(c&8)?0:cur_screen.parameter[at-1];
      at++;
    }
  }
  return 0;
}

const char*save_screen(FILE*fp) {
  Uint8*pk=cur_screen.command;
  Uint8*pc=cur_screen.color;
  Uint8*pp=cur_screen.parameter;
  Uint8 c;
  Uint32 at=0;
  Uint8 run0,run1,run2,run3,run4;
  int i,n;
  fputc(cur_screen.varprop.count?0x80:0x00,fp);
  fputc(cur_screen.flag,fp);
  fputc(cur_screen.border_color,fp);
  fwrite(cur_screen.border,1,4,fp);
  fwrite(cur_screen.soft_edge,1,4,fp);
  fwrite(cur_screen.hard_edge,1,4,fp);
  fputc(cur_screen.view_x,fp);
  fputc(cur_screen.view_y,fp);
  fputc(cur_screen.message_x,fp);
  fputc(cur_screen.message_y,fp);
  fputc(cur_screen.message_l,fp);
  fputc(cur_screen.message_r,fp);
  // Variable property list
  if(cur_screen.varprop.count) save_varproperties(fp,&cur_screen.varprop);
  // Screen grid
  for(at=0;at<80*25;) {
    run0=run1=run2=run3=run4=0;
    for(n=0;n<80 && at+n<80*25;n++) {
      if(run0==n && at && pk[at+n]==pk[at-1] && pc[at+n]==pc[at-1] && pp[at+n]==pp[at-1]) ++run0;
      if(run1==n && at>=80 && pk[at+n]==pk[at+n-80] && pc[at+n]==pc[at+n-80] && pp[at+n]==pp[at+n-80]) ++run1;
      if(run2==n && pk[at+n]==pk[at] && pc[at+n]==pc[at]) {
        ++run2;
        if((at+n) && pp[at+n]==pp[at+n-1]) {
          if(++run3==4) run2-=4,run3=run4=0;
        } else {
          run3=0;
        }
      }
      if(run2==n+1) {
        if(at+n>=80 && pp[at+n]==pp[at+n-80]) {
          if(++run4==4) run2-=4,run3=run4=0;
        } else {
          run4=0;
        }
      }
    }
    if(run3>=run4) run2-=run3; else run2-=run4;
    if(run0>80 || run1>80 || run2>80) errx(1,"Unexpected internal error: at=%d run0=%d run1=%d run2=%d",at,run0,run1,run2);
    if(!run0 && !run1 && run2<2) {
      if(!pk[at] && !pc[at] && !pp[at]) c=0xF8;
      else if(!pk[at] && !pc[at]) c=0xFC;
      else if(!pk[at] && !pp[at]) c=0xFA;
      else if(!pc[at] && !pp[at]) c=0xF9;
      else if(at && pk[at]==pk[at-1] && pc[at]==pc[at-1]) c=0xF4;
      else if(at && pk[at]==pk[at-1] && pp[at]==pp[at-1]) c=0xF2;
      else if(at && pc[at]==pc[at-1] && pp[at]==pp[at-1]) c=0xF1;
      else if(!pk[at]) c=0xFE;
      else if(!pc[at]) c=0xFD;
      else if(!pp[at]) c=0xFB;
      else if(at && pk[at]==pk[at-1]) c=0xF6;
      else if(at && pc[at]==pc[at-1]) c=0xF5;
      else if(at && pp[at]==pp[at-1]) c=0xF3;
      else c=0xFF;
      fputc(c,fp);
      if(c&1) fputc(pk[at],fp);
      if(c&2) fputc(pc[at],fp);
      if(c&4) fputc(pp[at],fp);
      at++;
    } else if(run0 && run0>=run1) {
      fputc(run0-1,fp);
      at+=run0;
    } else if(run1) {
      fputc(run1+79,fp);
      at+=run1;
    } else {
      fputc(run2+159,fp);
      fputc(pk[at],fp);
      fputc(pc[at],fp);
      fwrite(pp+at,1,run2,fp);
      at+=run2;
    }
  }
  return 0;
}

const char*load_window(FILE*fp,WindowInfo*wind) {
  int a,c;
  wind->flag=c=fgetc(fp);
  if(c==EOF) {
    wind->flag=0;
    memset(wind->command,'X',80);
    memset(wind->color,0,80);
    memset(wind->parameter,0,80);
    return 0;
  }
  if(fgetc(fp)) return "Reserved byte has improper value";
  for(a=0;a<80;a++) {
    c=fgetc(fp);
    if(c==EOF) return "Unexpected end of file in window lump";
    if(c&0x80) {
      wind->command[a]=(c&0x1F)+0x40;
      if(c&0x20) wind->parameter[a]=fgetc(fp); else if(a) wind->parameter[a]=wind->parameter[a-1];
      if(c&0x40) wind->color[a]=fgetc(fp); else if(a) wind->color[a]=wind->color[a-1];
    } else {
      if(c>80 || !a || !c) return "Unrecognized command in window lump";
      while(c-- && a<80) {
        wind->command[a]=wind->command[a-1];
        wind->color[a]=wind->color[a-1];
        wind->parameter[a]=wind->parameter[a-1];
        if(c) a++;
      }
    }
  }
  return 0;
}

void work_varproperties(VarPropertyList*vp) {
  int i;
  VarProperty*p;
  for(i=0;i<vp->count;i++) switch((p=vp->item+i)->type) {
    case 0x00:
      if(i && !editor && vp->count>1) {
        --i;
        if(i+2<vp->count) memmove(vp->item+i,vp->item+i+2,(vp->count-i-2)*sizeof(VarProperty));
        --i;
        vp->count-=2;
      }
      break;
    case 0x04:
      if(!editor) {
        scroll_x=(p->data[0]|(p->data[1]<<8))-cur_screen.view_x;
        scroll_y=(p->data[2]|(p->data[3]<<8))-cur_screen.view_y;
      }
      break;
    case 0x11 ... 0x18:
      p->data[p->type&15]=0;
      load_font(p->data,LOADFONT_BASE);
      break;
    case 0x1F:
      if(font) memcpy(font+p->data[0]*14,p->data+1,14);
      break;
    case 0x21 ... 0x28:
      p->data[p->type&15]=0;
      load_palette(p->data,LOADPAL_BASE);
      break;
    default:
      if(!editor) errx(1,"Improper variable property list");
  }
}

