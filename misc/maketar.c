#if 0
gcc -s -O2 -o maketar maketar.c
exit
#endif

#define _GNU_SOURCE
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// (This structure is copied from and modified from the GNU documentation)
typedef union {
  unsigned char raw[512];
  struct {
    char name[100];               /*   0 */
    char mode[8];                 /* 100 */
    char uid[8];                  /* 108 */
    char gid[8];                  /* 116 */
    char size[12];                /* 124 */
    char mtime[12];               /* 136 */
    char chksum[8];               /* 148 */
    char typeflag;                /* 156 */
    char linkname[100];           /* 157 */
    char magic[6];                /* 257 */
    char version[2];              /* 263 */
    char uname[32];               /* 265 */
    char gname[32];               /* 297 */
    char devmajor[8];             /* 329 */
    char devminor[8];             /* 337 */
    char prefix[155];             /* 345 */
    char unused[12];              /* 500 */
  };                              /* 512 */
} Header;

static Header emheader={
  .uid="0000000",
  .gid="0000000",
  .chksum={0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20},
  .typeflag='0', // ordinary files
  .magic={'u','s','t','a','r',' '}, // the documentation says otherwise, but this is what is observed from GNU tar
  .version=" ", // the documentation says otherwise, but this is what is observed from GNU tar
};
static Header header;
static time_t mintime=0;
static time_t maxtime=((time_t)077777777777LL)<0?017777777777LL:077777777777LL;
static unsigned char buf[512];
static char prefix[100];

static void do_file(char*line) {
  char*in=line;
  char*on;
  FILE*f;
  struct stat s;
  int i,j;
  line=strchr(line,' ');
  if(!line) errx(1,"Improper command in input");
  *line++=0;
  on=line;
  if(line=strchr(line,' ')) *line++=0;
  if(stat(in,&s)) err(1,"stat(%s)",in);
  f=fopen(in,"r");
  if(!f) err(1,"fopen(%s)",in);
  memcpy(&header,&emheader,sizeof(header));
  snprintf(header.name,100,"%s%s",prefix,on);
  snprintf(header.size,12,"%011lo",(long)s.st_size);
  snprintf(header.mtime,12,"%011llo",(long long)(s.st_mtime<mintime?mintime:s.st_mtime>maxtime?maxtime:s.st_mtime));
  if(s.st_mode&S_IXUSR) i=0777; else i=0666;
  snprintf(header.mode,8,"%07o",i);
  for(i=j=0;i<512;i++) j+=header.raw[i];
  snprintf(header.chksum,8,"%06lo",(long)(j&0x1FFFFL));
  fwrite(&header,1,512,stdout);
  while(s.st_size>0) {
    fread(buf,1,512,f);
    if(s.st_size<512) memset(buf+s.st_size,0,512-s.st_size);
    fwrite(buf,1,512,stdout);
    if(s.st_size<=512) break;
    s.st_size-=512;
  }
  fclose(f);
}

int main(int argc,char**argv) {
  int n;
  char*line=0;
  size_t linesize=0;
  while((n=getline(&line,&linesize,stdin))>0) {
    while(n && (line[n-1]==' ' || line[n-1]=='\n' || line[n-1]=='\r')) line[--n]=0;
    switch(*line) {
      case 0: case '#': /* do nothing */ break;
      case 'C': if(chdir(line+(line[1]==' '?2:1))) err(1,"chdir"); break;
      case 'G': strncpy(emheader.gname,line+(line[1]==' '?2:1),31); break;
      case 'P': strncpy(prefix,line+(line[1]==' '?2:1),99); break;
      case 'T': if(line[1]=='<') mintime=strtol(line+2,0,0); else if(line[1]=='>') maxtime=strtol(line+2,0,0); else mintime=maxtime=strtol(line+2,0,0); break;
      case 'U': strncpy(emheader.uname,line+(line[1]==' '?2:1),31); break;
      case '.': case '/': do_file(line); break;
      default: errx(1,"Improper command in input"); break;
    }
  }
  memset(buf,0,512);
  fwrite(buf,1,512,stdout);
  fwrite(buf,1,512,stdout);
  return 0;
}
