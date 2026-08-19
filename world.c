#if 0
gcc $CFLAGS -c -Wno-unused-result world.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"
#include <time.h>

Uint32 item_random_key;

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
  if(v->type==ASN1_RELATIVE_OID && v->length==3 && !memcmp(v->data,"\x04\x03",2) && v->data[2]==version_is_release) return 0;
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

#define X(aa) if(v1.constructed || v1.class || (v1.type!=ASN1_NULL && v1.type!=ASN1_INTEGER) || (v1.type==ASN1_INTEGER && asn1_decode_number(&v1,ASN1_INTEGER,&config.aa))) return 1;
#define Y(aa) if(v1.constructed || v1.class || (v1.type!=ASN1_NULL && v1.type!=ASN1_ENUMERATED) || (v1.type==ASN1_ENUMERATED && asn1_decode_number(&v1,ASN1_INTEGER,&config.aa))) return 1;
static int do_override_option(const ASN1_Value*v0) {
  ASN1_Value v1;
  if(asn1_first_of(&v1,v0)) return 0; X(speed)
  if(asn1_next_of(&v1,v0)) return 0; X(speed_fast)
  if(asn1_next_of(&v1,v0)) return 0; X(message_timer)
  if(asn1_next_of(&v1,v0)) return 0; X(menu_x)
  if(asn1_next_of(&v1,v0)) return 0; X(menu_y)
  if(asn1_next_of(&v1,v0)) return 0; Y(game_key_repeat)
  return 0;
}
#undef X
#undef Y

static int do_font_palette(const ASN1_Value*v) {
  char m[12];
  ASN1_Value a;
  if(asn1_first_of(&a,v) || a.class!=ASN1_UNIVERSAL || a.constructed || a.length>8) return 1;
  if(a.type==ASN1_VISIBLE_STRING && a.length>0) {
    memcpy(m,a.data,a.length);
    m[a.length]=0;
    if(!load_font(m,LOADFONT_BASE|LOADFONT_CHARMAP)) return 1;
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

static const char*load_item_definitions(FILE*f) {
  FILE*of;
  size_t ofs=0;
  FILE*nf;
  size_t nfs=0;
  Uint32 nfi=1;
  ASN1_Value v={};
  ASN1_Value vv={};
  ASN1_Value v1;
  ItemDef id;
  const char*e=0;
  if(asn1_read(f,&v.constructed,&v.class,&v.type,&v.length,0)) return "ASN.1 error in ITEM.DER";
  if(!v.constructed || v.type!=ASN1_SEQUENCE || v.class!=ASN1_UNIVERSAL) return "Wrong ASN.1 type in ITEM.DER";
  of=open_memstream((char**)&itemdefs,&ofs);
  nf=open_memstream((char**)&itemnames,&nfs);
  if(!of || !nf) err(1,"Allocation failed");
  fputc(0,nf);
  nitemdefs=0;
  while(!e && !asn1_read_item(f,&vv,0)) {
    nitemdefs++;
    id=(ItemDef){.weight=0xFFFFFFFFUL,.class=255};
    if(vv.class==ASN1_UNIVERSAL && vv.type==ASN1_SEQUENCE) {
      id.weight=0; id.maxheap=0xFFFFFFFFUL;
      if(asn1_first_of(&v,&vv)) goto error;
      if(v.class || (v.type!=ASN1_PC_STRING && v.type!=ASN1_OCTET_STRING) || v.length<1 || v.length>79) goto wrongtype;
      id.name=nfi; fwrite(v.data,1,v.length,nf); fputc(0,nf); nfi+=v.length+1;
      if(asn1_next_of(&v,&vv) || v.class || v.type!=ASN1_INTEGER || v.length!=1 || asn1_decode_number(&v,ASN1_INTEGER,&id.class) || (id.class&0x80)) goto error;
      if(asn1_next_of(&v,&vv) || v.class || v.type!=ASN1_BIT_STRING || v.length<1 || v.length>5) goto error;
      if(v.length>1) id.flag|=v.data[1]<<000;
      if(v.length>2) id.flag|=v.data[1]<<010;
      if(v.length>3) id.flag|=v.data[1]<<020;
      if(v.length>4) id.flag|=v.data[1]<<030;
      while(!asn1_next_of(&v,&vv)) if(v.class==ASN1_CONTEXT_SPECIFIC) switch(v.type) {
        case 0: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id.weight)) goto error; break;
        case 1: case 4: case 5:
          if(asn1_first_of(&v1,&v) || v1.class || (v1.type!=ASN1_PC_STRING && v1.type!=ASN1_OCTET_STRING) || v1.constructed) goto error;
          if(!v1.length) break;
          if(v.type==1) id.script=nfi; else if(v.type==4) id.desc=nfi; else if(v.type==5) id.appearance=nfi;
          fwrite(v1.data,1,v1.length,nf); fputc(0,nf); nfi+=v1.length+1;
          break;
        case 2: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id.maxheap)) goto error; break;
        case 3: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id.element)) goto error; break;
        case 6: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id.price)) goto error; break;
        case 7: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id.ext3)) goto error; break;
        case 8: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id.ext4)) goto error; break;
        case 9: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id.ext5)) goto error; break;
        case 10:
          if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_OCTET_STRING || v1.length<1 || v1.length>2) goto error;
          id.color=v1.data[0]; if(v1.length==2) id.parameter=v1.data[1];
          break;
        case 11: if(asn1_first_of(&v1,&v) || v1.class || v1.type!=ASN1_ENUMERATED || asn1_decode_number(&v1,ASN1_INTEGER,&id.special)) goto error; break;
        default: e="Unexpected field in ITEM.DER";
      }
    } else if(vv.class!=ASN1_UNIVERSAL || vv.type!=ASN1_NULL) {
      wrongtype: e="Wrong ASN.1 type in ITEM.DER";
    }
    fwrite(&id,1,sizeof(ItemDef),of);
    if(0) error: e="Error in ITEM.DER";
    asn1_free(&vv);
  }
  fclose(of); fclose(nf);
  if((nitemdefs && !itemdefs) || !itemnames || nfi!=nfs) err(1,"Allocation failed");
  if(!e && !editor) e=randomize_itemdefs(0);
  return e;
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
  item_random_key=time(0)^42;
  // "OPTION.DER"
  if(!editor && (fp=open_lump("OPTION.DER","r"))) {
    const char*e=load_special_options(fp);
    fclose(fp);
    if(e) return e;
  }
  end_config_special_options();
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
  if((u&0x0004) && !editor) v_mode|=VIDEO_SMZX; else v_mode&=~VIDEO_SMZX;
  if(u&~0x0007) return "Unrecognized data in START lump";
  cur_screen.message_l=222;
  cur_board_id=read16(fp);
  v=read32(fp);
  if((v&~0xFFFF) || read32(fp)) return "Unrecognized data in START lump";
  for(i=0;i<16;i++) status_vars[i]=(v&(1<<i))?0:read32(fp);
  fclose(fp);
  for(i=0;i<16;i++) namedflag[i].name[0]=0;
  for(i=0;i<4;i++) asn1reg[i]=(ASN1_Value){.type=ASN1_NULL};
  // "GENERAL.DER"
  if(fp=open_lump("GENERAL.DER","r")) {
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
      case 2: // Override option
        if(!editor && config.override_option && do_override_option(&a2)) return "Override option is incorrect";
        break;
      case 3: // PC sound
        if(!editor && config.audio_rate && world_configure_audio(&a2)) return "Error in PC sound settings in world file";
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
  // "ITEM.DER"
  free(itemdefs);
  free(itemnames);
  itemdefs=0;
  nitemdefs=0;
  itemnames=0;
  if(!editor && (fp=open_lump("ITEM.DER","r"))) {
    const char*e=load_item_definitions(fp);
    fclose(fp);
    if(e) return e;
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
  // "X?.INV"
  if(!editor) {
    const char*e;
    char buf[]="X0.INV";
    for(i=0;i<8;i++) {
      buf[1]=i+'0';
      e=load_inventory(fp=open_lump(buf,"r"),inventory+i);
      if(fp) fclose(fp);
      if(e) return e;
    }
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

static void load_zones(FILE*fp) {
  UnordZone*u;
  OrdZone*o;
  Uint32 at,n,x,y;
  Uint16 zo,v,f,ex;
  Uint8 c,w;
  // Unordered zones
  zo=read16(fp);
  for(w=0;w<16;w++) if(zo&(1<<w)) {
    f=read16(fp); ex=read16(fp); v=board_info.height>256?read16(fp):read8(fp);
    u=uzone[w]=calloc(1,(y=((v+1)*board_info.width)/8+1)+sizeof(UnordZone));
    if(!u) err(1,"Allocation failed");
    u->flag=f; u->extra=ex; u->maxy=v;
    for(x=0;x<y;) {
      u->data[x++]=c=fgetc(fp);
      if(c==0 || c==255) {
        n=fgetc(fp)&255;
        while(n-- && x<y) u->data[x++]=c;
      }
    }
  }
  // Ordered zones
  zo=read16(fp);
  for(w=0;w<16;w++) if(zo&(1<<w)) {
    f=read16(fp); ex=read16(fp); n=read16(fp);
    o=ozone[w]=malloc(sizeof(OrdZone)+n*sizeof(OrdZoneXY));
    if(!o) err(1,"Allocation failed");
    o->flag=f; o->extra=ex; o->ncells=n;
    x=y=1;
    for(v=0;v<n;) {
      c=fgetc(fp);
      for(;;) {
        f=(c>>4)&3;
        if(!f) x=board_info.width>256?read16(fp):read8(fp); else x+=f-2;
        if(x>=board_info.width) x=0;
        f=(c>>6)&3;
        if(!f) y=board_info.height>256?read16(fp):read8(fp); else y+=f-2;
        if(y>=board_info.height) y=0;
        o->xy[v].x=x; o->xy[v].y=y; v++;
        if(!(c&15)) break;
        c--;
      }
    }
  }
}

static void save_zones(FILE*fp) {
  UnordZone*u;
  OrdZone*o;
  Uint32 at,n,x,y;
  Uint16 zo,v;
  Uint8 c,d,w;
  // Unordered zones
  for(zo=w=0;w<16;w++) if(uzone[w]) zo|=1<<w;
  write16(fp,zo);
  for(w=0;w<16;w++) if(u=uzone[w]) {
    write16(fp,u->flag); write16(fp,u->extra);
    if(board_info.height>256) write16(fp,u->maxy); else write8(fp,u->maxy);
    y=((u->maxy+1)*board_info.width)/8+1;
    for(x=0;x<y;) {
      fputc(c=u->data[x++],fp);
      if(c==0 || c==255) {
        for(n=0;n<255 && x+n<y && u->data[x+n]==c;n++);
        fputc(n,fp);
        x+=n;
      }
    }
  }
  // Ordered zones
  for(zo=w=0;w<16;w++) if(ozone[w]) zo|=1<<w;
  write16(fp,zo);
  for(w=0;w<16;w++) if(o=ozone[w]) {
    write16(fp,o->flag); write16(fp,o->extra); write16(fp,o->ncells);
    x=y=1;
    for(n=v=d=0;n<=o->ncells;n++) {
      if(n!=o->ncells) {
        c=(o->xy[n].x==x-1?0x10:o->xy[n].x==x?0x20:o->xy[n].x==x+1?0x30:0x00)+(o->xy[n].y==y-1?0x40:o->xy[n].y==y?0x80:o->xy[n].y==y+1?0xC0:0x00);
        x=o->xy[n].x; y=o->xy[n].y;
      }
      if(n!=v && (c!=d || n==o->ncells || n==v+16)) {
        fputc(d+n-v-1,fp);
        while(v<n) {
          if(!(d&0x30)) board_info.width>256?write16(fp,o->xy[v].x):write8(fp,o->xy[v].x);
          if(!(d&0xC0)) board_info.height>256?write16(fp,o->xy[v].y):write8(fp,o->xy[v].y);
          v++;
        }
      }
      d=c;
    }
  }
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
  if(ef&0x7040) return "Unrecognized file format";
  if(feof(fp)) return "Input past end of file";
  free(b_under);
  b_under=b_main=b_over=0;
  for(i=0;i<maxstat;i++) {
    free(stats[i].text);
    free(stats[i].xy);
  }
  free(stats);
  stats=0;
  maxstat=0;
  for(i=0;i<16;i++) {
    free(ozone[i]); ozone[i]=0;
    free(uzone[i]); uzone[i]=0;
  }
  free(board_info.varprop.item);
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
  maxstat=read8(fp);
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
  if(!maxstat) goto nostats;
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
    stats[i].zone=(stats[i].mode&STAT_ZONERESTRICT)?read8(fp):0;
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
          if(c&0x20) r[j].extra=read16(fp);
        }
      }
    } else {
      stats[i].xy=0;
    }
  }
  nostats:
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
  // Zones
  if(ef&0x80) load_zones(fp);
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
  for(i=0;i<16 && !(ef&0x80);i++) if(ozone[i] || uzone[i]) ef|=0x80;
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
      for(j=0;j<stats[i].count;j++) if(stats[i].xy[j].sensor.kind || stats[i].xy[j].sensor.color || stats[i].xy[j].sensor.param || stats[i].xy[j].sensor.stat || stats[i].xy[j].frame || stats[i].xy[j].extra) {
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
    if(stats[i].mode&STAT_ZONERESTRICT) write8(fp,stats[i].zone);
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
        if(r[j].extra) c|=0x20;
        write8(fp,c);
        if(c&0x01) write8(fp,r[j].sensor.kind);
        if(c&0x02) write8(fp,r[j].sensor.color);
        if(c&0x04) write8(fp,r[j].sensor.param);
        if(c&0x08) write8(fp,r[j].sensor.stat);
        if(c&0x10) write16(fp,r[j].frame);
        if(c&0x20) write16(fp,r[j].extra);
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
  // Zones
  if(ef&0x80) save_zones(fp);
  // Restore saved stat1
  if(savedstat1.text) {
    *stats=savedstat1;
    if(stats->count) stats->xy->frame=stat1frame;
  }
  return 0;
}

const char*load_screen(FILE*fp) {
  Uint8 vp;
  Uint8 x,y,z,c;
  Uint32 at=0;
  int i,n;
  free(cur_screen.varprop.item);
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
    } else if(c==240) {
      return "Reserved opcode in screen definition";
    } else if(c==247) {
      x=fgetc(fp); y=(x&127)/7; z=(x&127)%7; x>>=7;
      if(y==18) return "Reserved opcode in screen definition";
      if((at<80 && z>3) || (!at && (y<3 || z))) return "Out of bounds access";
      cur_screen.command[at]=(z==0||z==2||z==5)?fgetc(fp):cur_screen.command[at-(z<4?1:80)];
      cur_screen.color[at]=(z==0||z==1||z==4)?fgetc(fp):cur_screen.color[at-(z<4?1:80)];
      cur_screen.parameter[at]=y>2?fgetc(fp):cur_screen.parameter[at-(z<4?1:80)]+(x?1:-1);
      if(!y) y=3;
      for(i=1;i<y && at+i<80*25;i++) {
        cur_screen.command[at+i]=cur_screen.command[at];
        cur_screen.color[at+i]=cur_screen.color[at];
        cur_screen.parameter[at+i]=cur_screen.parameter[at+i-1]+(x?1:-1);
      }
      at+=y;
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
  Uint8 run0,run1,run2,run3,run4,run5,run6;
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
    run0=run1=run2=run3=run4=0; run5=run6=1;
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
        if(at+n>=80 && pk[at+n]==pk[at+n-80] && pc[at+n]==pc[at+n-80] && pp[at+n]==pp[at+n-80]) {
          if(++run4==4) run2-=4,run3=run4=0;
        } else {
          run4=0;
        }
      }
      if(n<17) {
        if(run5==n && pk[at+n]==pk[at] && pc[at+n]==pc[at] && pp[at+n]==((pp[at+n-1]+1)&255)) ++run5;
        if(run6==n && pk[at+n]==pk[at] && pc[at+n]==pc[at] && pp[at+n]==((pp[at+n-1]-1)&255)) ++run6;
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
    } else if(run5>1 && run5>run1) {
      if(at && pk[at]==pk[at-1] && pc[at]==pc[at-1]) c=3;
      else if(at>=80 && pk[at]==pk[at-80] && pc[at]==pc[at-80]) c=6;
      else if(at && pk[at]==pk[at-1]) c=1;
      else if(at && pc[at]==pc[at-1]) c=2;
      else if(at>=80 && pk[at]==pk[at-80]) c=4;
      else if(at>=80 && pc[at]==pc[at-80]) c=5;
      else c=0;
      if(run5<=3) {
        if(!c) goto not247;
        if(c>3 && pp[at]!=pp[at-80]+1) {
          if(pp[at]==pp[at-1]+1) {
            if(pk[at]==pk[at-1]) c=1; else if(pc[at]==pc[at-1]) c=2; else if(run5==3) c=0;
          }
          if(run6<3) goto not247;
        }
        if(run5>run6 && !c) goto not247;
        if(run5==3 && pp[at]==pp[at-(c>3?80:1)]+1) run5=0;
        if(run5!=3 && pp[at]!=pp[at-(c>3?80:1)]+1) goto not247;
      }
      fputc(247,fp); fputc(c+7*run5+128,fp);
      if(c==0 || c==2 || c==5) fputc(pk[at],fp);
      if(c==0 || c==1 || c==4) fputc(pc[at],fp);
      if(run5>2) fputc(pp[at],fp);
      at+=run5?:3;
    } else if(run6>1 && run6>run1) {
      if(at && pk[at]==pk[at-1] && pc[at]==pc[at-1]) c=3;
      else if(at>=80 && pk[at]==pk[at-80] && pc[at]==pc[at-80]) c=6;
      else if(at && pk[at]==pk[at-1]) c=1;
      else if(at && pc[at]==pc[at-1]) c=2;
      else if(at>=80 && pk[at]==pk[at-80]) c=4;
      else if(at>=80 && pc[at]==pc[at-80]) c=5;
      else c=0;
      if(run6<=3) {
        if(!c) goto not247;
        if(c>3 && pp[at]!=pp[at-80]-1) {
          if(pp[at]==pp[at-1]-1) {
            if(pk[at]==pk[at-1]) c=1; else if(pc[at]==pc[at-1]) c=2; else if(run6==3) c=0;
          }
          if(run6<3) goto not247;
        }
        if(run2>run6 && !c) goto not247;
        if(run6==3 && pp[at]==pp[at-(c>3?80:1)]-1) run6=0;
        if(run6!=3 && pp[at]!=pp[at-(c>3?80:1)]-1) goto not247;
      }
      fputc(247,fp); fputc(c+7*run6,fp);
      if(c==0 || c==2 || c==5) fputc(pk[at],fp);
      if(c==0 || c==1 || c==4) fputc(pc[at],fp);
      if(run6>2) fputc(pp[at],fp);
      at+=run6?:3;
    } else not247: if(run1) {
      fputc(run1+79,fp);
      at+=run1;
    } else {
      for(n=1;n<run2-3;n++) {
        if(pp[at+n+1]==pp[at+n]+1 && pp[at+n+2]==pp[at+n]+2 && pp[at+n+3]==pp[at+n]+3) run2=n;
        if(pp[at+n+1]==pp[at+n]-1 && pp[at+n+2]==pp[at+n]-2 && pp[at+n+3]==pp[at+n]-3) run2=n;
      }
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
  int a,b,c;
  memset(wind->wcolor,0,16);
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
    } else if(c==0x7F) {
      c=read16(fp);
      for(b=0;b<16;b++) if(c&(1<<b)) wind->wcolor[b]=fgetc(fp);
      a--;
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
  int i,j;
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
    case 0x01: case 0x02:
      j=(editor?VIDEO_SMZX|VIDEO_FLASHY:VIDEO_SMZX|VIDEO_80COLUMNS|VIDEO_FLASHY|VIDEO_EGS|VIDEO_MONO);
      v_mode=(v_mode&~j)|(p->data[0]&j);
      v_colormask=(p->type==0x01?(v_mode&VIDEO_SMZX?0x80:0x30):(p->data[1]<0x30?0x30:p->data[1]));
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
    case 0x32 ... 0x3F:
      /* Do nothing; this case is handled by a separate function */
      break;
    case 0x40:
      if(!editor) audio_set_music(0,0);
      break;
    case 0x42 ... 0x4B:
      p->data[p->type&15]=0;
      if(!editor) {
        int s=(p->data[0]&0x3F);
        int k=1;
        if(s==0x3F) k=3,s=p->data[1]|(p->data[2]<<8);
        if(!*music_name || (p->data[0]>>6)==3 || ((p->data[0]&0xC0) && (((p->data[0]&0x80) && s!=music_song) || strcmp(music_name,p->data+k)))) audio_set_music(p->data+k,s);
      }
      break;
    case 0x50 ... 0x5E:
      /* No effect */
      break;
    case 0x60 ... 0x6E:
      p->data[p->type&15]=0;
      if(!editor) set_backdrop(p->data,1);
      break;
    default:
      if(!editor) errx(1,"Improper variable property list");
  }
}

const char*load_inventory(FILE*fp,Inventory*inv) {
  int c,m,n;
  free(inv->item);
  *inv=(Inventory){};
  if(!fp) return 0;
  m=read8(fp);
  if(m<=0) return 0;
  if(m<2 || m>14 || ((1UL<<m)&0b010101110111011UL)) return "Improper header size in .INV lump";
  inv->count=read16(fp);
  inv->maxheap=(m>2?read32(fp):0x7FFFFFFFL);
  inv->strength=(m>6?read32(fp):0xFFFFFFFFL);
  inv->flag=(m>10?read16(fp):0x0000);
  inv->cursor=(m>12?read16(fp):0x0000);
  inv->item=calloc(inv->count,sizeof(ItemSlot));
  if(inv->count && !inv->item) err(1,"Allocation failed");
  for(n=0;n<inv->count;) switch(c=read8(fp)) {
    case 0x00 ... 0x3F: n+=c+1; break;
    case 0x80 ... 0xBF:
      inv->item[n].item=read16(fp);
      inv->item[n].quantity=((c&3)==0?(inv->item[n].item?1:0):(c&3)==1?read8(fp):(c&3)==2?read16(fp):read32(fp));
      if(c&4) inv->item[n].flag=read16(fp);
      if(c&8) inv->item[n].ext0=read32(fp);
      if(c&16) inv->item[n].ext1=read16(fp);
      if(c&32) inv->item[n].ext2=read16(fp);
      n++; break;
    default: return "Improper command in .INV lump";
  }
  return 0;
}

const char*save_inventory(FILE*fp,Inventory*inv) {
  int m,n;
  if(!fp) return 0;
  if(!inv->count && !inv->flag && !inv->maxheap && !inv->strength) return 0;
  write8(fp,14);
  write16(fp,inv->count);
  write32(fp,inv->maxheap);
  write32(fp,inv->strength);
  write16(fp,inv->flag);
  write16(fp,inv->cursor);
  for(m=n=0;n<inv->count;n++) {
    if(inv->item[n].item|inv->item[n].quantity|inv->item[n].flag|inv->item[n].ext0|inv->item[n].ext1|inv->item[n].ext2) {
      if(m) write8(fp,m-1);
      m=128;
      m|=(inv->item[n].quantity==(inv->item[n].item?1:0)?0:inv->item[n].quantity&~0xFFFF?3:inv->item[n].quantity&~0xFF?2:1);
      if(inv->item[n].flag) m|=4;
      if(inv->item[n].ext0) m|=8;
      if(inv->item[n].ext1) m|=16;
      if(inv->item[n].ext2) m|=32;
      write8(fp,m);
      write16(fp,inv->item[n].item);
      switch(m&3) {
        case 1: write8(fp,inv->item[n].quantity); break;
        case 2: write16(fp,inv->item[n].quantity); break;
        case 3: write32(fp,inv->item[n].quantity); break;
      }
      if(m&4) write16(fp,inv->item[n].flag);
      if(m&8) write32(fp,inv->item[n].ext0);
      if(m&16) write16(fp,inv->item[n].ext1);
      if(m&32) write16(fp,inv->item[n].ext2);
      m=0;
    } else {
      if(m==64) write8(fp,m-1),m=0;
      m++;
    }
  }
  if(m) write8(fp,m-1);
  return 0;
}

#define CBRANDOM_KEY 7333142306270100471ULL
static inline Uint32 cbrandom2(Uint32 n,Uint64 c) {
  // Square RNG counter-based random numbers.
  // Used for randomizing item definitions.
  Uint32 m=n-1;
  Uint32 r;
  Uint32 k=item_random_key&(c+42);
  m|=m>>1; m|=m>>2; m|=m>>4; m|=m>>8; m|=m>>16;
  for(;;) {
    Uint64 x=CBRANDOM_KEY*c;
    Uint64 y=x;
    Uint64 z=CBRANDOM_KEY+y;
    x=x*x+y; x=(x>>32)|(x<<32);
    x=x*x+z; x=(x>>32)|(x<<32);
    x^=((Uint64)item_random_key)<<29;
    x=x*x+y; x=(x>>32)|(x<<32);
    r=(k+((x*x+z)>>32))&m;
    if(r<n) return r;
    c++; k++;
  }
}

static inline const char*itemrand1(Uint8*d,Uint8 rev,Uint64 key) {
  ItemDef*it;
  ItemDef*it2;
  Uint16*list=0;
  Uint16*mix=0;
  size_t nlist=0;
  FILE*flist=0;
  Uint32 q,r;
  Uint16 k,m;
  Uint16 n=0;
  if((d[0]|d[1])&0x80) return "Error in item randomization";
  flist=open_memstream((char**)&list,&nlist);
  if(!flist) err(1,"Unexpected error");
  do {
    it=itemdefs+n;
    if(it->class==255 || (it->flag&IDF_NO_RANDOMIZE)) continue;
    if(it->class<d[0] || it->class>d[1]) continue;
    if((d[6]&0x80) && !it->appearance) continue;
    if((d[2]|(d[3]<<8))&~(it->flag>>16)) continue;
    if((d[4]|(d[5]<<8))&(it->flag>>16)) continue;
    fwrite(&n,sizeof(Uint16),1,flist);
  } while(++n!=nitemdefs);
  fclose(flist);
  if(nlist && !list) err(1,"Unexpected error");
  if(!nlist || (nlist<2 && !d[8])) { free(list); return 0; }
  mix=malloc(nlist);
  if(!mix) err(1,"Allocation failed");
  nlist/=sizeof(Uint16);
  for(n=0;n<nlist;n++) mix[n]=n;
  for(m=nlist;m>1;m--) {
    n=cbrandom2(m,key+=123456789ULL);
    k=mix[n]; mix[n]=mix[m-1]; mix[m-1]=k;
  }
  q=(d[10]<<16)|(d[11]<<24);
  for(n=0;n<nlist;n++) {
    m=(rev?nlist-n-1:n);
    if(m==mix[m]) continue;
    it=itemdefs+list[m]; it2=itemdefs+list[mix[m]];
    r=it->flag&q; it->flag&=~q; it->flag|=it2->flag&q; it2->flag&=~q; it2->flag|=r;
    if(d[9]&0x01) r=it->ext5,it->ext5=it2->ext5,it2->ext5=r;
    if(d[9]&0x02) r=it->ext4,it->ext4=it2->ext4,it2->ext4=r;
    if(d[9]&0x04) r=it->ext3,it->ext3=it2->ext3,it2->ext3=r;
    if(d[9]&0x08) r=it->parameter,it->parameter=it2->parameter,it2->parameter=r;
    if(d[9]&0x10) r=it->color,it->color=it2->color,it2->color=r;
    if(d[9]&0x20) r=it->element,it->element=it2->element,it2->element=r;
    if(d[9]&0x40) r=it->name,it->name=it2->name,it2->name=r;
    if(d[9]&0x80) r=it->appearance,it->appearance=it2->appearance,it2->appearance=r;
  }
  free(list);
  free(mix);
  return 0;
}

const char*randomize_itemdefs(Uint8 rev) {
  const char*e=0;
  Uint64 key;
  Uint32 n,s;
  Uint8 d[16];
  FILE*f;
  if(config.test_mode) {
    setbuf(stdout,0);
    putchar(rev+'0'); putchar(' ');
    f=popen("sha1sum","w");
    fwrite(itemdefs,sizeof(ItemDef),nitemdefs,f);
    pclose(f);
  }
  if(nitemdefs<2) return 0;
  f=open_lump("ITEMRAND","r");
  if(!f) return 0;
  s=lump_size/16;
  for(n=0;n<s && !e;n++) {
    if(rev) fseek(f,key=(s-n-1)*16LL,SEEK_SET); else key=n*16LL;
    fread(d,1,16,f);
    e=itemrand1(d,rev,key);
  }
  fclose(f);
  if(config.test_mode) {
    putchar('+'); putchar(' ');
    f=popen("sha1sum","w");
    fwrite(itemdefs,sizeof(ItemDef),nitemdefs,f);
    pclose(f);
  }
  return e;
}

static const ASN1_Value*bit_v;
static Uint32 bit_p;

static inline Uint8 bit_read(void) {
  Uint8 c=bit_v->data[bit_p>>3]&(128>>(bit_p&7));
  bit_p++;
  return c?1:0;
}

static inline Uint8 bit_eof(void) {
  Uint32 p=bit_p>>3;
  return (p>=bit_v->length || (p==bit_v->length-1 && (bit_p&7)>=8-bit_v->data[0]));
}

typedef struct {
  Sint16 t0[260];
  Sint16 t1[260];
  Uint16 len[260];
  Uint16 p;
  Uint8 v,rle;
} Huff;

static Sint16 read_huffman_tree(Huff*h,Uint16 n) {
  Sint16 r;
  if(h->p==260) errx(1,"Too many nodes in Huffman tree in backdrop data");
  if(bit_eof()) errx(1,"Unexpected end of bit string in backdrop data");
  if(bit_read()) {
    // 1**
    r=h->p++;
    h->t0[r]=read_huffman_tree(h,n+1);
    h->t1[r]=read_huffman_tree(h,n+1);
    return ~r;
  } else {
    if(bit_read()) {
      if(bit_read()) {
        if(bit_read()) {
          // 0111__
          r=bit_read()<<1; r|=bit_read();
          h->rle|=1<<r;
          h->len[r+256]=n;
          return r+256;
        } else {
          // 0110________
          r=0;
          b8: r|=bit_read()<<7;
          b7: r|=bit_read()<<6;
          b6: r|=bit_read()<<5;
          b5: r|=bit_read()<<4;
          b4: r|=bit_read()<<3;
          b3: r|=bit_read()<<2;
          b2: r|=bit_read()<<1;
          b1: r|=bit_read()<<0;
        }
      } else {
        // 010____
        r=h->v&0xF0; goto b4;
      }
    } else {
      if(bit_read()) {
        // 001_______
        r=h->v&0x80; goto b7;
      } else {
        // 000______
        r=h->v&0xC0; goto b6;
      }
    }
    h->len[r]=n;
    return h->v=r;
  }
}

static Uint32 rle_number(Uint8 b) {
  Uint32 n=0;
  Uint8 k=0;
  Uint8 m,v;
  do {
    for(v=1,m=0;m<b;m++) if(bit_read()) v+=1<<m;
    n+=((Uint32)v)<<k;
    k+=b;
  } while(bit_read());
  return n;
}

static Uint32 unhuff_backdrop(Uint8*output,Uint16 width) {
  Huff h={{},{},{},0,0x40,0};
  Uint8 fil[31]; // filters
  Uint8 va[256];
  Uint8 nfil=0;
  Uint8 rleb;
  Uint32 t=0; // total number of pixels
  Uint32 u,v,y;
  Sint16 b;
  while(bit_read()) {
    if(bit_eof()) errx(1,"Unexpected end of bit string in backdrop data");
    if(nfil==31) errx(1,"Too many filters in backdrop data");
    fil[nfil]=bit_read();
    if(bit_read()) fil[nfil]+=2;
    if(bit_read()) fil[nfil]+=4;
    nfil++;
  }
  if(read_huffman_tree(&h,0)>=0) errx(1,"Improper Huffman tree in backdrop data");
  if(h.rle) {
    rleb=bit_read()<<1; rleb|=bit_read();
    if(!rleb) rleb=4;
  }
  for(b=0;b<256;b++) va[b]=b;
  while(!bit_eof()) {
    if(t==640*350) errx(1,"Improper data size in backdrop");
    for(b=-1;b<0;) b=(bit_read()?h.t1[~b]:h.t0[~b]);
    if(b<256) {
      if(t>=width && b!=output[t-width]) va[output[t-width]]=b;
      output[t++]=b;
    } else if(b==256) {
      b=output[t-1];
      u=rle_number(rleb)+(h.len[256]+rleb+1)/h.len[b];
      while(u-- && t!=640*350) output[t++]=b;
    } else if(b==257) {
      u=rle_number(rleb);
      if(h.len[257]+rleb+1>=h.len[output[t-width]]) u++;
      while(u-- && t!=640*350) output[t]=output[t-width],t++;
    } else if(b==258) {
      v=rle_number(rleb)+1; u=rle_number(rleb)+1;
      if(v>=width) v++;
      while(u-- && t!=640*350) output[t]=output[t-v],t++;
    } else if(b==259) {
      u=rle_number(1)+1;
      while(u-- && t!=640*350) output[t]=va[output[t-width]],t++;
    }
  }
  while(nfil--) switch(fil[nfil]) {
    case 1: // Reverse
      for(u=0;u<t/2;u++) b=output[u],output[u]=output[t-u-1],output[t-u-1]=b;
      break;
    case 2: // XOR vertical
      for(u=width;u<t;u++) output[u]^=output[u-width];
      break;
    case 3: // XOR horizontal
      for(u=1;u<t;u++) output[u]^=output[u-1];
      break;
    case 6: // LOCO-I prediction
      for(u=1;u<t;u++) {
        b=output[u%width?u-1:u-width]; v=output[u>=width?u-width:u-1]; y=output[u>=width+1?u-width-1:u-1];
        if(y>=b && y>=v) v=(b<v?b:v); else if(y<=b && y<=v) v=(b>v?b:v); else v=b+v-y;
        output[u]+=v;
      }
      break;
    default: errx(1,"Unrecognized filter in backdrop data");
  }
  return t;
}

static const char*decode_backdrop(Uint8*output,const ASN1_Value*input,Uint16 width) {
  Uint32 t,x,y;
  if(input->type==ASN1_OCTET_STRING) {
    t=input->length;
    if(t>width*350L) {
      return "Improper data size in backdrop";
    } else if(t==1) {
      memset(output,input->data[0],640*350);
      return 0;
    } else {
      memcpy(output+y,input->data,t);
    }
  } else if(input->type==ASN1_BIT_STRING) {
    bit_v=input; bit_p=8;
    t=unhuff_backdrop(output,width);
    if(!t) return "Backdrop data is empty";
  } else {
    return "Incorrect ASN.1 type in backdrop";
  }
  if(t!=640*350) {
    for(y=0;y<640*350;y+=t) {
      x=640*350-y; if(x>t) x=t;
      memcpy(output+y,output,x);
    }
  }
  if(width!=640) {
    for(y=349;y;y--) memmove(output+y*640,output+(width*y)%t,width);
    for(y=0;y<350;y++) for(x=width;x<640;x+=width) {
      t=640-x; if(t>width) t=width;
      memcpy(output+y*640+x,output+y*640,t);
    }
  }
  return 0;
}

const char*set_backdrop(const char*name,char usepal) {
  Uint8 buf[16];
  const char*er=0;
  FILE*f;
  ASN1_Value v0,v1;
  int i;
  Uint16 wid=640;
  free(backdrop_p); free(backdrop_z);
  backdrop_p=0; backdrop_z=0;
  if(!name || !*name) {
    *backdrop_name=0;
    return 0;
  }
  snprintf(backdrop_name,9,"%s",name);
  for(i=0;name[i] && i<8;i++) {
    if(name[i]=='.' || name[i]<33) break;
    buf[i]=name[i];
  }
  buf[i]='.'; buf[i+1]='R'; buf[i+2]='O'; buf[i+3]='P'; buf[i+4]=0;
  f=open_lump(buf,"r");
  if(!f) return "Backdrop not found";
  if(asn1_read_item(f,&v0,0)) {
    fclose(f);
    return "ASN.1 error in backdrop lump";
  }
  fclose(f);
  if(v0.class!=ASN1_UNIVERSAL || v0.type!=ASN1_SEQUENCE) {
    asn1type: er="Incorrect ASN.1 type in backdrop"; goto stop;
  }
  if(asn1_first_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL) goto asn1type;
  if(v1.type==ASN1_INTEGER) {
    if(asn1_decode_number(&v1,ASN1_INTEGER,&wid) || wid<1 || wid>640) { er="Improper width in backdrop"; goto stop; }
    if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL) goto asn1type;
  }
  if(v1.constructed || !v1.length) goto asn1type;
  backdrop_p=calloc(640,350);
  backdrop_z=calloc(640,350);
  if(!backdrop_p || !backdrop_z) err(1,"Allocation failed");
  if(er=decode_backdrop(backdrop_p,&v1,wid)) goto stop;
  if(asn1_next_of(&v1,&v0)) goto stop;
  while(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_VISIBLE_STRING && v1.length>0 && v1.length<=8) {
    memcpy(buf,v1.data,v1.length);
    buf[v1.length]=0;
    load_palette(buf,LOADPAL_BASE);
    if(asn1_next_of(&v1,&v0)) goto stop;
  }
  if(v1.class!=ASN1_UNIVERSAL) goto asn1type;
  if(er=decode_backdrop(backdrop_z,&v1,wid)) goto stop;
  if(!asn1_next_of(&v1,&v0)) er="Too many fields in backdrop data";
  stop:
  asn1_free(&v0);
  if(er) {
    free(backdrop_p); free(backdrop_z);
    backdrop_p=0; backdrop_z=0;
  }
  return er;
}
