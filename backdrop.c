#if 0
gcc -s -O2 -o ~/bin/sz0backdrop -std=gnu99 -Wno-unused-result backdrop.c asn1.o
exit
#endif

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "asn1.h"

typedef struct {
  uint8_t r,g,b,use;
} RGB;

typedef struct {
  char name[9];
} Name;

#define KIND_NONE 0
#define KIND_NORMAL 1
#define KIND_SOLID 2

static uint8_t picture[640*350];
static uint8_t zbuffer[640*350];
static uint8_t pkind=0;
static uint8_t zkind=0;
static uint8_t picrlebits=1;
static uint8_t zrlebits=1;
static uint8_t picuncomp,zuncomp;
static uint8_t showinfo,testmode;
static unsigned int piclz77,zlz77;
static unsigned int minlz77=2;
static uint32_t picrleguess,picupguess,picvaguess,zrleguess,zupguess,zvaguess;
static uint8_t piceffort,zeffort;
static uint16_t width=640;
static uint16_t height=350;
static uint32_t mcount[260*2+3];
static int16_t mchain0[260+2];
static int16_t mchain1[260+2];
static int16_t mchainr[260*2+3];
static uint16_t mlength[260];
static int16_t root;
static RGB palette[256];
static RGB inpal[256];
static ASN1_Encoder*enc;
static uint8_t bitbuf,bitpos;
static uint16_t treelast;
static Name palname[3];
static uint8_t pfilter[32];
static uint8_t zfilter[32];

static void make_mcount(const uint8_t*x) {
  uint32_t a;
  uint32_t b=width*height;
  for(a=0;a<260;a++) mcount[a]=0;
  for(a=0;a<b;a++) mcount[x[a]]++;
}

static void make_huffman_codes(void) {
  int i,n,a,b;
  for(i=0;i<260;i++) mchain0[i]=mchain1[i]=mchainr[i]=mchainr[i+260]=-1;
  for(n=0;;) {
    a=b=-1;
    for(i=0;i<n+260;i++) if(mcount[i]) {
      if(a<0 || mcount[i]<mcount[a]) b=a,a=i;
      else if(b<0 || mcount[i]<mcount[b]) b=i;
    }
    if(b==-1) break;
    if(b<a) i=a,a=b,b=i;
    mcount[n+260]=mcount[a]+mcount[b];
    mcount[a]=mcount[b]=0;
    mchain0[n]=a;
    mchain1[n]=b;
    mchainr[a]=mchainr[b]=n+260;
    n++;
  }
  root=a;
  for(i=0;i<260;i++) {
    n=0;
    a=i;
    while(a>=0 && a!=root && mchainr[a]>=0) n++,a=mchainr[a];
    mlength[i]=n;
  }
}

static inline void sendbit(int v,FILE*f) {
  //putchar(v?'#':'.');
  if(v) bitbuf|=128>>bitpos;
  if(++bitpos==8) {
    fputc(bitbuf,f);
    bitbuf=bitpos=0;
  }
}

static void send_code(int16_t x,FILE*f) {
  int y;
  if(x>=0 && x!=root) {
    send_code(y=mchainr[x],f);
    sendbit(x==mchain1[y-260],f);
  }
}

static uint32_t send_rle_number(uint32_t x,uint8_t rlebits,FILE*f) {
  uint32_t nb=0;
  int i;
  do {
    x--;
    if(f) {
      for(i=0;i<rlebits;i++) sendbit(x&(1<<i),f);
      sendbit(x>=(1<<rlebits),f);
    }
    nb+=rlebits+1;
    x>>=rlebits;
  } while(x);
  return nb;
}

static uint32_t send_tree(int16_t t,FILE*f) {
  uint32_t k;
  if(t<260) {
    if(f) sendbit(0,f);
    if(t>=256) {
      if(f) {
        sendbit(1,f); sendbit(1,f); sendbit(1,f);
        sendbit(t&2,f); sendbit(t&1,f);
      }
      k=5;
    } else if(!((t^treelast)&~0x0F)) {
      if(f) {
        sendbit(1,f); sendbit(0,f);
        sendbit(t&0x08,f); sendbit(t&0x04,f); sendbit(t&0x02,f); sendbit(t&0x01,f);
      }
      k=6;
    } else if(!((t^treelast)&~0x3F)) {
      if(f) {
        sendbit(0,f); sendbit(0,f);
        sendbit(t&0x20,f); sendbit(t&0x10,f);
        sendbit(t&0x08,f); sendbit(t&0x04,f); sendbit(t&0x02,f); sendbit(t&0x01,f);
      }
      k=8;
    } else if(!((t^treelast)&~0x7F)) {
      if(f) {
        sendbit(0,f); sendbit(1,f);
        sendbit(t&0x40,f); sendbit(t&0x20,f); sendbit(t&0x10,f);
        sendbit(t&0x08,f); sendbit(t&0x04,f); sendbit(t&0x02,f); sendbit(t&0x01,f);
      }
      k=9;
    } else if(!((t^treelast)&~0xFF)) {
      if(f) {
        sendbit(1,f); sendbit(1,f); sendbit(0,f);
        sendbit(t&0x80,f); sendbit(t&0x40,f); sendbit(t&0x20,f); sendbit(t&0x10,f);
        sendbit(t&0x08,f); sendbit(t&0x04,f); sendbit(t&0x02,f); sendbit(t&0x01,f);
      }
      k=11;
    }
    if(t<256) treelast=t;
    return k+1;
  } else {
    if(f) sendbit(1,f);
    k=send_tree(mchain0[t-260],f);
    return k+send_tree(mchain1[t-260],f)+1;
  }
}

static uint32_t send_huffed(const uint8_t*data,uint8_t rlebits,unsigned int maxlz77,char mode,uint32_t nb,const uint8_t*filter) {
  uint8_t va[256];
  uint32_t at,aw,ax,ay,az,bw,bx,by,bz,cx,cz,qq;
  uint32_t wh=width*height;
  int i,j;
  FILE*f=0;
  if(mode) {
    f=asn1_primitive_stream(enc,ASN1_UNIVERSAL,ASN1_BIT_STRING);
    if(!f) err(1,"Unexpected error");
    fputc(7&-nb,f);
    bitbuf=bitpos=0;
  } else {
    for(i=0;i<260;i++) mcount[i]=0;
    nb=1;
  }
  while(*filter) {
    if(mode) {
      sendbit(1,f); sendbit(*filter&1,f); sendbit(*filter&2,f); sendbit(*filter&4,f);
    }
    filter++;
    nb+=4;
  }
  if(mode) sendbit(0,f);
  treelast=0x40;
  nb+=send_tree(root,f);
  if(mchainr[256]>0 || mchainr[257]>0 || mchainr[258]>0 || mchainr[259]>0) {
    nb+=2;
    if(mode) {
      if(rlebits<1 || rlebits>4) errx(1,"Improper number of RLE bits");
      sendbit(rlebits&2,f);
      sendbit(rlebits&1,f);
    }
  }
  for(i=0;i<256;i++) va[i]=i;
  for(at=0;at<wh;) {
    j=data[at];
    aw=ax=ay=az=bw=bx=by=bz=cx=cz=0;
    if(at && mchainr[256]>=0 && data[at]==data[at-1]) {
      for(ax=1;at+ax<wh && data[at-1]==data[at+ax];ax++);
      az=(mlength[256]+rlebits+1)/mlength[data[at]];
    }
    if(at>=width && mchainr[257]>=0) {
      for(;at+ay<wh && data[at+ay-width]==data[at+ay];ay++);
      if(ay && mlength[257]+rlebits+1>=mlength[j]) aw=1; else aw=0;
    }
    if(maxlz77 && mchainr[258]>=0) {
      for(bz=2;bz<maxlz77 && bz<=at;bz++) {
        if(bz!=width && data[at-bz]==data[at]) {
          qq=mlength[data[at]];
          for(bx=1;ax+at<wh && data[at+bx-bz]==data[at+bx];bx++) qq+=mlength[data[at+bx]];
          if(bx<=by || qq<mlength[258]+2*rlebits) continue;
          by=bx; bw=bz;
        }
      }
    }
    if(mchainr[259]>=0 && at>=width) {
      for(cx=0;at+cx<wh && data[at+cx]==va[data[at+cx-width]];cx++);
      cz=1; // if(cx>1) cz=(mlength[259]+2)/(mlength[data[at]]+mlength[data[at+1]])+1;
    }
    if(by>ax+minlz77 && by>ay+minlz77) j=258;
    if(ax>az) j=256;
    if(ay>ax && ay>aw) j=257;
    if(cx>1 && cx>ax && cx>ay && cx>=by) j=259;
    //if(mode) printf("j=%d ax=%d ay=%d az=%d aw=%d cx=%d\n",(int)j,(int)ax,(int)ay,(int)az,(int)aw,(int)cx);
    if(mode) {
      send_code(j,f);
    } else {
      mcount[j]++;
      nb+=mlength[j];
    }
    if(j==256) {
      nb+=send_rle_number(ax-az,rlebits,f);
      at+=ax;
    } else if(j==257) {
      nb+=send_rle_number(ay-aw,rlebits,f);
      at+=ay;
    } else if(j==258) {
      if(bw>width) bw--;
      nb+=send_rle_number(bw-1,rlebits,f);
      nb+=send_rle_number(by-1,rlebits,f);
      at+=by;
    } else if(j==259) {
      nb+=send_rle_number(cx-cz,1,f);
      at+=cx;
    } else {
      if(at>=width && data[at-width]!=j) va[data[at-width]]=j;
      at++;
    }
  }
  if(mode) {
    if(bitpos) fputc(bitbuf,f);
    asn1_end(enc);
  }
  return nb;
}

static void load_picture(const char*name) {
  RGB rgb;
  int c,i;
  uint32_t n,s,t;
  uint8_t buf[16];
  FILE*f;
  if(*name=='-' && !name[1]) f=stdin;
  else if(*name=='|') f=popen(name+1,"r");
  else f=fopen(name,"r");
  if(!f) err(1,"Cannot open picture file");
  c=fgetc(f);
  if(c=='P' && fgetc(f)=='K' && fgetc(f)=='M' && !fgetc(f)) {
    int pb=fgetc(f);
    int pw=fgetc(f);
    width=fgetc(f); width|=fgetc(f)<<8;
    height=fgetc(f); height|=fgetc(f)<<8;
    if(width<1 || width>640 || height<1 || height>350) errx(1,"Improper dimensions");
    for(i=0;i<256;i++) {
      palette[i].r=fgetc(f);
      palette[i].g=fgetc(f);
      palette[i].b=fgetc(f);
    }
    c=fgetc(f); c|=fgetc(f)<<8;
    if(c<0) errx(1,"Unexpected end of file");
    while(c--) fgetc(f);
    s=width*height;
    for(n=t=0;t<s;t++) {
      if(n) {
        n--;
      } else {
        c=fgetc(f);
        if(c==pb) {
          c=fgetc(f);
          if(n=fgetc(f)) n--;
        } else if(c==pw) {
          c=fgetc(f);
          n=fgetc(f)<<8; n|=fgetc(f);
          if(n) n--;
        }
        palette[c].use=1;
      }
      picture[t]=c;
    }
    pkind=KIND_NORMAL;
  } else if(c=='f' && fread(buf,1,15,f)==15 && !memcmp(buf,"arbfeld\x00",9) && !buf[11] && !buf[12]) {
    width=(buf[9]<<8)|buf[10];
    height=(buf[13]<<8)|buf[14];
    if(width<1 || width>640 || height<1 || height>350) errx(1,"Improper dimensions");
    s=width*height;
    rgb.use=1;
    for(n=t=0;t<s;t++) {
      fread(buf,1,8,f);
      rgb.r=buf[0]>>2; rgb.g=buf[2]>>2; rgb.b=buf[4]>>2;
      for(i=0;i<n;i++) if(rgb.r==palette[i].r && rgb.g==palette[i].g && rgb.b==palette[i].b) break;
      if(i==n) {
        if(n==256) errx(1,"Too many colours");
        palette[i]=rgb;
        n++;
      }
      picture[t]=i;
    }
    pkind=KIND_NORMAL;
  } else {
    errx(1,"Unrecognized file format");
  }
  if(*name=='|') pclose(f); else if(*name!='-') fclose(f);
}

static void load_zbuffer(const char*name) {
  int c,i;
  uint32_t n,s,t;
  uint8_t buf[16];
  FILE*f;
  if(*name=='-' && !name[1]) f=stdin;
  else if(*name=='|') f=popen(name+1,"r");
  else f=fopen(name,"r");
  if(!f) err(1,"Cannot open picture file");
  c=fgetc(f);
  if(c=='P' && fgetc(f)=='K' && fgetc(f)=='M' && !fgetc(f)) {
    int pb=fgetc(f);
    int pw=fgetc(f);
    n=fgetc(f); n|=fgetc(f)<<8; if(n!=width) errx(1,"Dimension mismatch");
    n=fgetc(f); n|=fgetc(f)<<8; if(n!=height) errx(1,"Dimension mismatch");
    for(i=0;i<256;i++) fread(buf,1,3,f);
    c=fgetc(f); c|=fgetc(f)<<8;
    if(c<0) errx(1,"Unexpected end of file");
    while(c--) fgetc(f);
    s=width*height;
    for(n=t=0;t<s;t++) {
      if(n) {
        n--;
      } else {
        c=fgetc(f);
        if(c==pb) {
          c=fgetc(f);
          if(n=fgetc(f)) n--;
        } else if(c==pw) {
          c=fgetc(f);
          n=fgetc(f)<<8; n|=fgetc(f);
          if(n) n--;
        }
      }
      zbuffer[t]=c;
    }
    zkind=KIND_NORMAL;
  } else if(c=='f' && fread(buf,1,15,f)==15 && !memcmp(buf,"arbfeld\x00",9) && !buf[11] && !buf[12]) {
    if(width!=((buf[9]<<8)|buf[10]) || height!=((buf[13]<<8)|buf[14])) errx(1,"Dimension mismatch");
    s=width*height;
    for(n=t=0;t<s;t++) {
      fread(buf,1,8,f);
      zbuffer[t]=*buf;
    }
    zkind=KIND_NORMAL;
  } else {
    errx(1,"Unrecognized file format");
  }
  if(*name=='|') pclose(f); else if(*name!='-') fclose(f);
}

static void load_raw(const char*name,uint8_t*data) {
  size_t n;
  FILE*f;
  if(*name=='-' && !name[1]) f=stdin;
  else if(*name=='|') f=popen(name+1,"r");
  else f=fopen(name,"r");
  if(!f) err(1,"Cannot open raw file");
  n=fread(data,1,width*350,f);
  if(!n) errx(1,"Cannot read data from raw file");
  if(n%width) errx(1,"Raw data is not a multiple of width");
  height=n/width;
  if(height>350) errx(1,"Raw file is too big");
  if(*name=='|') pclose(f); else if(*name!='-') fclose(f);
}

static void palette_adjust(uint8_t a) {
  RGB rgb[256];
  uint32_t s,t;
  if(!a) return;
  s=width*height;
  for(t=0;t<s;t++) picture[t]+=a;
  memcpy(rgb+a,palette,(256-a)*sizeof(RGB));
  memcpy(rgb,palette+256-a,a*sizeof(RGB));
  memcpy(palette,rgb,256*sizeof(RGB));
}

static void input_ega(const uint8_t*p,uint16_t lo,uint16_t hi) {
  int i;
  for(i=lo;i<=hi;i++) {
    inpal[i]=(RGB){(*p&040?0x55:0)+(*p&04?0xAA:0),(*p&020?0x55:0)+(*p&02?0xAA:0),(*p&010?0x55:0)+(*p&01?0xAA:0),1};
    p++;
  }
}

static void input_vga(const uint8_t*p,uint16_t lo,uint16_t hi) {
  int i;
  for(i=lo;i<=hi;i++) {
    inpal[i]=(RGB){p[0],p[1],p[2],1};
    p+=3;
  }
}

static void input_palette(const char*name) {
  uint8_t buf[385];
  FILE*f;
  if(*name=='-' && !name[1]) f=stdin;
  else if(*name=='|') f=popen(name+1,"r");
  else f=fopen(name,"r");
  if(!f) err(1,"Cannot open palette file");
  switch(fread(buf,1,385,f)) {
    case 16:
      if(inpal[0x30].use) errx(1,"Palette already in use");
      input_ega(buf,0x30,0x40);
      break;
    case 48:
      if(inpal[0x30].use) errx(1,"Palette already in use");
      input_vga(buf,0x30,0x40);
      break;
    case 64:
      if(inpal[0x40].use) errx(1,"Palette already in use");
      input_ega(buf,0x40,0x80);
      break;
    case 128:
      if(inpal[0x80].use) errx(1,"Palette already in use");
      input_ega(buf,0x80,0x100);
      break;
    case 192:
      if(inpal[0x40].use) errx(1,"Palette already in use");
      input_vga(buf,0x40,0x80);
      break;
    case 384:
      if(inpal[0x80].use) errx(1,"Palette already in use");
      input_vga(buf,0x80,0x100);
      break;
    default: errx(1,"Unrecognized file format");
  }
  if(*name=='|') pclose(f); else if(*name!='-') fclose(f);
  if(*buf&0xC0) errx(1,"Unrecognized file format");
}

static void name_palette(const char*n) {
  char*s=strrchr(n,'/');
  int i;
  if(s) n=s;
  if(!*n) return;
  if(palname[2].name[0]) errx(1,"Too many palette names");
  s=palname[palname[1].name[0]?2:palname[0].name[0]?1:0].name;
  for(i=0;i<8;i++) {
    if(n[i]>='a' && n[i]<='z') s[i]=n[i]+'A'-'a';
    else if(n[i]>='A' && n[i]<='Z') s[i]=n[i];
    else if(n[i]>='0' && n[i]<='9') s[i]=n[i];
    else if(n[i]=='_' || n[i]=='-') s[i]=n[i];
    else if(n[i]=='.' || !n[i]) break;
    else errx(1,"Improper character in palette name");
  }
  if(n[i] && n[i]!='.') errx(1,"Too long palette name");
}

static void out_palette(const char*n,int i) {
  FILE*f;
  int lo,hi;
  int j=0;
  int k=strtol(n,0,10);
  n=strchr(n,'=');
  if(!n) errx(1,"Improper syntax");
  switch(k) {
    case 16: lo=0x30; hi=0x40; break;
    case 64: lo=0x40; hi=0x80; break;
    case 128: lo=0x80; hi=0x100; break;
    default: errx(1,"Palette type %d is not valid",k);
  }
  n++;
  if(i) name_palette(n);
  for(i=lo;!j && i<hi;i++) j=(palette[i].r%0x15)|(palette[i].g%0x15)|(palette[i].b%0x15);
  f=fopen(n,"w");
  if(!f) err(1,"Error opening \"%s\"",n);
  if(j) {
    for(i=lo;i<hi;i++) {
      fputc(palette[i].r,f);
      fputc(palette[i].g,f);
      fputc(palette[i].b,f);
    }
  } else {
    for(i=lo;i<hi;i++) fputc("\000\040\004\044"[palette[i].r&3]+"\000\020\002\022"[palette[i].g&3]+"\000\010\001\011"[palette[i].b&3],f);
  }
  fclose(f);
}

static void apply_palette(void) {
  uint8_t v[256]={};
  uint32_t s,t;
  if(pkind!=KIND_NORMAL) return;
  for(t=0;t<256;t++) if(palette[t].use) {
    for(s=0x30;s<256;s++) if(inpal[s].use && inpal[s].r==palette[t].r && inpal[s].g==palette[t].g && inpal[s].b==palette[t].b) {
      v[t]=s;
      break;
    }
    if(s==256) errx(1,"Color not present in palette");
  }
  s=width*height;
  for(t=0;t<s;t++) if(!(picture[t]=v[picture[t]])) errx(1,"Color not present in palette");
}

static void set_filter(const char*t,uint8_t*f) {
  int i;
  if(*t=='0' && !t[1]) {
    *f=0;
    return;
  }
  for(i=0;t[i] && i<31;i++) {
    if(t[i]<'1' || t[i]>'8') errx(1,"Invalid filter");
    f[i]=t[i]-'0';
  }
  if(t[i]) errx(1,"Too many filters");
  f[i]=0;
}

static void apply_filter(uint8_t*p,uint8_t k) {
  uint8_t*q;
  uint32_t s=width*height;
  uint32_t t,x,y;
  uint8_t u,v,w;
  //fprintf(stderr,"k=%d  ",k);
  //make_mcount(p); t=0; for(x=0;x<256;x++) t=(1234567*t+mcount[x])^5; fprintf(stderr,"%08lX ",(unsigned long)t);
  switch(k) {
    case 1: // Reverse
      for(t=0;t<s/2;t++) u=p[t],p[t]=p[s-t-1],p[s-t-1]=u;
      break;
    case 2: // XOR vertical
      for(t=s-1;t>=width;t--) p[t]^=p[t-width];
      break;
    case 3: // XOR horizontal
      for(t=s-1;t;t--) p[t]^=p[t-1];
      break;
#if 0
    case 4: // Divide with XOR
      if((width|height)&1) errx(1,"Filter requires even dimensions");
      q=malloc(640*480);
      if(!q) err(1,"Allocation failed");
      for(t=1;t<height;t+=2) for(x=0;x<width;x++) p[t*width+x]^=p[(t-1)*width+x];
      for(t=0;t<s;t+=2) p[t+1]^=p[t];
      for(t=0;t<height/2;t++) memcpy(q+t*width,p+2*t*width,width),memcpy(q+(t+height/2)*width,p+(2*t+1)*width,width);
      for(t=0;t<height;t++) for(x=0;x<width/2;x++) p[t*width+x]=q[t*width+2*x],p[t*width+x+width/2]=q[t*width+2*x+1];
      free(q);
      break;
    case 5: // Divide without XOR
      if((width|height)&1) errx(1,"Filter requires even dimensions");
      q=malloc(640*480);
      if(!q) err(1,"Allocation failed");
      for(t=0;t<height/2;t++) memcpy(q+t*width,p+2*t*width,width),memcpy(q+(t+height/2)*width,p+(2*t+1)*width,width);
      for(t=0;t<height;t++) for(x=0;x<width/2;x++) p[t*width+x]=q[t*width+2*x],p[t*width+x+width/2]=q[t*width+2*x+1];
      free(q);
      break;
#endif
    case 6: // LOCO-I prediction
      for(t=s-1;t;t--) {
        u=p[t%width?t-1:t-width]; v=p[t>=width?t-width:t-1]; w=p[t>=width+1?t-width-1:t-1];
        if(w>=u && w>=v) w=(u<v?u:v); else if(w<=u && w<=v) w=(u>v?u:v); else w=u+v-w;
        p[t]-=w;
      }
      break;
    default: errx(1,"Invalid filter");
  }
  //make_mcount(p); t=0; for(x=0;x<256;x++) t=(1234567*t+mcount[x])^5; fprintf(stderr,"%08lX\n",(unsigned long)t);
}

int main(int argc,char**argv) {
  int c;
  while((c=getopt(argc,argv,"+B:E:F:I:J:K:M:O:P:R:S:TUY:a:b:e:f:i:j:k:m:n:o:p:r:s:uvw:y:z:"))>0) switch(c) {
    case 'B': zrlebits=strtol(optarg,0,10); break;
    case 'E': zeffort=strtol(optarg,0,10); break;
    case 'F': set_filter(optarg,zfilter); break;
    case 'I': input_palette(optarg); name_palette(optarg); break;
    case 'J': zrleguess=strtol(optarg,0,10); break;
    case 'K': zupguess=strtol(optarg,0,10); break;
    case 'M': zlz77=strtol(optarg,0,10); break;
    case 'O': out_palette(optarg,1); break;
    case 'P': load_zbuffer(optarg); break;
    case 'R': zkind=KIND_NORMAL; load_raw(optarg,zbuffer); break;
    case 'S': zkind=KIND_SOLID; memset(zbuffer,strtol(optarg,0,16),640*350); break;
    case 'T': testmode=1; break;
    case 'U': zuncomp=1; break;
    case 'Y': zvaguess=strtol(optarg,0,10); break;
    case 'a': palette_adjust(strtol(optarg,0,16)); break;
    case 'b': picrlebits=strtol(optarg,0,10); break;
    case 'e': piceffort=strtol(optarg,0,10); break;
    case 'f': set_filter(optarg,pfilter); break;
    case 'i': input_palette(optarg); break;
    case 'j': picrleguess=strtol(optarg,0,10); break;
    case 'k': picupguess=strtol(optarg,0,10); break;
    case 'm': piclz77=strtol(optarg,0,10); break;
    case 'n': name_palette(optarg); break;
    case 'o': out_palette(optarg,0); break;
    case 'p': load_picture(optarg); break;
    case 'r': pkind=KIND_NORMAL; load_raw(optarg,picture); break;
    case 's': pkind=KIND_SOLID; memset(picture,strtol(optarg,0,16),640*350); break;
    case 'u': picuncomp=1; break;
    case 'v': showinfo=1; break;
    case 'w': width=strtol(optarg,0,10); if(width<1 || width>640 || pkind==KIND_NORMAL || zkind) errx(1,"Improper width"); break;
    case 'y': picvaguess=strtol(optarg,0,10); break;
    case 'z': minlz77=strtol(optarg,0,10); break;
    default: errx(1,"Improper switch");
  }
  if(!pkind) errx(1,"Missing picture");
  if(inpal[0x30].use|inpal[0x40].use|inpal[0x80].use) apply_palette();
  if(piceffort && !picrleguess && !picupguess) picrleguess=picupguess=42;
  if(zeffort && !zrleguess && !zupguess) zrleguess=zupguess=42;
  if(showinfo) {
    printf("Palette usage:\n 0123456789ABCDEF");
    for(c=0;c<256;c++) {
      if(!(c&15)) printf("\n%X",c>>4);
      putchar(palette[c].use?'#':'.');
    }
    putchar('\n');
  }
  enc=showinfo?asn1_start_encoding_value(malloc(sizeof(ASN1_Value))):asn1_start_encoding_file(stdout);
  asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    if(width!=640 && (pkind==KIND_NORMAL || zkind==KIND_NORMAL)) asn1_encode_integer(enc,width);
    if(pkind==KIND_NORMAL) {
      if(picuncomp) {
        asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,picture,width*height);
      } else {
        if(*pfilter) for(c=0;pfilter[c];c++) apply_filter(picture,pfilter[c]);
        make_mcount(picture); mcount[256]=picrleguess; mcount[257]=picupguess; mcount[259]=picvaguess;
        if(piclz77) mcount[258]=10L*width+2;
        make_huffman_codes();
        while(piceffort--) {
          send_huffed(picture,picrlebits,piclz77,0,0,pfilter);
          if(piclz77 && !mcount[258]) mcount[258]=width+2;
          make_huffman_codes();
        }
        send_huffed(picture,picrlebits,piclz77,1,send_huffed(picture,picrlebits,piclz77,0,0,pfilter),pfilter);
      }
    } else if(pkind==KIND_SOLID) {
      asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,picture,1);
    }
    for(c=0;c<3;c++) if(palname[c].name[0]) asn1_encode_c_string(enc,ASN1_VISIBLE_STRING,palname[c].name);
    if(zkind==KIND_NORMAL) {
      if(zuncomp) {
        asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,zbuffer,width*height);
      } else {
        if(*zfilter) for(c=0;zfilter[c];c++) apply_filter(zbuffer,zfilter[c]);
        make_mcount(zbuffer); mcount[256]=zrleguess; mcount[257]=zupguess; mcount[259]=zvaguess;
        if(zlz77) mcount[258]=10L*width+2;
        make_huffman_codes();
        while(zeffort--) {
          send_huffed(zbuffer,zrlebits,zlz77,0,0,zfilter);
          if(zlz77 && !mcount[258]) mcount[258]=width+2;
          make_huffman_codes();
        }
        send_huffed(zbuffer,zrlebits,zlz77,1,send_huffed(zbuffer,zrlebits,zlz77,0,0,zfilter),zfilter);
      }
    } else if(zkind==KIND_SOLID && *zbuffer) {
      asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,zbuffer,1);
    }
  asn1_end(enc);
  asn1_finish_encoder(enc);
  return 0;
}
