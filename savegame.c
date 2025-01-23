#if 0
gcc -s -O2 -c -Wno-unused-result savegame.c `sdl-config --cflags`
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
      if(i>32 && i<127 && xc<76) {
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
  return 1;
}

void save_state(void) {
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
  //  SAVE
  if(!(fp=open_lump("SAVE","w"))) goto error;
  v=(condflag?1:0);
  write16(fp,v);
  write16(fp,cur_board_id);
  write16(fp,cur_screen_id);
  write32(fp,scroll_x);
  write32(fp,scroll_y);
  for(i=0;i<16;i++) write32(fp,status_vars[i]);
  for(i=0;i<7;i++) write32(fp,regs[i]);
  write8(fp,ntextbuf); if(ntextbuf) fwrite(textbuf,1,ntextbuf,fp);
  write8(fp,nvtextbuf); if(nvtextbuf) fwrite(vtextbuf,1,nvtextbuf,fp);
  for(i=0;i<16;i++) fwrite(namedflag[i].name,1,strlen(namedflag[i].name)+1,fp);
  write16(fp,vtexttime);
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
  //
  fp=fopen(savename,"w");
  if(!fp) goto error;
  save_game(fp);
  fclose(fp);
  return;
  error: v_status[1]='!'; if(errno) alert_text(strerror(errno)); else alert_text("Error saving game");
}

void load_state(void) {
  Uint8 buf[32]={'!','S','Z','0',0,0,0,7,0,0};
  FILE*fp;
  Uint32 u,v;
  int i;
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
  restore_game(fp);
  //  SAVE
  if(!(fp=open_lump("SAVE","r"))) errx(1,"Invalid save game file (missing SAVE lump)");
  v=read16(fp);
  if(v&~3) errx(1,"Invalid data in save game file");
  condflag=v&1;
  cur_board_id=read16(fp);
  cur_screen_id=read16(fp);
  scroll_x=read32(fp);
  scroll_y=read32(fp);
  for(i=0;i<16;i++) status_vars[i]=read32(fp);
  for(i=0;i<7;i++) regs[i]=read32(fp);
  memset(textbuf,0,81);
  if((ntextbuf=read8(fp)) && ntextbuf<81) fread(textbuf,1,ntextbuf,fp);
  memset(vtextbuf,0,81);
  if((nvtextbuf=read8(fp)) && nvtextbuf<81) fread(vtextbuf,1,nvtextbuf,fp);
  if(ntextbuf>80 || nvtextbuf>80) errx(1,"Invalid data in save game file");
  vtexttime=read16(fp);
  if(vtexttime) ++vtexttime;
  if(vtexttime>config.message_timer) vtexttime=config.message_timer;
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
  }
}
