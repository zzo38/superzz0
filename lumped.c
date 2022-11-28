#if 0
gcc -s -O2 -c -Wno-unused-result lumped.c `sdl-config --cflags`
exit
#endif

#include "common.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
  char name[16]; // name in uppercase (null-terminated)
  Uint32 offset; // offset of beginning of data; if zero, then it is a new lump
  Uint32 length;
  Uint8*data; // if changed or in use
  Uint32 refcount;
  Uint8 flag; // bit0=changed bit1=writing
} Lump;

char*world_name;

static FILE*worldfile;
static Lump*lumps;
static int nlumps;

typedef struct {
  char name[16];
  Uint32 length;
  Uint32 wlength;
  Uint8*data;
  Uint32 pos;
  Uint32 id;
  Uint8 writing;
} LumpCookie;

static void convert_lump_name(const char*in,char*out) {
  int i;
  for(i=0;i<15;i++) {
    out[i]=in[i];
    if(!in[i]) break;
    if(in[i]>='a' && in[i]<='z') out[i]+='A'-'a';
  }
  out[15]=0;
}

static int compare_lump_name(const void*a,const void*b) {
  const Lump*x=a;
  const Lump*y=b;
  return strcmp(x->name,y->name);
}

static ssize_t lump_read(void*cookie,char*buf,size_t size) {
  LumpCookie*co=cookie;
  if(co->pos>=co->length || !size) return 0;
  if(size>co->length-co->pos) size=co->length-co->pos;
  memcpy(buf,co->data+co->pos,size);
  co->pos+=size;
  return size;
}

static ssize_t lump_write(void*cookie,const char*buf,size_t size) {
  LumpCookie*co=cookie;
  if(!size) return 0;
  if(co->pos+size>co->length) co->length=co->pos+size;
  if(co->pos+size>co->wlength) {
    if(co->wlength) co->wlength+=co->wlength>>1; else co->wlength=1024;
    if(co->pos+size>co->wlength) co->wlength=co->pos+size;
    co->data=realloc(co->data,co->wlength);
    if(!co->data) err(1,"Allocation failed");
  }
  memcpy(co->data+co->pos,buf,size);
  co->pos+=size;
  return size;
}

static int lump_seek(void*cookie,off64_t*offset,int whence) {
  LumpCookie*co=cookie;
  switch(whence) {
    case SEEK_SET: co->pos=*offset; break;
    case SEEK_CUR: co->pos+=*offset; break;
    case SEEK_END: co->pos=co->length+*offset; break;
    default: return -1;
  }
  *offset=co->pos;
  return 0;
}

static int lump_close(void*cookie) {
  Lump*obj;
  LumpCookie*co=cookie;
  Lump key;
  if(co->id<nlumps && !strcmp(lumps[co->id].name,co->name)) {
    obj=lumps+co->id;
  } else {
    memcpy(key.name,co->name,16);
    obj=bsearch(&key,lumps,nlumps,sizeof(Lump),compare_lump_name);
    if(!obj) errx(1,"Unexpected error in lump_close");
  }
  --obj->refcount;
  if(co->writing) {
    obj->flag&=~2;
    obj->data=co->data;
    obj->length=co->length;
  }
  if(!obj->refcount && !obj->flag) free(obj->data);
  free(co);
  return 0;
}

FILE*open_lump(const char*name,const char*mode) {
  static const cookie_io_functions_t cf={
    .read=lump_read,
    .write=lump_write,
    .seek=lump_seek,
    .close=lump_close,
  };
  LumpCookie*co;
  FILE*fp;
  Lump key;
  Lump*obj;
  convert_lump_name(name,key.name);
  if(strchr(mode,'x')) {
    if(bsearch(&key,lumps,nlumps,sizeof(Lump),compare_lump_name)) return 0;
    goto notfound;
  }
  try_again:
  obj=bsearch(&key,lumps,nlumps,sizeof(Lump),compare_lump_name);
  if(!obj) {
    notfound:
    if(*mode=='r') return 0;
    lumps=realloc(lumps,(nlumps+1)*sizeof(Lump));
    if(!lumps) err(1,"Allocation failed");
    lumps[nlumps]=key;
    lumps[nlumps].offset=lumps[nlumps].length=lumps[nlumps].refcount=0;
    lumps[nlumps].data=0;
    lumps[nlumps].flag=1;
    ++nlumps;
    qsort(lumps,nlumps,sizeof(Lump),compare_lump_name);
    goto try_again;
  }
  if(obj->flag&2) return 0;
  if(obj->refcount && (*mode!='r' || strchr(mode,'+'))) return 0;
  ++obj->refcount;
  co=malloc(sizeof(LumpCookie));
  if(!co) err(1,"Allocation failed");
  memcpy(co->name,obj->name,16);
  if(*mode=='w') {
    obj->length=0;
    free(obj->data);
    obj->data=0;
  }
  co->length=obj->length;
  co->data=obj->data;
  co->pos=co->wlength=0;
  co->id=obj-lumps;
  co->writing=0;
  if(obj->length && !obj->data) {
    fseek(worldfile,obj->offset,SEEK_SET);
    co->data=obj->data=malloc(obj->length);
    if(!co->data) err(1,"Allocation failed");
    fread(co->data,1,obj->length,worldfile);
  }
  if(*mode!='r' || strchr(mode,'+')) co->writing=obj->flag|=3;
  fp=fopencookie(co,mode,cf);
  if(!fp) errx(1,"Unable to open lump cookie stream");
  return fp;
}

FILE*open_lump_by_number(Uint16 id,const char*ext,const char*mode) {
  static char name[16];
  snprintf(name,15,"%04X.%s",id,ext);
  return open_lump(name,mode);
}

void revert_lump(const char*name) {
  Lump key;
  Lump*obj;
  convert_lump_name(name,key.name);
  if((obj=bsearch(&key,lumps,nlumps,sizeof(Lump),compare_lump_name)) && obj->flag&1) {
    if((obj->flag&2) || obj->refcount) errx(1,"Trying to revert a lump which is in use");
    free(obj->data);
    obj->data=0;
    obj->flag=0;
    if(obj->offset && worldfile) {
      fseek(worldfile,obj->offset-4,SEEK_SET);
      obj->length=fgetc(worldfile)<<16;
      obj->length|=fgetc(worldfile)<<24;
      obj->length|=fgetc(worldfile)<<0;
      obj->length|=fgetc(worldfile)<<8;
    } else {
      obj->length=0;
    }
  }
}

void revert_lump_by_number(Uint16 id,const char*ext) {
  static char name[16];
  snprintf(name,15,"%04X.%s",id,ext);
  revert_lump(name);
}

int open_world(const char*name) {
  FILE*fp=fopen(name,"r");
  Uint32 n;
  int c,i;
  if(!fp) {
    warn("Cannot open file '%s'",name);
    return -1;
  }
  if(flock(fileno(fp),LOCK_SH|LOCK_NB)) {
    warn("Cannot lock file '%s'",name);
    fclose(fp);
    return -1;
  }
  if(name!=world_name) {
    free(world_name);
    world_name=strdup(name);
    if(!world_name) err(1,"Allocation failed");
  }
  if(worldfile) fclose(worldfile);
  worldfile=fp;
  for(n=0;n<nlumps;n++) free(lumps[n].data);
  free(lumps);
  nlumps=0;
  for(;;) {
    while((c=fgetc(fp))>0);
    if(c<0) break;
    ++nlumps;
    n=fgetc(fp)<<16;
    n|=fgetc(fp)<<24;
    n|=fgetc(fp);
    n|=fgetc(fp)<<8;
    fseek(fp,n,SEEK_CUR);
  }
  rewind(fp);
  if(!nlumps) return 0;
  lumps=calloc(nlumps,sizeof(Lump));
  if(!lumps) err(1,"Allocation failed");
  for(n=0;n<nlumps;n++) {
    i=0;
    while((c=fgetc(fp))>0) {
      if(i<16) lumps[n].name[i++]=c+(c>='a' && c<='z'?'A'-'a':0); else lumps[n].name[0]=0;
    }
    lumps[n].length=fgetc(fp)<<16;
    lumps[n].length|=fgetc(fp)<<24;
    lumps[n].length|=fgetc(fp);
    lumps[n].length|=fgetc(fp)<<8;
    lumps[n].offset=ftell(fp);
    fseek(fp,lumps[n].length,SEEK_CUR);
    if(!lumps[n].name[0]) --n,--nlumps;
  }
  qsort(lumps,nlumps,sizeof(Lump),compare_lump_name);
  return 0;
}

void close_world(void) {
  Uint32 n;
  free(world_name);
  world_name=0;
  if(worldfile) fclose(worldfile);
  worldfile=0;
  for(n=0;n<nlumps;n++) free(lumps[n].data);
  free(lumps);
  nlumps=0;
}

int save_world(const char*name) {
  Uint8 buf[1024];
  FILE*fp=name?fopen(name,"wx"):fopen(".superzz0_save","w");
  Uint32 n,o;
  if(!name && !worldfile) {
    if(!world_name) errx(1,"Internal confusion in save_world");
    worldfile=fmemopen("",1,"rb");
  }
  if(!fp) {
    warn("Cannot save world");
    return -1;
  }
  if(!name && flock(fileno(worldfile),LOCK_EX|LOCK_NB)) {
    warn("Cannot lock world");
    fclose(fp);
    return -1;
  }
  if(flock(fileno(fp),LOCK_EX)) {
    warn("Cannot lock new world file");
    fclose(fp);
    if(!name) flock(fileno(worldfile),LOCK_SH);
    return -1;
  }
  for(n=0;n<nlumps;n++) {
    if(lumps[n].flag&2) errx(1,"Trying to save world while lump '%s' is open for writing",lumps[n].name);
    if(!name) lumps[n].flag=0;
    fwrite(lumps[n].name,1,strlen(lumps[n].name)+1,fp);
    o=lumps[n].length;
    fputc(o>>16,fp); fputc(o>>24,fp); fputc(o>>0,fp); fputc(o>>8,fp);
    o=lumps[n].offset;
    if(!name) lumps[n].offset=ftell(fp);
    if(lumps[n].data) {
      fwrite(lumps[n].data,1,lumps[n].length,fp);
    } else {
      fseek(worldfile,o,SEEK_SET);
      o=lumps[n].length;
      while(o>1024) {
        fread(buf,1,1024,worldfile);
        fwrite(buf,1,1024,fp);
        o-=1024;
      }
      if(o) {
        fread(buf,1,o,worldfile);
        fwrite(buf,1,o,fp);
      }
    }
  }
  n=ferror(fp);
  fclose(fp);
  if(n) {
    if(!name) err(1,"Error writing to new world file");
    warn("Error writing to new world file");
    return -1;
  }
  if(!name) {
    if(rename(".superzz0_save",world_name)) err(1,"Cannot replace world file (the new file is now called .superzz0_save)");
    flock(fileno(worldfile),LOCK_UN);
    fclose(worldfile);
    worldfile=fopen(world_name,"r");
    if(!worldfile) err(1,"Cannot reopen world file '%s'",name);
  }
  return 0;
}

int save_game(FILE*fp) {
  FILE*f;
  Uint32 n,o;
  for(n=0;n<nlumps;n++) if(lumps[n].flag&1) {
    if(lumps[n].flag&2) errx(1,"Trying to save game while lump '%s' is open for writing",lumps[n].name);
    fwrite(lumps[n].name,1,strlen(lumps[n].name)+1,fp);
    o=lumps[n].data?lumps[n].length:0;
    fputc(o>>16,fp); fputc(o>>24,fp); fputc(o>>0,fp); fputc(o>>8,fp);
    if(o) fwrite(lumps[n].data,1,o,fp);
  }
  return 0;
}

int restore_game(FILE*fp) {
  Uint8 buf[1024];
  Uint32 n,o;
  FILE*f;
  int c;
  for(n=0;n<nlumps;n++) {
    if((lumps[n].flag&2) || lumps[n].refcount) errx(1,"Trying to restore a saved game while lump '%s' is in use",lumps[n].name);
    if(lumps[n].flag&1) {
      lumps[n].flag=0;
      free(lumps[n].data);
      lumps[n].data=0;
      if(lumps[n].offset && worldfile) {
        fseek(worldfile,lumps[n].offset-4,SEEK_SET);
        lumps[n].length=fgetc(worldfile)<<16;
        lumps[n].length|=fgetc(worldfile)<<24;
        lumps[n].length|=fgetc(worldfile)<<0;
        lumps[n].length|=fgetc(worldfile)<<8;
      } else {
        lumps[n].length=0;
      }
    }
  }
  for(;;) {
    o=0;
    while((c=fgetc(fp))>0) if(o<16) buf[o++]=c;
    if(c<0) break;
    buf[o]=0;
    o=fgetc(fp)<<16;
    o|=fgetc(fp)<<24;
    o|=fgetc(fp);
    o|=fgetc(fp)<<8;
    f=open_lump(buf,"w");
    if(!f) errx(1,"Error opening lump for writing");
    while(o>1024) {
      fread(buf,1,1024,fp);
      fwrite(buf,1,1024,f);
      o-=1024;
    }
    if(o) {
      fread(buf,1,o,fp);
      fwrite(buf,1,o,f);
    }
    fclose(f);
  }
  return 0;
}

