#if 0
gcc -s -O2 -o ./sz0version version.c hash.o
exit
#endif

// This program is used for generating the version.inc file

#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "hash.h"

static int major,minor,patch;
static int major0,minor0,patch0;
static char**files;
static int nfiles;
static unsigned char oldhash[32];
static unsigned char newhash[32];

static size_t copy_stream(FILE*in,FILE*out,size_t len) {
  char buf[0x2000];
  size_t t=0;
  size_t n;
  size_t s;
  while(len) {
    if(len>0x2000) n=0x2000; else n=len;
    if(s=fread(buf,1,n,in)) fwrite(buf,1,s,out);
    if(s<n) return s+t;
    len-=n;
    t+=s;
  }
  return t;
}

static void read_status(void) {
  char*line=0;
  size_t line_size=0;
  int i;
  FILE*f=fopen("version.status","r");
  if(!f) err(1,"Cannot open version.status file");
  if(getline(&line,&line_size,f)<=0) errx(1,"Error in status file");
  sscanf(line,"%d.%d.%d",&major,&minor,&patch);
  major0=major; minor0=minor; patch0=patch;
  if(getline(&line,&line_size,f)<=0) errx(1,"Error in status file");
  if(*line>=0x20) for(i=0;i<32;i++) {
    if(!line[i+i] || !line[i+i+1]) errx(1,"Error in status file");
    sscanf(line+i+i,"%02hhX",oldhash+i);
  }
  while(getline(&line,&line_size,f)>0) {
    i=strlen(line);
    while(i && line[i-1]<=0x20) line[--i]=0;
    if(!*line || *line=='#') continue;
    files=realloc(files,++nfiles*sizeof(char*));
    if(!files) err(1,"Allocation failed");
    if(!(files[nfiles-1]=strdup(line))) err(1,"Allocation failed");
  }
  fclose(f);
}

static void calculate(void) {
  FILE*ff=hash_stream(HASH_SHA3_256,0,newhash);
  FILE*f;
  int i;
  if(!ff) err(1,"Cannot open hash stream");
  for(i=0;i<nfiles;i++) {
    fprintf(ff,"\x1C%s\x02",files[i]);
    f=fopen(files[i],"r");
    if(!f) err(1,"Cannot open \"%s\" file",files[i]);
    copy_stream(f,ff,-1);
    fclose(f);
  }
  fclose(ff);
  newhash[2]^=newhash[0];
  newhash[3]^=newhash[1];
  if(memcmp(newhash+2,oldhash+2,30)) {
    newhash[0]=1;
    newhash[1]=major+minor+patch;
  } else {
    newhash[0]=oldhash[0];
    newhash[1]=oldhash[1];
  }
}

static void show_status(void) {
  int i;
  printf("Version = %d.%d.%d\nOld hash = ",major,minor,patch);
  for(i=0;i<32;i++) printf("%02X",oldhash[i]);
  printf("\nNew hash = ");
  for(i=0;i<32;i++) printf("%02X",newhash[i]);
  putchar('\n');
}

static void write_status(void) {
  int i;
  FILE*f=fopen("version.status","w");
  if(!f) err(1,"Cannot open version.status file for writing");
  fprintf(f,"%d.%d.%d\n",major,minor,patch);
  for(i=0;i<32;i++) fprintf(f,"%02X",newhash[i]);
  fputc('\n',f);
  for(i=0;i<nfiles;i++) fprintf(f,"%s\n",files[i]);
  fclose(f);
}

static int encode_number(FILE*f,int n) {
  // (This currently assumes that the pieces of the version number cannot exceed 16383, but it is unlikely to exceed 16383 anyways)
  if(n>127) {
    fprintf(f,"%d,%d,",(n>>7)+128,n&127);
    return 2;
  } else {
    fprintf(f,"%d,",n);
    return 1;
  }
}

static void write_version_inc(void) {
  int i,s;
  FILE*f=fopen("version.inc","w");
  if(!f) err(1,"Cannot open version.inc file for writing");
  fprintf(f,"// Auto-generated file; see version.doc for instructions\n");
  fprintf(f,"const Uint8 version_hash[32]={");
  for(i=0;i<32;i++) fprintf(f,"%d,",newhash[i]);
  fprintf(f,"};\n");
  fprintf(f,"const Uint8 version_rel_oid[]={0,");
  s=encode_number(f,major);
  s+=encode_number(f,minor);
  i=s;
  s+=encode_number(f,patch);
  fprintf(f,"};\n");
  fprintf(f,"const Uint8 version_rel_oid_length=%d;\n",s+1);
  fprintf(f,"const Uint8 version_rel_oid_length_2=%d;\n",i+1);
  fprintf(f,"const Uint8 version_is_release=%d;\n",*newhash==2);
  fprintf(f,"const char version_name[]=\"Super ZZ Zero\\nVersion %d.%d.%d%s\\n\";\n",major,minor,patch,*newhash==2?"":"(modified)");
  fclose(f);
}

static void new_version(void) {
  //TODO: Possibly handle major versions differently
  //TODO: Possibly check if program has been compiled already, to avoid publishing untested versions
  int i;
  char buf[256];
  printf("New version number = %d.%d.%d\n",major,minor,patch);
  if(*newhash==2) errx(1,"Program has not changed; version number will not be changed");
  *newhash=2;
  i=snprintf(buf,256,"./maker main.mak && fossil commit --tag v%d.%d.%d",major,minor,patch);
  if(major!=major0) i+=snprintf(buf+i,256-i," --tag major");
  if(major!=major0 || minor!=minor0) i+=snprintf(buf+i,256-i," --tag minor");
  if(major!=major0 || minor!=minor0 || patch!=patch0) i+=snprintf(buf+i,256-i," --tag patch");
  puts(buf);
  write_status();
  write_version_inc();
  i=system(buf);
  if(WIFSIGNALED(i) || !WIFEXITED(i) || WEXITSTATUS(i)) {
    warnx("New version canceled; reverting to %d.%d.%d",major0,minor0,patch0);
    *newhash=1;
    major=major0; minor=minor0; patch=patch0;
    write_status();
    write_version_inc();
    if(!WIFSIGNALED(i) || WTERMSIG(i)!=SIGQUIT) -system("./maker main.mak");
  }
  if(WIFSIGNALED(i)) {
    kill(getpid(),WTERMSIG(i));
    errx(1,"Signal %d received from child",WTERMSIG(i));
  }
  if(!WIFEXITED(i) || WEXITSTATUS(i)) {
    errx(WEXITSTATUS(i)?:1,"Exit status %d from child",WEXITSTATUS(i));
  }
  warnx("New version successful.");
}

int main(int argc,char**argv) {
  read_status();
  calculate();
  if(argc<2 || !strcmp(argv[1],"show")) {
    show_status();
  } else if(!strcmp(argv[1],"update")) {
    write_status();
    write_version_inc();
  } else if(!strcmp(argv[1],"major")) {
    major++; minor=patch=0;
    new_version();
  } else if(!strcmp(argv[1],"minor")) {
    minor++; patch=0;
    new_version();
  } else if(!strcmp(argv[1],"patch")) {
    patch++;
    new_version();
  } else {
    errx(1,"Improper mode");
  }
  return 0;
}

