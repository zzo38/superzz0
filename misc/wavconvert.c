#if 0
gcc -s -O2 -o ./wavconvert wavconvert.c
exit
#endif

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void skip_header(void) {
  uint8_t z[12]={};
  fread(z,1,12,stdin);
  if(memcmp(z,"RIFF",4) || memcmp(z+8,"WAVE",4)) errx(1,"Improper header");
}

static int do_blocks(void) {
  static uint8_t*data=0;
  static uint32_t data_alloc=0;
  static uint8_t started=0;
  uint8_t head[8];
  uint32_t size;
  if(fread(head,1,8,stdin)<=0) return 0;
  if(head[7]) errx(1,"Too big block");
  size=head[4]|(head[5]<<8)|(head[6]<<16)|(head[7]<<24);
  if(!size) return 1;
  if(size>data_alloc) {
    data=realloc(data,data_alloc=size);
    if(!data) err(1,"Allocation failed");
  }
  if(size) fread(data,1,size,stdin);
  if(!memcmp(head,"fmt ",4)) {
    if(started) errx(1,"Multiple fmt blocks");
    if(size<16) errx(1,"The fmt block is too small");
    if((data[2]>2) || data[3]) errx(1,"Too many channels");
    if(data[6] || data[7]) errx(1,"Too high sample rate");
    if((data[14]!=8 && data[14]!=16) || data[15]) errx(1,"Improper bits per sample");
    if((data[0]!=1 && data[0]!=6 && data[0]!=7) || data[1]) errx(1,"Unrecognized codec");
    putchar(4);
    putchar(data[2]==2?5:1);
    putchar(data[0]==1?(data[14]==8?0:4):data[0]);
    fwrite(data+4,1,2,stdout);
    started=1;
  } else if(!memcmp(head,"data",4)) {
    if(!started) errx(1,"The data block is not preceded by the fmt block");
    fwrite(data,1,size,stdout);
  }
  return 1;
}

int main(int argc,char**argv) {
  skip_header();
  while(do_blocks());
  return 0;
}

