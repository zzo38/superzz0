#if 0
gcc -s -O2 -o ./vocconvert vocconvert.c
exit
#endif

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void skip_header(void) {
  uint8_t b[64];
  uint16_t n;
  if(fread(b,1,26,stdin)!=26) errx(1,"Improper header");
  if(memcmp(b,"Creative Voice File\x1A",20)) errx(1,"Improper header");
  if(b[23]!=1) errx(1,"Improper version");
  n=b[20]|(b[21]<<8);
  if(n<26) errx(1,"Improper header");
  while(n-->26) getchar();
}

static int do_blocks(void) {
  static uint8_t*data=0;
  static uint32_t data_alloc=0;
  static uint8_t started=0;
  int bt=getchar();
  uint32_t size;
  if(bt==EOF || !bt) return 0;
  size=getchar(); size|=getchar()<<8; size|=getchar()<<16;
  if(size>0xFFFFFF) errx(1,"Improper block type");
  if(size>data_alloc) {
    data=realloc(data,data_alloc=size);
    if(!data) err(1,"Allocation failed");
  }
  if(size) fread(data,1,size,stdin);
  switch(bt) {
    case 1: // Sound start
      if(size<3) errx(1,"Improper block size");
      if(started>1) errx(1,"Multiple blocks are not implemented");
      if(!started) {
        putchar(4);
        putchar(0);
        putchar(data[1]);
        putchar(0);
        putchar(data[0]);
      }
      started=2;
      fwrite(data+2,1,size-2,stdout);
      break;
    case 2: // Sound continuation
      fwrite(data,1,size,stdout);
      break;
    case 3: // Silence
      errx(1,"Silence is not implemented");
      break;
    case 4: // Marker
      // No use
      break;
    case 5: // Text
      // No use
      break;
    case 8: // Extra info
      if(started) errx(1,"Multiple blocks are not implemented");
      if(size<4) errx(1,"Improper block size");
      if(data[3]>1) errx(1,"Improper number of channels");
      putchar(4);
      putchar(data[3]<<2);
      putchar(data[2]);
      fwrite(data,1,2,stdout);
      started=1;
      break;
    case 9: // New sound start
      if(started) errx(1,"Multiple blocks are not implemented");
      if(size<12) errx(1,"Improper block size");
      if(data[2] || data[3]) errx(1,"Too high sample rate");
      if(data[5]>2) errx(1,"Improper number of channels");
      if(data[7]) errx(1,"Improper codec number");
      putchar(4);
      putchar(data[5]>1?5:1);
      putchar(data[6]);
      fwrite(data,1,2,stdout);
      fwrite(data+12,1,size-12,stdout);
      started=2;
      break;
    default: errx(1,"Improper block type");
  }
  return 1;
}

int main(int argc,char**argv) {
  skip_header();
  while(do_blocks());
  return 0;
}
