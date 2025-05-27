#if 0
gcc -s -O2 -o ./cbgconv cbgconv.c -Wno-unused-result
exit
#endif

// Convert character-based graphics
// (SAUCE is not currently implemented, but may be done in future)

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

static void in_bin(const char*arg) {
  arg=read_options(arg);
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

// *** Modifiers

static void do_mode(const char*arg) {
  ReqArg;
  videomode=Video_Set;
  while(*arg) switch(*arg++) {
    case '9': videomode|=Video_NineDots; break;
    case 'b': videomode|=Video_BrightBG; break;
    default: errx(Err_Argument,"Improper video mode");
  }
}

static void do_pcpal(const char*arg) {
  static const Palette p[16]={
    {0x00,0x00,0x00},
    {0x00,0x00,0xAA},
    {0x00,0xAA,0x00},
    {0x00,0xAA,0xAA},
    {0xAA,0x00,0x00},
    {0xAA,0x00,0xAA},
    {0xAA,0x55,0x00},
    {0xAA,0xAA,0xAA},
    {0x55,0x55,0x55},
    {0x55,0x55,0xFF},
    {0x55,0xFF,0x55},
    {0x55,0xFF,0xFF},
    {0xFF,0x55,0x55},
    {0xFF,0x55,0xFF},
    {0xFF,0xFF,0x55},
    {0xFF,0xFF,0xFF},
  };
  paltype=Pal_VGA;
  memcpy(palette,p,sizeof(p));
}

// *** Output formats

static void out_bin(const char*arg) {
  if(!playfield) errx(Err_Data,"Playfield is not available");
  fwrite(playfield,2L*nrows*ncolumns,1,outfile);
}

static void out_chr(const char*arg) {
  if(!fontdata) errx(Err_Data,"Font is not available");
  fwrite(fontdata,fontheight,font512?512:256,outfile);
}

static void out_farbfeld(const char*arg) {
  static struct {
    uint8_t data[8];
  } colors[16];
  uint32_t x,y;
  uint8_t c,f,i,xx,yy;
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
      if(!(videomode&Video_BrightBG)) c&=0x7F;
      for(xx=0;xx<8;xx++) fwrite(colors[(c>>((128>>xx)&f?0:4))&15].data,1,8,outfile);
      if(videomode&Video_NineDots) fwrite(colors[(c>>(f&1?(i>191&&i<224?0:4):4))&15].data,1,8,outfile);
    }
  }
}

static void out_info(const char*arg) {
  printf("fontheight=%d\n",fontheight);
  printf("nrows=%d\n",nrows);
  printf("ncolumns=%d\n",ncolumns);
  printf("font512=%d\n",font512);
}

static void out_mzm(const char*arg) {
  if(!playfield) errx(Err_Data,"Playfield is not available");
  fwrite("MZM2",1,3,outfile);
  fputc(ncolumns,outfile); fputc(ncolumns>>8,outfile);
  fputc(nrows,outfile); fputc(nrows>>8,outfile);
  fwrite("\0\0\0\0\0\x01\0\0",1,8,outfile);
  fwrite(playfield,2L*nrows*ncolumns,1,outfile);
}

// *** End of filters

static const Filters filters[]={
  // File controls
  {"-f",in_f},
  {"-p",in_p},
  {"+f",out_f},
  {"+p",out_p},
  // Input formats
  {"-bin",in_bin},
  {"-chr",in_chr},
  {"-ega",in_ega},
  {"-mzm",in_mzm},
  {"-vga",in_vga},
  {"-xbin",in_xbin},
  // Modifiers
  {"mode",do_mode},
  {"pcpal",do_pcpal},
  // Output formats
  {"+bin",out_bin},
  {"+chr",out_chr},
  {"+farbfeld",out_farbfeld},
  {"+info",out_info},
  {"+mzm",out_mzm},
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
