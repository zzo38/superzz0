#if 0
gcc -s -O2 -o ./cbgconv cbgconv.c -Wno-unused-result -Wno-multichar
exit
#endif

// Convert character-based graphics

#define _GNU_SOURCE
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Err_System 6
#define Err_Argument 7
#define Err_Data 8
#define Err_Others 9

#define Video_BrightBG 0x01
#define Video_NineDots 0x02
#define Video_Underline 0x04
#define Video_Set 0x80

#define Pal_None 0
#define Pal_EGA 1 // internally stored as VGA format
#define Pal_VGA 2
#define Pal_24bits 3

typedef struct {
  uint8_t ch,co;
} Tile;

typedef struct {
  uint8_t r,g,b;
} Palette;

enum {
  TileStructureSize=2/(sizeof(Tile)==2)
};

static FILE*infile;
static FILE*outfile;
static uint8_t intype,outtype;
static int32_t option[128];
static uint16_t nrows,ncolumns;
static Tile*playfield;
static uint8_t fontheight;
static uint8_t*fontdata;
static uint8_t font512;
static uint8_t videomode;
static Palette palette[16];
static uint8_t paltype;
static int32_t csi[32];
static uint8_t sauce[128];

typedef struct {
  const char*name;
  void(*call)(const char*arg);
} Filters;

#define ReqArg if(!arg) errx(Err_Argument,"Required argument missing");

static const char*read_options(const char*arg) {
  memset(option,0,sizeof(option));
  if(!arg) return 0;
  while(*arg && *arg!='=') {
    if(*arg==',' || *arg==' ') {
      arg++;
      continue;
    }
    if((*arg<'A' || *arg>'Z') && (*arg<'a' && *arg>'z') && *arg!='_' && *arg!='.' && *arg!='/') errx(Err_Argument,"Improper options");
    option[*arg]=strtol(arg+1,(char**)&arg,0);
  }
  return (*arg=='='?arg+1:(const char*)0);
}

static void make_sauce(uint8_t DataType,uint8_t FileType,uint8_t TInfo1,uint8_t TInfo2,uint8_t TInfo3,uint8_t TInfo4) {
  memcpy(sauce,"SAUCE00",7);
  memset(sauce+7,32,83);
  memset(sauce+90,0,38);
  sauce[94]=DataType;
  sauce[95]=FileType;
  sauce[96]=TInfo1; sauce[97]=TInfo1>>8;
  sauce[98]=TInfo2; sauce[99]=TInfo2>>8;
  sauce[100]=TInfo3; sauce[101]=TInfo3>>8;
  sauce[102]=TInfo4; sauce[102]=TInfo4>>8;
  sauce[105]=(videomode&Video_BrightBG?1:0);
  if(videomode&Video_Set) sauce[105]|=(videomode&Video_NineDots?4:2);
}

// *** File controls

static void in_f(const char*arg) {
  ReqArg;
  if(intype==1) fclose(infile); else if(intype==2) pclose(infile);
  infile=fopen(arg,"r");
  if(!infile) err(Err_System,"Cannot open input file \"%s\"",arg);
  intype=1;
}

static void in_p(const char*arg) {
  ReqArg;
  if(intype==1) fclose(infile); else if(intype==2) pclose(infile);
  infile=popen(arg,"r");
  if(!infile) err(Err_System,"Cannot open input pipe \"%s\"",arg);
  intype=2;
}

static void out_f(const char*arg) {
  ReqArg;
  if(outtype==1) fclose(outfile); else if(outtype==2) pclose(outfile);
  outfile=fopen(arg,"w");
  if(!outfile) err(Err_System,"Cannot open output file \"%s\"",arg);
  outtype=1;
}

static void out_p(const char*arg) {
  ReqArg;
  if(outtype==1) fclose(outfile); else if(outtype==2) pclose(outfile);
  outfile=popen(arg,"w");
  if(!outfile) err(Err_System,"Cannot open output pipe \"%s\"",arg);
  outtype=2;
}

// *** Input formats

static uint16_t ansi_vscroll(uint16_t y) {
  uint16_t x;
  if(y<nrows) return y;
  if(y>nrows || nrows>32767) errx(Err_Data,"Too many lines in ANSI file");
  if(option['y']) {
    if(option['v']) memmove(playfield,playfield+ncolumns,2L*(nrows-1));
    y--;
  } else {
    playfield=realloc(playfield,2L*++nrows*ncolumns);
    if(!playfield) err(Err_System,"Allocation failed");
  }
  for(x=0;x<ncolumns;x++) playfield[y*ncolumns+x]=(Tile){option['b'],option['c']?:7};
  return y;
}

static uint8_t ansi_csi(void) {
  int c;
  int i=0;
  uint32_t n=0;
  for(;;) {
    c=fgetc(infile);
    if(c==';') {
      csi[i]=n;
      if(++i==32) goto bad;
      n=0;
    } else if(c>='0' && c<='9') {
      n=10*n+c-'0';
    } else {
      break;
    }
  }
  csi[i]=n;
  return c;
  bad:
  while(fgetc(infile)/16==3);
  return 0;
}

static void ansi_erase(uint32_t a,uint32_t b,Tile t) {
  if(a>=ncolumns*nrows) a=ncolumns*nrows-1;
  if(b>=ncolumns*nrows) b=ncolumns*nrows-1;
  while(a<b) playfield[a++]=t;
}

static void in_ansi(const char*arg) {
  int c,d,i,r;
  uint8_t color;
  uint8_t sgr=0;
  uint8_t rev=0;
  uint16_t x=0;
  uint16_t y=0;
  uint16_t xs=0;
  uint16_t ys=0;
  arg=read_options(arg);
  ncolumns=option['x']?:80;
  nrows=option['y']?:1;
  color=option['c']?:7;
  if(option['S']) {
    if(intype!=1) errx(Err_Argument,"SAUCE cannot be read from pipe");
    *sauce=0;
    fseek(infile,-128,SEEK_END);
    fread(sauce,128,1,infile);
    rewind(infile);
    if(memcmp(sauce,"SAUCE00",7)) errx(Err_Data,"Improper SAUCE data");
    if(sauce[94]) {
      if(memcmp(sauce+94,"\x01\x01",2)) errx(Err_Data,"SAUCE specifies non-ANSI file type");
      if((sauce[96]|sauce[97]) && !option['x']) ncolumns=sauce[96]|(sauce[97]<<8);
      if((sauce[98]|sauce[99]) && !option['y']) option['y']=nrows=sauce[98]|(sauce[99]<<8);
      i=sauce[105];
      videomode|=Video_Set;
      if(i&1) videomode|=Video_BrightBG; else videomode&=~Video_BrightBG;
      i&=6;
      if(i==4) videomode|=Video_NineDots; else if(i==2) videomode&=~Video_NineDots;
    }
  }
  resize:
  free(playfield);
  playfield=malloc(2L*nrows*ncolumns);
  if(!playfield) err(Err_System,"Allocation failed");
  for(y=0;y<nrows;y++) for(x=0;x<ncolumns;x++) playfield[y*ncolumns+x]=(Tile){option['b'],color};
  for(x=y=0;;) switch(c=fgetc(infile)) {
    case EOF: return;
    case 0: if(option['d']) c=fgetc(infile); goto normal;
    case 8: if(option['f']<1) goto normal; if(x) --x; break;
    case 9: if(option['f']<1) goto normal; if((x=(x+8)&8)>=ncolumns) x=0,y=ansi_vscroll(y+1); break;
    case 10: linefeed:
      if(option['e']==1) x=0;
      if(++y==nrows) y=ansi_vscroll(y);
      break;
    case 12:
      if(option['f']<1) goto normal;
      allclear:
      for(y=0;y<nrows;y++) for(x=0;x<ncolumns;x++) playfield[y*ncolumns+x]=(Tile){option['b'],color};
      x=y=0;
      break;
    case 13:
      x=0;
      if(option['e']==2) goto linefeed;
      break;
    case 26: if(option['f']<0) goto normal; return;
    case 27:
      c=fgetc(infile);
      if(c!='[') {
        if(option['f']<0) {
          ungetc(c,infile);
          c=27;
          goto normal;
        }
        break;
      }
      c=fgetc(infile);
      if((c>='0' && c<='9') || c==';' || c>='@') ungetc(c,infile),c='_';
      memset(csi,-1,sizeof(csi));
      d=ansi_csi();
      if(option['D']) printf("(%d,%d)[%c,%c](%d,%d,%d,%d)\n",x,y,c,d,csi[0],csi[1],csi[2],csi[3]);
      switch(d*'\0\1'+c*'\1\0') {
        case '_F':
          x=0; // fall through
        case '_A':
          i=(*csi<=0?1:*csi);
          if(y>i) y-=i; else y=0;
          break;
        case '_E':
          x=0; // fall through
        case '_B':
          i=(*csi<=0?1:*csi);
          if(option['y'] && i+y>=nrows) y=nrows-1; else y+=i;
          if(y>=nrows) {
            // Handle possibility of skipping multiple lines beyond the screen size
            i=nrows-y;
            while(i--) ansi_vscroll(nrows);
            if(y>=nrows) y=nrows-1;
          }
          break;
        case '_C': x+=(*csi<=0?1:*csi); break;
        case '_D': i=(*csi<=0?1:*csi); x=(x>i?x-i:0); break;
        case '_G': x=(*csi<=0?1:*csi-1); break;
        case '_H': case '_f':
          x=(csi[0]<=0?1:csi[0]-1);
          y=(csi[1]<=0?1:csi[1]-1);
          if(x>=ncolumns) x=ncolumns-1;
          if(y>=nrows) y=nrows-1;
          break;
        case '_J':
          if(*csi<0) *csi=0; else if(*csi>=2) goto allclear;
          ansi_erase(*csi?0:y*ncolumns+x,*csi?y*ncolumns+x:nrows*ncolumns,(Tile){option['b'],color});
          break;
        case '_K':
          if(*csi<0) *csi=0; else if(*csi>2) *csi=2;
          if(*csi==0) ansi_erase(y*ncolumns+x,(y+1)*ncolumns,(Tile){option['b'],color});
          if(*csi==1) ansi_erase(y*ncolumns,y*ncolumns+x,(Tile){option['b'],color});
          if(*csi==2) ansi_erase(0,(y+1)*ncolumns,(Tile){option['b'],color});
          break;
        case '_m':
          if(*csi<0) *csi=0;
          for(i=0;i<32 && csi[i]>=0;i++) switch(csi[i]) {
            case 0: color=option['c']?:7,sgr=rev=0; break;
            case 1: color|=0x08,sgr|=0x08; break;
            case 4: if(videomode&Video_Underline) color=(color&0xF8)+1; break;
            case 5: color|=0x80,sgr|=0x80; break;
            case 7: rev=1; break;
            case 8: color=0; break;
            case 22: color&=0xF7,sgr&=0xF7; break;
            case 24: if((videomode&Video_Underline) && (color&7)==1) color+=6; break;
            case 25: color&=0x7F,sgr&=0x7F; break;
            case 27: rev=0; break;
            case 28: if(!color) color=option['c']?:7; break;
            case 30 ... 37: color=(color&0xF0)|sgr|"\x00\x04\x02\x06\x01\x05\x03\x07"[csi[i]-30]; break;
            case 39: color=((option['c']?:7)&0x0F)|sgr|(color&0xF0); break;
            case 40 ... 47: color=(color&0x0F)|sgr|("\x00\x04\x02\x06\x01\x05\x03\x07"[csi[i]-40]<<4); break;
            case 49: color=(option['c']&0xF0)|sgr|(color&0x0F); break;
            case 90 ... 97: color=(color&0xF0)|sgr|"\x00\x04\x02\x06\x01\x05\x03\x07"[csi[i]-90]|0x08; break;
            case 100 ... 107: color=(color&0x0F)|sgr|("\x00\x04\x02\x06\x01\x05\x03\x07"[csi[i]-100]<<4)|0x80; break;
          }
          break;
        case '_s': xs=x; ys=y; break;
        case '_u': x=xs; y=ys; break;
        case '=h': case '=l':
          for(r=i=0;i<32 && csi[i]>=0;i++) {
            if(csi[i]==255) option['d']=(d=='h');
            if(csi[i]==0) r=1,videomode|=Video_Underline,ncolumns=40,option['y']=nrows=25;
            if(csi[i]==1) r=1,videomode&=~Video_Underline,ncolumns=40,option['y']=nrows=25;
            if(csi[i]==2) r=1,videomode|=Video_Underline,ncolumns=80,option['y']=nrows=25;
            if(csi[i]==3) r=1,videomode&=~Video_Underline,ncolumns=80,option['y']=nrows=25;
          }
          if(r) goto resize;
          break;
        case '?h': case '?l':
          for(r=i=0;i<32 && csi[i]>=0;i++) switch(csi[i]) {
            case 3: ncolumns=(d=='h'?132:80); r=1; break;
            case 7: if(d=='h') option['w']|=2; else option['w']&=~2; break;
            case 33: if(d=='h') videomode|=Video_BrightBG; else videomode&=~Video_BrightBG; break;
          }
          if(r) goto resize;
          break;
        case '=\x7B':
          // This is supposed to load a font that can then be selectable, but this
          // program does not implement selectable fonts, so it always overrides
          // any existing font unconditionally, instead. (The newer base64 format
          // is not implemented.)
          fontheight=(csi[1]==1?14:csi[1]==2?8:16);
          free(fontdata);
          font512=0;
          fontdata=malloc(256*fontheight);
          if(!fontdata) err(Err_System,"Allocation failed");
          fread(fontdata,fontheight,256,infile);
          break;
      }
      break;
    default: normal:
      if(y>=nrows) y=ansi_vscroll(y);
      if(x>=ncolumns) {
        if(option['w']&2) {
          x=ncolumns-1;
        } else {
          x=0;
          if(++y==nrows) y=ansi_vscroll(y);
        }
      }
      playfield[y*ncolumns+x++]=(Tile){c,rev?(color>>4)|(color<<4):color};
      if(x>=ncolumns && !option['w']) {
        x=0;
        if(++y==nrows) y=ansi_vscroll(y);
      }
  }
}

static void in_artworx(const char*arg) {
  int i,j;
  arg=read_options(arg);
  if(!option['x']) option['x']=80;
  if(!option['y']) {
    if(intype!=1) errx(Err_Argument,"Height cannot be read from pipe");
    fseek(infile,0,SEEK_END);
    option['y']=(ftell(infile)-4289)/(2*option['x']);
    rewind(infile);
  }
  if(!videomode) videomode=Video_BrightBG;
  paltype=Pal_VGA;
  font512=0;
  if(fontheight!=16) {
    fontdata=realloc(fontdata,4096);
    if(!fontdata) err(Err_System,"Allocation failed");
    fontheight=16;
  }
  if(nrows!=option['y'] || ncolumns!=option['x']) {
    nrows=option['y']; ncolumns=option['x'];
    playfield=realloc(playfield,2L*nrows*ncolumns);
    if(!playfield) err(Err_System,"Allocation failed");
  }
  fgetc(infile); // version number (ignored)
  // The palette is in VGA format but is indexed by the EGA numbers of the standard PC colours.
  // (I don't know why it works like that.)
  for(i=0;i<64;i++) {
    if(i<8 && i!=6) j=i; else if(i==20) j=6; else if(i>55) j=i-48; else j=-1;
    if(j<0) {
      fgetc(infile); fgetc(infile); fgetc(infile);
      continue;
    }
    palette[j].r=fgetc(infile);
    palette[j].g=fgetc(infile);
    palette[j].b=fgetc(infile);
  }
  fread(fontdata,1,4096,infile);
  fread(playfield,2L*nrows*ncolumns,1,infile);
}

static void in_bin(const char*arg) {
  int i;
  arg=read_options(arg);
  if(option['S']) {
    if(intype!=1) errx(Err_Argument,"SAUCE cannot be read from pipe");
    *sauce=0;
    fseek(infile,-128,SEEK_END);
    fread(sauce,128,1,infile);
    rewind(infile);
    if(memcmp(sauce,"SAUCE00",7)) errx(Err_Data,"Improper SAUCE data");
    if(sauce[94]) {
      if(sauce[94]!=5) errx(Err_Data,"SAUCE specifies wrong file type");
      option['x']=sauce[95]*2;
      i=sauce[105];
      videomode|=Video_Set;
      if(i&1) videomode|=Video_BrightBG; else videomode&=~Video_BrightBG;
      i&=6;
      if(i==4) videomode|=Video_NineDots; else if(i==2) videomode&=~Video_NineDots;
    }
  }
  if(!option['x']) option['x']=160;
  if(!option['y']) {
    if(intype!=1) errx(Err_Argument,"Height cannot be read from pipe");
    fseek(infile,0,SEEK_END);
    option['y']=ftell(infile)/(2*option['x']);
    rewind(infile);
  }
  if(!option['y']) errx(Err_Data,"File is too small");
  if((option['x']|option['y'])&~0x7FFF) errx(Err_Data,"Width/height are out of range");
  free(playfield);
  nrows=option['y'];
  ncolumns=option['x'];
  playfield=malloc(2L*nrows*ncolumns);
  if(!playfield) err(Err_System,"Allocation failed");
  fread(playfield,2L*nrows*ncolumns,1,infile);
}

static void in_chr(const char*arg) {
  arg=read_options(arg);
  if(option['n']<=0) option['n']=256;
  if(!option['h']) {
    if(intype!=1) errx(Err_Argument,"Height cannot be read from pipe");
    fseek(infile,0,SEEK_END);
    option['h']=ftell(infile)/option['n'];
    rewind(infile);
  }
  if(option['n']<1 || (option['n']>256 && option['n']!=512)) errx(Err_Argument,"Number of characters in font is incorrect");
  if((option['o']&~255) || (option['o'] && option['o']+option['n']>256)) errx(Err_Argument,"Improper font offset");
  if(option['h']<1 || option['h']>32) errx(Err_Data,"Font height is not in range 1 to 32");
  if(option['h']!=fontheight || font512!=option['n']/512 || !fontdata) {
    if(option['o'] || (option['n']&255)) errx(Err_Data,"Partial fonts cannot be used since a full font is not loaded yet");
    free(fontdata);
    fontdata=malloc(option['n']*option['h']);
    if(!fontdata) err(Err_System,"Allocation failed");
    font512=option['n']/512;
  }
  fontheight=option['h'];
  fread(fontdata+fontheight*option['o'],fontheight,option['n'],infile);
}

static void in_ega(const char*arg) {
  int i,c;
  paltype=Pal_EGA;
  for(i=0;i<16;i++) {
    c=fgetc(infile);
    palette[i].r=(c&040?0x15:0)+(c&04?0x2A:0);
    palette[i].g=(c&020?0x15:0)+(c&02?0x2A:0);
    palette[i].b=(c&010?0x15:0)+(c&01?0x2A:0);
  }
}

static void in_mzm(const char*arg) {
  uint8_t head[20]={};
  uint32_t at=0;
  arg=read_options(arg);
  if(!fontdata && !playfield && !videomode) videomode=Video_BrightBG;
  if(fread(head,1,16,infile)!=16) errx(Err_Data,"Input past end of file");
  if(memcmp(head,"MZM",3)) errx(Err_Data,"Unrecognized file format");
  if(head[3]=='3') fread(head+16,1,4,infile);
  else if(head[3]=='X') memset(head+6,0,12);
  else if(head[3]!='2') errx(Err_Data,"Unrecognized MZM version");
  free(playfield);
  nrows=head[6]|(head[7]<<8);
  ncolumns=head[4]|(head[5]<<8);
  if(!nrows || !ncolumns) errx(Err_Data,"Grid size is zero");
  if(head[13]&~1) errx(Err_Data,"Improper MZM storage mode");
  playfield=malloc(2L*nrows*ncolumns);
  if(!playfield) err(Err_System,"Allocation failed");
  for(at=0;at<nrows*ncolumns;at++) {
    if(!head[13]) fgetc(infile);
    playfield[at].ch=fgetc(infile);
    playfield[at].co=fgetc(infile);
    if(!head[13]) {
      fgetc(infile);
      if(option['u']) {
        playfield[at].ch=fgetc(infile);
        playfield[at].co=fgetc(infile);
      } else {
        fgetc(infile);
        fgetc(infile);
      }
    }
  }
}

static void in_vga(const char*arg) {
  int i;
  paltype=Pal_VGA;
  for(i=0;i<16;i++) {
    palette[i].r=fgetc(infile);
    palette[i].g=fgetc(infile);
    palette[i].b=fgetc(infile);
  }
}

static void in_xbin(const char*arg) {
  // Synchronet extensions of XBIN are not implemented.
  uint8_t head[11];
  int c,i,x,y;
  uint32_t at=0;
  fread(head,1,11,infile);
  if(memcmp(head,"XBIN\x1A",5)) errx(Err_Data,"Unrecognized file format");
  // Mode
  videomode=Video_Set;
  if(head[10]&8) videomode|=Video_BrightBG;
  // Palette
  if(head[10]&1) {
    paltype=Pal_VGA;
    for(i=0;i<16;i++) {
      palette[i].r=fgetc(infile);
      palette[i].g=fgetc(infile);
      palette[i].b=fgetc(infile);
    }
  }
  // Font
  if(head[10]&2) {
    free(fontdata);
    fontheight=head[9];
    if(fontheight<1 || fontheight>32) errx(Err_Data,"Improper font height");
    font512=(head[10]&16?1:0);
    fontdata=malloc((font512?512:256)*fontheight);
    if(!fontdata) err(Err_System,"Allocation failed");
    fread(fontdata,fontheight,font512?512:256,infile);
  }
  // Image
  if(head[5]|head[6]|head[7]|head[8]) {
    free(playfield);
    nrows=head[7]|(head[8]<<8);
    ncolumns=head[5]|(head[6]<<8);
    if(!nrows || !ncolumns) errx(Err_Data,"Grid size is zero");
    playfield=malloc(2L*(nrows*ncolumns+64));
    if(!playfield) err(Err_System,"Allocation failed");
    if(head[10]&4) {
      // Compressed
      for(at=0;at<nrows*ncolumns;) {
        c=fgetc(infile);
        switch(c>>6) {
          case 0: c++; while(c--) fread(playfield+at++,2,1,infile); break;
          case 1:
            playfield[at].ch=fgetc(infile);
            playfield[at].co=fgetc(infile);
            at++;
            c&=0x3F;
            while(c--) {
              playfield[at].ch=playfield[at-1].ch;
              playfield[at++].co=fgetc(infile);
            }
            break;
          case 2:
            playfield[at].co=fgetc(infile);
            playfield[at].ch=fgetc(infile);
            at++;
            c&=0x3F;
            while(c--) {
              playfield[at].co=playfield[at-1].co;
              playfield[at++].ch=fgetc(infile);
            }
            break;
          case 3:
            fread(playfield+at++,2,1,infile);
            c&=0x3F;
            while(c--) playfield[at]=playfield[at-1],at++;
            break;
          default: errx(Err_Data,"Improper data");
        }
      }
    } else {
      // Uncompressed
      fread(playfield,2L*nrows*ncolumns,1,infile);
    }
  }
}

static void in_zzt(const char*arg) {
  static const uint8_t el[256]={
  //  0    1    2    3    4    5    6    7    8    9
    0x20,0x20,0x20,0x20,0x02,0x84,0x9D,0x04,0x0C,0x0A,
    0xE8,0xF0,0xFA,0x0B,0x7F,0xB3,0x2F,0x5C,0xF8,0xB0,
    0xB0,0xDB,0xB2,0xB1,0xFE,0x12,0x1D,0xB2,0x20,0xCE,
    0x5E,0xCE,0x2A,0xCD,0x99,0x05,0x00,0x2A,0x5E,0x18,
    0x1F,0xEA,0xE3,0xBA,0xE9,0x4F,0x20,
  };
  uint8_t istat[128];
  uint8_t bkind[25*60];
  uint32_t at;
  int i,j,k;
  arg=read_options(arg);
  if(nrows!=25 || ncolumns!=60) {
    free(playfield);
    playfield=malloc(2L*25*60);
    if(!playfield) err(Err_System,"Allocation failed");
    nrows=25; ncolumns=60;
  }
  i=fgetc(infile); i|=fgetc(infile)<<8;
  if((i&0x8000) || i<4) errx(Err_Data,"Does not seem to be a ZZT board file");
  fread(istat,1,51,infile); // ignore this data
  // Board grid
  for(at=0;at<60*25;) {
    i=fgetc(infile)?:256; j=fgetc(infile); k=fgetc(infile);
    while(i-- && at<60*25) playfield[at].ch=el[bkind[at]=j&255],playfield[at++].co=k;
  }
  fread(istat,1,86,infile); // ignore this data
  k=fgetc(infile); fgetc(infile);
  // Stats
  for(i=0;i<=k;i++) {
    fread(istat,1,33,infile);
    if(istat[0] && istat[0]<=60 && istat[1] && istat[1]<=25) {
      at=istat[0]+istat[1]*60-61;
      if(!i && option['m']) playfield[at]=(Tile){32,7};
      switch(bkind[at]) {
        case 12: if(istat[8]<6) playfield[at].ch="\xFA\xFA\xF9\xF8oO"[istat[8]]; break;
        case 13: if(istat[8]>1) playfield[at].ch=istat[8]+48; break;
        case 30: playfield[at].ch=(istat[2]&0x80?'(':istat[2]?')':istat[4]&0x80?'^':istat[4]?'v':'^'); break;
        case 36: playfield[at].ch=istat[8]; break;
        case 40: playfield[at].ch=(istat[2]==1?16:istat[2]==255?17:istat[4]==255?30:31); break;
      }
    }
    if((istat[23] || istat[24]) && istat[24]<128) {
      j=istat[23]+(istat[24]<<8);
      while(j>0) fread(istat,1,j>128?128:j,infile),j-=128;
    }
  }
  // Conversion
  for(at=0;at<80*25;at++) switch(bkind[at]) {
    case 0: playfield[at].co=0; break;
    case 31:
      i=0;
      if(at<60 || bkind[at-60]==1 || bkind[at-60]==31) i|=2;
      if(at>1439 || bkind[at+60]==1 || bkind[at+60]==31) i|=8;
      if(at%60==0 || bkind[at-1]==1 || bkind[at-1]==31) i|=4;
      if(at%60==59 || bkind[at+1]==1 || bkind[at+1]==31) i|=1;
      playfield[at].ch="\xF9\xC6\xD0\xC8\xB5\xCD\xBC\xCA\xD2\xC9\xBA\xCC\xBB\xCB\xB9\xCE"[i];
      break;
    case 47 ... 52: case 54 ... 127: playfield[at].ch=playfield[at].co; playfield[at].co=(bkind[at]-46)*16+15; break;
    case 53: playfield[at].ch=playfield[at].co; playfield[at].co=15; break;
    case 128 ... 255: playfield[at].ch=playfield[at].co; playfield[at].co=bkind[at]-128; break;
  }
}

// *** Modifiers

static void do_cancel(const char*arg) {
  ReqArg;
  while(*arg) switch(*arg++) {
    case 'f': free(fontdata); fontdata=0; font512=0; fontheight=0; break;
    case 'g': free(playfield); playfield=0; nrows=ncolumns=0; break;
    case 'm': videomode=0; break;
    case 'p': paltype=0; break;
    default: errx(Err_Argument,"Improper argument");
  }
}

static void do_mode(const char*arg) {
  ReqArg;
  videomode=Video_Set;
  while(*arg) switch(*arg++) {
    case '9': videomode|=Video_NineDots; break;
    case 'b': videomode|=Video_BrightBG; break;
    case 'u': videomode|=Video_Underline; break;
    default: errx(Err_Argument,"Improper video mode");
  }
}

static void do_pcpal(const char*arg) {
  static const Palette p[16]={
    {0x00,0x00,0x00},
    {0x00,0x00,0x2A},
    {0x00,0x2A,0x00},
    {0x00,0x2A,0x2A},
    {0x2A,0x00,0x00},
    {0x2A,0x00,0x2A},
    {0x2A,0x15,0x00},
    {0x2A,0x2A,0x2A},
    {0x15,0x15,0x15},
    {0x15,0x15,0x3F},
    {0x15,0x3F,0x15},
    {0x15,0x3F,0x3F},
    {0x3F,0x15,0x15},
    {0x3F,0x15,0x3F},
    {0x3F,0x3F,0x15},
    {0x3F,0x3F,0x3F},
  };
  paltype=Pal_EGA;
  memcpy(palette,p,sizeof(p));
}

static void do_showfont(const char*arg) {
  int i;
  free(playfield);
  nrows=ncolumns=16;
  playfield=malloc(512);
  if(!playfield) err(Err_System,"Allocation failed");
  for(i=0;i<256;i++) playfield[i]=(Tile){i,7};
}

// *** Output formats

static void out_artworx(const char*arg) {
  int i,j;
  if(!playfield) errx(Err_Data,"Playfield is not available");
  if(!fontdata) errx(Err_Data,"Font is not available");
  if(font512 || fontheight!=16) errx(Err_Data,"Artworx cannot use this font");
  fputc(0,outfile); // version number
  if(paltype==Pal_None) {
    for(i=0;i<64;i++) {
      fputc("\x00\x2A______\x15\x3F"[(i&044)>>2],outfile);
      fputc("\x00\x2A______\x15\x3F"[(i&022)>>2],outfile);
      fputc("\x00\x2A______\x15\x3F"[(i&011)>>2],outfile);
    }
  } else if(paltype==Pal_24bits) {
    for(i=0;i<64;i++) {
      j=(i==20?6:i&15);
      fputc(palette[j].r>>2,outfile);
      fputc(palette[j].g>>2,outfile);
      fputc(palette[j].b>>2,outfile);
    }
  } else {
    for(i=0;i<64;i++) {
      j=(i==20?6:i&15);
      fputc(palette[j].r,outfile);
      fputc(palette[j].g,outfile);
      fputc(palette[j].b,outfile);
    }
  }
  fwrite(fontdata,16,256,outfile);
  fwrite(playfield,2L*nrows*ncolumns,1,outfile);
}

static void out_bin(const char*arg) {
  arg=read_options(arg);
  if(option['S']) {
    if(ncolumns&1) errx(Err_Argument,"SAUCE cannot be applied to BIN with odd number of columns");
    if(ncolumns>510) errx(Err_Argument,"SAUCE cannot be applied to BIN with more than 510 columns");
  }
  if(!playfield) errx(Err_Data,"Playfield is not available");
  fwrite(playfield,2L*nrows*ncolumns,1,outfile);
  if(option['S']) {
    fputc(26,outfile);
    make_sauce(5,ncolumns>>1,0,0,0,0);
    fwrite(sauce,1,128,outfile);
  }
}

static void out_chr(const char*arg) {
  if(!fontdata) errx(Err_Data,"Font is not available");
  fwrite(fontdata,fontheight,font512?512:256,outfile);
}

static void out_ega(const char*arg) {
  int i,j;
  int s=(paltype==Pal_24bits?0:2);
  if(!paltype) errx(Err_Data,"Palette is not available");
  for(i=0;i<16;i++) {
    j=(palette[i].r>>s)&0x20?4:0; j|=(palette[i].r>>s)&0x10?040:0;
    j|=(palette[i].g>>s)&0x20?2:0; j|=(palette[i].g>>s)&0x10?020:0;
    j|=(palette[i].b>>s)&0x20?1:0; j|=(palette[i].b>>s)&0x10?010:0;
  }
}

static void out_farbfeld(const char*arg) {
  static struct {
    uint8_t data[8];
  } colors[16];
  uint32_t x,y;
  uint8_t c,f,i,xx,yy;
  arg=read_options(arg);
  if(!playfield) errx(Err_Data,"Playfield is not available");
  if(!fontdata) errx(Err_Data,"Font is not available");
  if(!paltype) errx(Err_Data,"Palette is not available");
  if(paltype==Pal_24bits) {
    for(i=0;i<16;i++) {
      colors[i].data[0]=colors[i].data[1]=palette[i].r;
      colors[i].data[2]=colors[i].data[3]=palette[i].g;
      colors[i].data[4]=colors[i].data[5]=palette[i].b;
      colors[i].data[6]=colors[i].data[7]=255;
    }
  } else {
    for(i=0;i<16;i++) {
      colors[i].data[0]=colors[i].data[1]=(palette[i].r*65)>>4;
      colors[i].data[2]=colors[i].data[3]=(palette[i].g*65)>>4;
      colors[i].data[4]=colors[i].data[5]=(palette[i].b*65)>>4;
      colors[i].data[6]=colors[i].data[7]=255;
    }
  }
  fwrite("farbfeld",1,8,outfile);
  if(!videomode) videomode=Video_BrightBG;
  x=ncolumns*(videomode&Video_NineDots?9:8);
  y=nrows*fontheight;
  fputc(0,outfile); fputc(0,outfile); fputc(x>>8,outfile); fputc(x,outfile);
  fputc(0,outfile); fputc(0,outfile); fputc(y>>8,outfile); fputc(y,outfile);
  for(y=0;y<nrows;y++) for(yy=0;yy<fontheight;yy++) {
    for(x=0;x<ncolumns;x++) {
      c=playfield[y*ncolumns+x].co;
      i=playfield[y*ncolumns+x].ch;
      f=fontdata[yy+fontheight*(i+(font512&&(c&8)?256:0))];
      if(!(videomode&Video_BrightBG)) {
        if((c&0x80) && option['b']) c=(c>>4)*0x11-0x88; else c&=0x7F;
      }
      if(yy==12 && (videomode&Video_Underline) && (c&7)==1) f=0xFF;
      for(xx=0;xx<8;xx++) fwrite(colors[(c>>((128>>xx)&f?0:4))&15].data,1,8,outfile);
      if(videomode&Video_NineDots) fwrite(colors[(c>>(f&1?(i>191&&i<224?0:4):4))&15].data,1,8,outfile);
    }
  }
}

static void out_info(const char*arg) {
  printf("fontheight=%d\n",fontheight);
  printf("font512=%d\n",font512);
  printf("paltype=%d\n",paltype);
  printf("nrows=%d\n",nrows);
  printf("ncolumns=%d\n",ncolumns);
}

static void out_mzm(const char*arg) {
  if(!playfield) errx(Err_Data,"Playfield is not available");
  fwrite("MZM2",1,3,outfile);
  fputc(ncolumns,outfile); fputc(ncolumns>>8,outfile);
  fputc(nrows,outfile); fputc(nrows>>8,outfile);
  fwrite("\0\0\0\0\0\x01\0\0",1,8,outfile);
  fwrite(playfield,2L*nrows*ncolumns,1,outfile);
}

static void out_vga(const char*arg) {
  int i;
  int s=(paltype==Pal_24bits?0:2);
  if(!paltype) errx(Err_Data,"Palette is not available");
  for(i=0;i<16;i++) {
    fputc(palette[i].r>>s,outfile);
    fputc(palette[i].g>>s,outfile);
    fputc(palette[i].b>>s,outfile);
  }
}

static void out_xbin(const char*arg) {
  int i;
  uint32_t at,w,x,y,z;
  uint32_t hi=nrows*ncolumns;
  arg=read_options(arg);
  if(!nrows || !ncolumns) option['c']=0;
  // Header
  fwrite("XBIN\x1A",1,5,outfile);
  fputc(ncolumns,outfile); fputc(ncolumns>>8,outfile);
  fputc(nrows,outfile); fputc(nrows>>8,outfile);
  fputc(fontheight?:16,outfile);
  fputc((paltype?1:0)+(fontdata?2:0)+(option['c']?4:0)+(videomode&Video_BrightBG?8:0)+(font512?16:0),outfile);
  // Palette
  if(paltype==Pal_24bits) {
    for(i=0;i<16;i++) {
      fputc(palette[i].r>>2,outfile);
      fputc(palette[i].g>>2,outfile);
      fputc(palette[i].b>>2,outfile);
    }
  } else if(paltype) {
    for(i=0;i<16;i++) {
      fputc(palette[i].r,outfile);
      fputc(palette[i].g,outfile);
      fputc(palette[i].b,outfile);
    }
  }
  // Font
  if(fontdata) fwrite(fontdata,font512?512:256,fontheight,outfile);
  // Image Data
  if(option['c']) {
    for(y=0;y<nrows;y++) {
      for(at=y*ncolumns,x=0;x<ncolumns-1;) {
        switch((playfield[at+x+1].ch==playfield[at+x].ch?1:0)+(playfield[at+x+1].co==playfield[at+x].co?2:0)) {
          case 0:
            for(z=1;z+x<ncolumns && z<64;z++) {
              if(x+z+1<ncolumns && playfield[at+x+z].ch==playfield[at+x+z+1].ch && playfield[at+x+z].co==playfield[at+x+z+1].co) break;
              if(x+z+2<ncolumns) {
                if(playfield[at+x+z].ch==playfield[at+x+z+1].ch && playfield[at+x+z].ch==playfield[at+x+z+2].ch) break;
                if(playfield[at+x+z].co==playfield[at+x+z+1].co && playfield[at+x+z].co==playfield[at+x+z+2].co) break;
              }
            }
            fputc(z-1,outfile);
            fwrite(playfield+at+x,2L*z,1,outfile);
            x+=z;
            break;
          case 1:
            for(z=1;z+x<ncolumns && z<64 && playfield[at+x+z].ch==playfield[at+x].ch;z++) {
              if(x+z+2<ncolumns && playfield[at+x+z].co==playfield[at+x+z+1].co && playfield[at+x+z].co==playfield[at+x+z+2].co) break;
            }
            fputc(z+0x3F,outfile);
            fputc(playfield[at+x].ch,outfile);
            for(w=0;w<z;w++) fputc(playfield[at+x+w].co,outfile);
            x+=z;
            break;
          case 2:
            for(z=1;z+x<ncolumns && z<64 && playfield[at+x+z].co==playfield[at+x].co;z++) {
              if(x+z+2<ncolumns && playfield[at+x+z].ch==playfield[at+x+z+1].ch && playfield[at+x+z].ch==playfield[at+x+z+2].ch) break;
            }
            fputc(z+0x7F,outfile);
            fputc(playfield[at+x].co,outfile);
            for(w=0;w<z;w++) fputc(playfield[at+x+w].ch,outfile);
            x+=z;
            break;
          case 3:
            for(z=1;z+x<ncolumns && z<64 && playfield[at+x+z].ch==playfield[at+x].ch && playfield[at+x+z].co==playfield[at+x].co;z++);
            fputc(z+0xBF,outfile);
            fputc(playfield[at+x].ch,outfile);
            fputc(playfield[at+x].co,outfile);
            x+=z;
            break;
        }
      }
      if(x==ncolumns-1) {
        fputc(0,outfile);
        fputc(playfield[at+x].ch,outfile);
        fputc(playfield[at+x].co,outfile);
      }
    }
  } else if(nrows && ncolumns) {
    fwrite(playfield,2L*nrows*ncolumns,1,outfile);
  }
}

// *** End of filters

static const Filters filters[]={
  // File controls
  {"-f",in_f},
  {"-p",in_p},
  {"+f",out_f},
  {"+p",out_p},
  // Input formats
  {"-ansi",in_ansi},
  {"-artworx",in_artworx},
  {"-bin",in_bin},
  {"-chr",in_chr},
  {"-ega",in_ega},
  {"-mzm",in_mzm},
  {"-vga",in_vga},
  {"-xbin",in_xbin},
  {"-zzt",in_zzt},
  // Modifiers
  {"cancel",do_cancel},
  {"mode",do_mode},
  {"pcpal",do_pcpal},
  {"showfont",do_showfont},
  // Output formats
  {"+artworx",out_artworx},
  {"+bin",out_bin},
  {"+chr",out_chr},
  {"+ega",out_ega},
  {"+farbfeld",out_farbfeld},
  {"+info",out_info},
  {"+mzm",out_mzm},
  {"+vga",out_vga},
  {"+xbin",out_xbin},
};

int main(int argc,char**argv) {
  int i,j;
  const char*p;
  infile=stdin; outfile=stdout;
  for(i=1;i<argc;i++) {
    p=strchrnul(argv[i],'=');
    for(j=0;j<sizeof(filters)/sizeof(*filters) && (strncmp(filters[j].name,argv[i],p-argv[i]) || filters[j].name[p-argv[i]]);j++);
    if(j==sizeof(filters)/sizeof(*filters)) errx(Err_Argument,"Improper filters: %s",argv[i]);
    filters[j].call(*p?p+1:(char*)0);
  }
  return 0;
}
