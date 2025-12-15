#if 0
gcc $CFLAGS -c -Wno-unused-result savegame.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>

static char*savename;

static char init_savegame(void) {
  FILE*fp;
  static char ok=0;
  if(ok) return 0;
  if(fp=open_lump("!SZ0","r+")) {
    fputc(0,fp);
    fclose(fp);
  } else if(config.version_check) {
    goto error;
  }
  if(config.save_dir && config.save_dir[0]) {
    if(config.save_dir[0]=='!' && !config.save_dir[1]) {
      char*v;
      int n;
      if(!world_name) goto error;
      n=strlen(world_name);
      if(n>4 && world_name[n-4]=='.' && (world_name[n-3]=='S' || world_name[n-3]=='s') && (world_name[n-2]=='Z' || world_name[n-2]=='z') && world_name[n-1]=='0') {
        v=malloc(n+3);
        if(!v) errx(1,"Allocation failed");
        memcpy(v,world_name,n-3);
        v[n-3]='s'; v[n-2]='a'; v[n-1]='v'; v[n]='e'; v[n+1]=0;
        if(mkdir(v,0777) && errno!=EEXIST) warn("Cannot create directory");
        if(chdir(v)) warn("Cannot change directory");
        free(v);
      }
    } else if(config.save_dir[0]=='~' && config.save_dir[1]=='/') {
      char*v=getenv("HOME");
      if(v) {
        if(chdir(v)) {
          warn("Cannot change to home directory");
        } else if(chdir(config.save_dir+2)) {
          warn("Cannot change directory");
        }
      } else {
        warnx("Cannot determine home directory");
      }
    } else {
      if(chdir(config.save_dir)) warn("Cannot change directory");
    }
  }
  ok=1;
  return 0;
  error: alert_text("Error initializing save game data"); return 1;
}

typedef struct {
  char name[81];
  char isdir;
  time_t mtime;
} FileItem;

static int compare_file_items(const void*a,const void*b) {
  const FileItem*x=a;
  const FileItem*y=b;
  Uint8 m=(config.file_list&0x80?1:config.file_list);
  if((m&7)==5) m-=4;
  if((m&2) && x->isdir!=y->isdir) return (x->isdir^m)&1?:-1;
  if((m&4) && ((!x->isdir && !y->isdir) || (m&3)==1) && x->mtime!=y->mtime) return x->mtime>y->mtime?1:-1;
  return strcmp(x->name,y->name);
}

int ask_save_file(char issave) {
  Uint8 vm=v_mode;
  FILE*fp=0;
  DIR*dir=0;
  struct dirent*ent;
  struct stat st;
  FileItem ite;
  FileItem*items=0;
  size_t nitems=0;
  char entry[81];
  char pattern[81]="*.sav";
  char buf[81];
  char*q;
  Uint8 xc;
  int i,j,k,yc,ys;
  set_timer(0);
  v_status[1]="RS"[issave];
  if(init_savegame()) return 0;
  config.file_list&=0x7F;
  if(config.file_list&32) pattern[1]=0;
  if(savename) {
    xc=snprintf(entry,81,"%s",savename);
  } else {
    *entry=xc=0;
  }
  v_mode|=VIDEO_80COLUMNS;
  list:
  yc=ys=0;
  free(items);
  items=0;
  nitems=0;
  dir=opendir(".");
  if(dir) {
    fp=open_memstream((char**)&items,&nitems);
    if(!fp) err(1,"Unexpected error");
    while(ent=readdir(dir)) {
      if(strlen(ent->d_name)>80 || stat(ent->d_name,&st)) continue;
      if(!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;
      ite.isdir=(S_ISDIR(st.st_mode)?1:0);
      if(ite.isdir && !(config.file_list&0x83)) continue;
      if(ite.isdir && ent->d_name[0]=='.' && (!(config.file_list&8) || !ent->d_name[1])) continue;
      if(!ite.isdir && (config.file_list&0x80)) continue;
      if(!ite.isdir && fnmatch(pattern,ent->d_name,(config.file_list&8?0:FNM_PERIOD)|FNM_NOESCAPE)) continue;
      snprintf(ite.name,81,"%s",ent->d_name);
      ite.mtime=st.st_mtime;
      fwrite(&ite,1,sizeof(FileItem),fp);
    }
    closedir(dir);
    fclose(fp);
    if(nitems && !items) err(1,"Unexpected error");
    qsort(items,yc=nitems/=sizeof(FileItem),sizeof(FileItem),compare_file_items);
  } else {
    snprintf(buf,78,"%m");
    alert_text(buf);
  }
  redraw0:
  draw_border(0x19,0,0,79,24);
  draw_text(issave?35:33,0,issave?" Save Game ":" Restore Game ",0x1B,-1);
  draw_text(3,24,"F1=Parent F2=Order F3=Dirs/Files F4=Mkdir F5=Refresh RET=Accept ESC=Cancel",0x13,-1);
  if(getcwd(buf,78) && *buf) {
    i=strlen(buf);
    if(config.file_list&0x80) snprintf(buf+i,77-i,"...");
    else if(i<76) snprintf(buf+i,77-i,"%s%s",i && buf[i-1]=='/'?"":"/",pattern);
    draw_text(2,2,buf,0x17,76);
    if(i>76) v_color[158]=0x14,v_char[158]='>';
  }
  draw_border(0,2,3,77,23);
  for(i=0;i<21;i++) {
    if(i+ys<nitems) {
      if(i+ys==yc) memset(v_color+i*80+242,0x22,76);
      j=draw_text(items[i+ys].isdir+2,i+3,items[i+ys].name,(items[i+ys].isdir?0x0D:0x0F)+(i+ys==yc?0x20:0x00),72);
      if(items[i+ys].isdir) {
        draw_text(72,i+3,"<DIR>",(i+ys==yc?0x2B:0x0B),5);
        v_color[(i+3)*80+j]=v_color[(i+3)*80+2]=(i+ys==yc?0x2C:0x0C);
        v_char[(i+3)*80+2]='['; v_char[(i+3)*80+j]=']';
      } else if(items[i+ys].mtime) {
        struct tm tm;
        if(config.time_utc) gmtime_r((const time_t*)&items[i+ys].mtime,&tm); else localtime_r((const time_t*)&items[i+ys].mtime,&tm);
        if(j=strftime(buf,41,config.time_format,&tm)) draw_text(77-j,i+3,buf,(i+ys==yc?0x2B:0x0B),j);
      }
    } else if(!i) {
      draw_text(2,i+3,"(Not found)",8,-1);
    }
  }
  if(ys) v_color[318]=0x15,v_char[318]=30;
  if(nitems>ys+21) v_color[1918]=0x15,v_char[1918]=31;
  redraw1:
  for(i=0;i<76 && entry[i];i++) {
    v_color[i+82]=0x0F;
    v_char[i+82]=entry[i];
  }
  xc=i;
  if(i<76) {
    v_color[i+82]=0x0B;
    v_char[i+82]=177;
    for(i++;i<76;i++) {
      v_color[i+82]=0x08;
      v_char[i+82]=250;
    }
  }
  redisplay();
  do { if(!next_event()) errx(0,"No events available."); } while(event.type!=SDL_KEYDOWN);
  switch(event.key.keysym.sym) {
    case SDLK_ESCAPE: escape:
      free(items);
      v_mode=vm;
      return 0;
    case SDLK_F1: config.file_list&=0x7F; chdir(".."); goto list;
    case SDLK_F2: config.file_list^=4; config.file_list&=0x7F; goto list;
    case SDLK_F3: config.file_list^=0x80; goto list;
    case SDLK_F4:
      *entry=*buf=0;
      ask_text("Name of new directory?",buf,70);
      if(*buf) {
        if(mkdir(buf,0777)) {
          alert_text(strerror(errno));
        } else {
          chdir(buf);
          config.file_list&=0x7F;
        }
      }
      goto list;
    case SDLK_F5: *entry=0; goto list;
    case SDLK_HOME: yc=ys=0; goto moved;
    case SDLK_END: yc=nitems-1; goto moved;
    case SDLK_UP: if(yc==nitems) yc=0; else yc--; goto moved;
    case SDLK_DOWN: if(yc==nitems) yc=0; else yc++; goto moved;
    case SDLK_PAGEUP: if(yc==nitems) break; ys-=20; yc-=20; goto moved;
    case SDLK_PAGEDOWN: ys+=20; yc+=20; goto moved;
    default:
      i=event.key.keysym.unicode;
      if(i>32 && i<256 && i!=127 && xc<76) {
        entry[xc++]=i;
        entry[xc]=0;
      } else if(i==8 && xc) {
        entry[--xc]=0;
      } else if(i==21) {
        entry[xc=0]=0;
      } else if(i==27) {
        goto escape;
      } else if(i==9 && nitems && (!(config.file_list&0x86) || config.file_list>=0x80)) {
        for(i=0;i<nitems;i++) if(!strncmp(entry,items[i].name,xc)) break;
        if(i==nitems) break;
        yc=i;
        if(yc<0) yc=0;
        if(yc>=nitems) yc=nitems-1;
        if(nitems<21) ys=0; else if(ys>nitems-21) ys=nitems-21;
        if(ys<yc-20) ys=yc-20;
        if(ys>yc) ys=yc;
        if(ys<0) ys=0;
        k=strlen(items[i].name);
        for(j=yc+1;j<nitems;j++) {
          if(strncmp(entry,items[j].name,xc)) break;
          for(i=xc;i<75 && i<k && items[j].name[i] && items[j].name[i]==items[yc].name[i];i++);
          if(i<k) k=i;
        }
        if(k>xc) {
          memcpy(entry+xc,items[yc].name+xc,k-xc);
          entry[k]=0;
        }
        goto redraw0;
      } else if((i==10 || i==13) && *entry) {
        config.file_list&=0x7F;
        if(q=strrchr(entry,'/')) {
          *q++=0;
          if(chdir(*entry?entry:"/")) {
            chdir_error:
            alert_text(strerror(errno));
            *entry=0;
            goto list;
          }
          memmove(entry,q,strlen(q)+1);
        }
        if(!*entry) goto list;
        if(entry[strcspn(entry,"*?\x5B")]) {
          memcpy(pattern,entry,81);
          *entry=0;
          goto list;
        } else {
          xc=strlen(entry);
          if(xc<77 && config.auto_suffix && (xc<4 || memcmp(entry+xc-4,".sav",4)) && !(yc!=nitems && !strcmp(items[yc].name,entry))) memcpy(entry+xc,".sav",5);
          i=lstat(entry,&st);
          if(issave && i && errno==ENOENT) goto ok;
          if(issave && !i && config.confirm_overwrite && !S_ISDIR(st.st_mode)) {
            snprintf(buf,74,"File \"%s\" already exists. Overwrite?",entry);
            if(!ask_yn(buf,0)) {
              *entry=0;
              goto redraw0;
            }
          }
          if(i) {
            alert_text(strerror(errno));
            *entry=0;
            goto redraw0;
          }
          if(S_ISDIR(st.st_mode)) {
            if(chdir(entry)) goto chdir_error;
            *entry=0;
            goto list;
          }
          goto ok;
        }
      }
  }
  goto redraw1;
  moved:
  if(!nitems) {
    yc=ys=0;
    goto redraw0;
  }
  if(yc<0) yc=0;
  if(yc>=nitems) yc=nitems-1;
  if(nitems<21) ys=0; else if(ys>nitems-21) ys=nitems-21;
  if(ys<yc-20) ys=yc-20;
  if(ys>yc) ys=yc;
  if(ys<0) ys=0;
  if(yc<nitems) snprintf(entry,77,"%s",items[yc].name);
  goto redraw0;
  ok:
  free(items);
  free(savename);
  savename=strdup(entry);
  if(!savename) err(1,"Allocation failed");
  v_mode=vm;
  return 1;
}

static void discard_unused_lumps(void) {
  // Some lumps are only used in save games, so discard them from memory after saving/restoring.
  revert_lump("SAVE.DER");
  revert_lump("CURRENT.BRD");
  revert_lump("MEMORY");
  revert_lump("GLOBAL");
  revert_lump("DYNASTR");
  revert_lump("X0.INV");
  revert_lump("X1.INV");
  revert_lump("X2.INV");
  revert_lump("X3.INV");
  revert_lump("X4.INV");
  revert_lump("X5.INV");
  revert_lump("X6.INV");
  revert_lump("X7.INV");
}

static void save_itemdef_flags(ASN1_Encoder*enc,Uint32 m) {
  FILE*fp=asn1_primitive_stream(enc,ASN1_UNIVERSAL,ASN1_BIT_STRING);
  int i,c;
  if(!fp) err(1,"Allocation failed");
  fputc(7&-nitemdefs,fp);
  for(c=i=0;i<nitemdefs;i++) {
    if(itemdefs[i].flag&m) c|=128>>(i&7);
    if((i&7)==7) fputc(c,fp),c=0;
  }
  if(nitemdefs&7) fputc(c,fp);
  asn1_end(enc);
}

void save_state(void) {
  Uint8 m[32];
  ASN1_Encoder*enc;
  FILE*fp;
  Uint32 u,v;
  int i;
  if(init_savegame()) return;
  if(!savename) {
    alert_text("File is not selected; push F3 or F4 to select a file");
    return;
  }
  v_status[1]='$';
  draw_text(0,0," Saving... ",0x6F,-1);
  redisplay();
  errno=0;
  //  SAVE.DER
  if(!(fp=open_lump("SAVE.DER","w"))) goto error;
  if(!(enc=asn1_create_encoder(fp))) errx(1,"Error with asn1_create_encoder");
  asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_RELATIVE_OID,version_rel_oid,version_rel_oid_length);
    asn1_encode_integer(enc,cur_board_id);
    asn1_encode_integer(enc,cur_screen_id);
    asn1_encode_integer(enc,scroll_x);
    asn1_encode_integer(enc,scroll_y);
    asn1_encode_boolean(enc,condflag);
    for(i=0;i<8;i++) asn1_encode_integer(enc,regs[i]);
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      for(i=0;i<16;i++) asn1_encode_integer(enc,status_vars[i]);
    asn1_end(enc);
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,textbuf,ntextbuf);
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,vtextbuf,nvtextbuf);
    asn1_encode_integer(enc,vtexttime);
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      for(i=0;i<16;i++) asn1_encode_c_string(enc,ASN1_OCTET_STRING,namedflag[i].name);
    asn1_end(enc);
    asn1_encode_boolean(enc,(global_text && maxstat && stats->text==global_text));
    if(global_frameoffset) {
      asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
        asn1_encode_integer(enc,global_frameoffset);
        asn1_encode_integer(enc,global_frameptr);
      asn1_end(enc);
    } else {
      asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_NULL,0,0);
    }
    save_fontpal_state(enc);
    *m=pvarproperty.type; memcpy(m+1,pvarproperty.data,15);
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,m,16);
    if(nitemdefs) save_itemdef_flags(enc,IDF_UNIDENTIFIED); else asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_NULL,0,0);
    if(*music_name) {
      asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
        asn1_encode_c_string(enc,ASN1_VISIBLE_STRING,music_name);
        asn1_encode_integer(enc,music_song);
      asn1_end(enc);
    } else {
      asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_NULL,0,0);
    }
    asn1_encode_integer(enc,item_random_key);
    asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
      for(i=0;i<4;i++) asn1_encode(enc,asn1reg+i);
    asn1_end(enc);
  asn1_end(enc);
  asn1_finish_encoder(enc);
  fclose(fp);
  //  CURRENT.BRD
  if(!(fp=open_lump("CURRENT.BRD","w"))) goto error;
  save_board(fp,1);
  fclose(fp);
  //  MEMORY
  if(!(fp=open_lump("MEMORY","w"))) goto error;
  for(u=0x10000;u>0x100 && !memory[u-1];u--);
  for(v=0;v<u;v++) write16(fp,memory[v]);
  fclose(fp);
  //  GLOBAL
  if(global_text) {
    if(!(fp=open_lump("GLOBAL","w"))) goto error;
    fwrite(global_text,1,global_length,fp);
    fclose(fp);
  }
  //  DYNASTR
  if(ndynastr && (fp=open_lump("DYNASTR","w"))) {
    fputc(ndynastr,fp);
    for(i=0;i<ndynastr;i++) {
      fputc(dynastr[i].len,fp);
      fwrite(dynastr[i].text,1,dynastr[i].len,fp);
    }
    fclose(fp);
  }
  //  X?.INV
  strcpy(m,"X0.INV");
  for(i=0;i<8;i++) {
    m[1]=i+'0';
    save_inventory(fp=open_lump(m,"w"),inventory+i);
    if(fp) fclose(fp);
  }
  //
  fp=fopen(savename,"w");
  if(!fp) goto error;
  save_game(fp);
  fclose(fp);
  discard_unused_lumps();
  if(event.type==SDL_KEYDOWN) event.type=SDL_NOEVENT;
  return;
  error: v_status[1]='!'; if(errno) alert_text(strerror(errno)); else alert_text("Error saving game");
  discard_unused_lumps();
  if(event.type==SDL_KEYDOWN) event.type=SDL_NOEVENT;
}

static void load_itemdef_flags(ASN1_Value*v,Uint32 m) {
  int i;
  if(v->class || v->type!=ASN1_BIT_STRING || v->length!=(nitemdefs+15)/8) errx(1,"Invalid data in save game file: Error in SAVE.DER lump");
  for(i=0;i<nitemdefs;i++) {
    if((128>>(i&7))&v->data[i/8+1]) itemdefs[i].flag|=m; else itemdefs[i].flag&=~m;
  }
}

static void load_saveder(FILE*fp,char*useglobalscript) {
  ASN1_Value v0,v1,v2;
  int i,j;
  if(asn1_read_item(fp,&v0,0) || v0.class) bad: errx(1,"Invalid data in save game file: Error in SAVE.DER lump");
  if(asn1_first_of(&v1,&v0) || v1.class || (v1.type!=ASN1_RELATIVE_OID && v1.type!=ASN1_OID)) goto bad;
  if(asn1_next_of(&v1,&v0) || asn1_decode_number(&v1,ASN1_AUTO,&cur_board_id)) goto bad;
  if(asn1_next_of(&v1,&v0) || asn1_decode_number(&v1,ASN1_AUTO,&cur_screen_id)) goto bad;
  if(asn1_next_of(&v1,&v0) || asn1_decode_number(&v1,ASN1_AUTO,&scroll_x)) goto bad;
  if(asn1_next_of(&v1,&v0) || asn1_decode_number(&v1,ASN1_AUTO,&scroll_y)) goto bad;
  if(asn1_next_of(&v1,&v0) || v1.class || v1.type!=ASN1_BOOLEAN || v1.length!=1) goto bad;
  condflag=(*v1.data?1:0);
  for(i=0;i<8;i++) if(asn1_next_of(&v1,&v0) || asn1_decode_number(&v1,ASN1_AUTO,regs+i)) goto bad;
  if(asn1_next_of(&v1,&v0) || v1.class || v1.type!=ASN1_SEQUENCE || asn1_first_of(&v2,&v1)) goto bad;
  for(i=0;i<16;i++) {
    if(asn1_decode_number(&v2,ASN1_AUTO,status_vars+i)) goto bad;
    if((j=asn1_next_of(&v2,&v1))!=(i==15?ASN1_DONE:ASN1_OK)) goto bad;
  }
  if(asn1_next_of(&v1,&v0) || v1.class || v1.type!=ASN1_OCTET_STRING || v1.constructed || v1.length>80) goto bad;
  memset(textbuf,0,81); memcpy(textbuf,v1.data,ntextbuf=v1.length);
  if(asn1_next_of(&v1,&v0) || v1.class || v1.type!=ASN1_OCTET_STRING || v1.constructed || v1.length>80) goto bad;
  memset(vtextbuf,0,81); memcpy(vtextbuf,v1.data,nvtextbuf=v1.length);
  if(asn1_next_of(&v1,&v0) || asn1_decode_number(&v1,ASN1_AUTO,&vtexttime)) goto bad;
  if(vtexttime) ++vtexttime;
  if(vtexttime>config.message_timer) vtexttime=config.message_timer;
  if(asn1_next_of(&v1,&v0) || v1.class || v1.type!=ASN1_SEQUENCE || asn1_first_of(&v2,&v1)) goto bad;
  for(i=0;i<16;i++) {
    if(v2.constructed || v2.class || v2.type!=ASN1_OCTET_STRING || v2.length>15) goto bad;
    memset(namedflag[i].name,0,16); memcpy(namedflag[i].name,v2.data,v2.length);
    if((j=asn1_next_of(&v2,&v1))!=(i==15?ASN1_DONE:ASN1_OK)) goto bad;
  }
  if(asn1_next_of(&v1,&v0) || v1.class || v1.type!=ASN1_BOOLEAN || v1.length!=1) goto bad;
  *useglobalscript=(*v1.data?1:0);
  if(asn1_next_of(&v1,&v0) || v1.class || (v1.type!=ASN1_SEQUENCE && v1.type!=ASN1_NULL)) goto bad;
  if(v1.type==ASN1_SEQUENCE) {
    if(asn1_first_of(&v2,&v1) || asn1_decode_number(&v2,ASN1_AUTO,&global_frameoffset)) goto bad;
    if(asn1_next_of(&v2,&v1) || asn1_decode_number(&v2,ASN1_AUTO,&global_frameptr)) goto bad;
    if(asn1_next_of(&v2,&v1)!=ASN1_DONE) goto bad;
  } else {
    global_frameoffset=global_frameptr=0;
  }
  // Further items might not be present in a older file, so do not error if they are missing.
  if((j=asn1_next_of(&v1,&v0))==ASN1_DONE) goto done; else if(j) goto bad;
  load_fontpal_state(&v1);
  if((j=asn1_next_of(&v1,&v0))==ASN1_DONE) goto done; else if(j) goto bad;
  if(v1.class || v1.type!=ASN1_NULL) {
    if(v1.class || v1.type!=ASN1_OCTET_STRING || v1.length!=16) goto bad;
    pvarproperty.type=v1.data[0]; memcpy(pvarproperty.data,v1.data+1,15);
  }
  if((j=asn1_next_of(&v1,&v0))==ASN1_DONE) goto done; else if(j) goto bad;
  if(v1.class || v1.type!=ASN1_NULL) load_itemdef_flags(&v1,IDF_UNIDENTIFIED);
  if((j=asn1_next_of(&v1,&v0))==ASN1_DONE) goto done; else if(j) goto bad;
  if(v1.class) goto bad;
  if(v1.type==ASN1_SEQUENCE) {
    char m[9]={};
    Uint16 s=0;
    if(asn1_first_of(&v2,&v1) || v2.class || v2.type!=ASN1_VISIBLE_STRING || v2.length<1 || v2.length>8) goto bad;
    memcpy(m,v2.data,v2.length);
    if(asn1_next_of(&v2,&v1) || v2.class || v2.type!=ASN1_INTEGER || asn1_decode_number(&v2,ASN1_INTEGER,&s)) goto bad;
    audio_set_music(m,s);
  } else if(v1.type!=ASN1_NULL) {
    goto bad;
  }
  if((j=asn1_next_of(&v1,&v0))==ASN1_DONE) goto done; else if(j) goto bad;
  if(v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&item_random_key)) goto bad;
  if((j=asn1_next_of(&v1,&v0))==ASN1_DONE) goto done; else if(j || v1.class!=ASN1_UNIVERSAL) goto bad;
  if(v1.type==ASN1_SEQUENCE) for(i=0;i<4;i++) {
    asn1_free(asn1reg+i);
    if(i?asn1_next_of(&v2,&v1):asn1_first_of(&v2,&v1)) goto bad;
    if(asn1_copy(&v2,asn1reg+i)) goto bad;
  }
  // End
  done: asn1_free(&v0);
}

void load_state(void) {
  Uint8 buf[32]={'!','S','Z','0',0,0,0,7,0,0};
  FILE*fp;
  Uint32 u,v;
  int i,j;
  char useglobalscript=0;
  if(init_savegame()) return;
  if(!savename) {
    alert_text("File is not selected; push F3 or F4 to select a file");
    return;
  }
  v_status[1]='$';
  draw_text(0,0," Restoring... ",0x6F,-1);
  redisplay();
  audio_set_sfx("@65536ZX");
  if(config.version_check) {
    if(fp=open_lump("!SZ0","r")) {
      fread(buf+9,1,7,fp);
      fclose(fp);
    }
  }
  fp=fopen(savename,"r");
  if(!fp) {
    err(1,"Cannot open save game file \"%s\" for reading",savename);
    v_status[1]='!';
    alert_text("Error restoring saved game");
    return;
  }
  if(config.version_check) {
    fread(buf+16,1,16,fp);
    if(memcmp(buf,buf+16,16)) {
      v_status[1]='!';
      alert_text("Save game does not match world file, or not a save game file");
      fclose(fp);
      return;
    }
  }
  rewind(fp); // ensure that the !SZ0 lump is not discarded
  restore_game(fp);
  //  SAVE.DER
  if(!(fp=open_lump("SAVE.DER","r"))) errx(1,open_lump("SAVE","r")?"This is an old save game file; not compatible with this version of Super ZZ Zero.":"Invalid save game file (missing SAVE.DER lump)");
  randomize_itemdefs(1);
  load_saveder(fp,&useglobalscript);
  randomize_itemdefs(0);
  fclose(fp);
  //  MEMORY
  if(fp=open_lump("MEMORY","r")) {
    u=lump_size>>1;
    if(u>0x10000) errx(1,"Invalid data in save game file");
    for(v=0;v<u;v++) memory[v]=read16(fp);
    for(;v<0x10000;v++) memory[v]=0;
    fclose(fp);
  }
  //  ????.SCR
  fp=open_lump_by_number(cur_screen_id,"SCR","r");
  if(!fp || load_screen(fp)) errx(1,"Error restoring screen");
  fclose(fp);
  //  CURRENT.BRD
  if(!(fp=open_lump("CURRENT.BRD","r"))) errx(1,"Invalid save game file (missing CURRENT.BRD lump)");
  if(load_board(fp)) errx(1,"Error loading CURRENT.BRD lump from save game file");
  fclose(fp);
  //  GLOBAL
  if(fp=open_lump("GLOBAL","r")) {
    global_text=realloc(global_text,(global_length=lump_size)+1);
    if(!global_text) err(1,"Allocation failed");
    fread(global_text,1,lump_size,fp);
    global_text[lump_size]=0;
    if(v && maxstat) {
      stats->text=global_text;
      stats->length=global_length;
    }
    fclose(fp);
  } else {
    free(global_text);
    global_text=0;
    global_length=0;
  }
  if(global_frameoffset) {
    if(!global_text || global_frameoffset>global_length-4 || global_frameptr>global_length) errx(1,"Invalid data in save game file");
  } else if(global_frameptr) {
    errx(1,"Invalid data in save game file");
  }
  if(useglobalscript && maxstat) {
    stats->text=global_text;
    stats->length=global_length;
    stats->frame=global_frameoffset;
    if(stats->count) stats->xy->frame=global_frameptr;
  }
  //  DYNASTR
  if(fp=open_lump("DYNASTR","r")) {
    ndynastr=read8(fp);
    dynastr=realloc(dynastr,ndynastr*sizeof(DynaString));
    if(ndynastr && !dynastr) err(1,"Allocation failed");
    for(i=0;i<ndynastr;i++) {
      dynastr[i].len=read8(fp);
      if(dynastr[i].len>=DYNASTRLEN) errx(1,"Invalid data in save game file");
      fread(dynastr[i].text,1,dynastr[i].len,fp);
      dynastr[i].text[dynastr[i].len]=0;
    }
    fclose(fp);
  } else {
    free(dynastr);
    dynastr=0;
    ndynastr=0;
  }
  //  X?.INV
  strcpy(buf,"X0.INV");
  for(i=0;i<8;i++) {
    buf[1]=i+'0';
    if(load_inventory(fp=open_lump(buf,"r"),inventory+i)) errx(1,"Invalid data in save game file");
    if(fp) fclose(fp);
  }
  // Finished
  discard_unused_lumps();
  if(event.type==SDL_KEYDOWN) event.type=SDL_NOEVENT;
}
