#if 0
gcc $CFLAGS -c -Wno-unused-result -fwrapv audio.c `sdl-config --cflags`
exit
#endif

#include "common.h"
#include "resample.h"
#include <math.h>

#define DRUM_NOTE 0x7F00
#define WAVE_NOTE 0x7EFF
#define NOTE_MASK 0x1FF
#define OVERTONE 128
#define UNDERTONE 320

#define MAXQUEUE_BITS (9)
#define MAXQUEUE (1<<MAXQUEUE_BITS)
#define MAXQUEUE_MASK (MAXQUEUE-1)

typedef struct {
  Sint16 note;
  Uint16 len;
} SoundQueue;

typedef struct {
  Sint16*data;
  Uint32 len;
  char name[12];
} WaveSound;

static SDL_AudioSpec spec;

static Uint16 volume=12288;
static Uint8 muted=255;
static Uint32 whole_note;

static Sint32 cfreq=-1;
static Uint32 cpos=0;
static Uint32 cmax=0;
static Uint16 priority=0;
static SoundQueue queue[MAXQUEUE];
static Uint16 qfirst=0;
static Uint16 qlast=0;

static WaveSound*wavesound;
static Uint16 nwavesound;

static float note_table[NOTE_MASK+1];
static float drum_table[256];

static const Uint16 drum_const_table[256]={
  [0x00]= /* ` */ 3200,800,0,
  [0x10]= /* unused */ 0,
  [0x20]= /* unused */ 0,
  [0x30]= /* * */ 500,2556,1929,3776,3386,4517,1385,1103,4895,3396,874,1616,5124,606,
  [0x40]= /* unused */ 0,
  [0x50]= /* \ */ 2400,2300,2200,2100,2000,1900,1800,1700,1600,1500,1400,1300,1200,1100,
  [0x60]= /* ~ */ 800,1000,1234,1567,1000,800,200,250,200,600,800,1000,1200,1500,
  [0x70]= /* ^ */ 4800,4780,8000,1600,4800,4780,8000,1600,4800,4800,8000,1600,4800,4800,8001,
  [0x80]= /* / */ 1100,1200,1300,1400,1500,1600,1700,1800,1900,2000,2100,2200,2300,2400,
  [0x90]= /* ! */ 1600,1514,1600,821,1600,1715,1600,911,1600,1968,1600,1490,1600,1722,1600,
  [0xA0]= /* : */ 2200,1760,1760,1320,2640,880,2200,1760,1760,1320,2640,880,2200,1760,
  [0xB0]= /* ; */ 688,676,664,652,640,628,616,604,592,580,568,556,544,532,
  [0xC0]= /* $ */ 1207,1224,1163,1127,1159,1236,1269,1314,1127,1224,1320,1332,1257,1327,
  [0xD0]= /* % */ 378,331,316,230,224,384,480,320,358,412,376,621,554,426,
  [0xE0]= /* & */ 100,200,300,400,800,700,600,500,450,1210,
  [0xF0]= /* ? */ 1500,1450,1400,1350,1300,1250,1300,1400,1600,1801,2111,2511,2811,3111,
};

static char init_resampler(Resample*resam,const char*text) {
  Uint16 t,f;
  Uint8 g=0;
  double p=1.0;
  if(!text || !*text || (*text=='0' && !text[1])) return 0;
  t=strtol(text,(char**)&text,10);
  f=strtol(text,(char**)&text,10);
  if(!t || !f) errx(1,"Syntax error in resampler configuration");
  while(*text==' ') ++text;
  if(*text=='L') {
    p=strtod(text+1,(char**)&text);
    g|=RESAMPLE_LOWPASS;
  }
  while(*text==' ') ++text;
  if(*text=='B') g|=RESAMPLE_BLACKMAN_HARRIS; else if(*text!='H') errx(1,"Syntax error in resampler configuration");
  ++text; while(*text==' ') ++text;
  if(*text=='I') g|=RESAMPLE_SUBSAMPLE_INTERPOLATE,++text;
  while(*text==' ') ++text;
  if(*text) errx(1,"Syntax error in resampler configuration");
  if(resample_init(resam,t,f,p,g)) errx(1,"Error in resampler configuration");
  return 1;
}

static void audiocb(void*userdata,Uint8*stream,int len) {
  static float prf=0.0;
  static float pha=0.0;
  float fil=config.audio_filter;
  float vol=volume;
  float x,y;
  Sint16*buf=(Sint16*)stream;
  int pos=0;
  len>>=1;
  while(pos<len) {
    if(cfreq<0) {
      while(pos<len) buf[pos++]=vol*(prf*=fil);
      return;
    } else if(cfreq && cfreq<=NOTE_MASK) {
      while(pos<len && cpos<cmax) {
        pha+=note_table[cfreq];
        if(pha>=1.0) pha-=1.0;
        prf=(1.0-fil)*(pha<0.5?1.0:-1.0)+fil*prf;
        if(!cpos++) prf*=0.5;
        buf[pos++]=vol*prf;
      }
      if(cpos>=cmax) cfreq=-1;
    } else if(cfreq>=DRUM_NOTE) {
      x=sqrt(fil);
      while(pos<len && cpos<cmax) {
        if((cpos*577ULL)/whole_note>=16 || !drum_const_table[(cfreq&15)*16+(cpos*577ULL)/whole_note]) {
          cfreq=0;
          break;
        }
        pha+=drum_table[(cfreq&15)*16+(cpos*577ULL)/whole_note];
        if(pha>=1.0) pha-=1.0;
        prf=(1.0-x)*(pha<0.5?-1.0:1.0)+x*prf;
        if(pha<0.1) prf*=-1.0;
        if(!cpos++) prf*=0.5;
        buf[pos++]=vol*prf;
      }
      if(cpos>=cmax) cfreq=-1;
    } else if(cfreq==WAVE_NOTE) {
      while(pos<len && cpos<wavesound[cmax].len) buf[pos++]=(vol*wavesound[cmax].data[cpos++])/32767.0;
      if(cpos>=wavesound[cmax].len) {
        cfreq=-1;
        prf=wavesound[cmax].data[wavesound[cmax].len-1]/32767.0;
      }
    } else {
      while(pos<len && cpos<cmax) cpos++,buf[pos++]=vol*(prf*=fil);
      if(cpos>=cmax) cfreq=-1;
    }
    if(cfreq==-1) {
      if(qfirst==qlast) {
        priority=0;
      } else {
        cfreq=queue[qfirst].note;
        cpos=0;
        cmax=queue[qfirst].len;
        qfirst=(qfirst+1)&MAXQUEUE_MASK;
        pha=0.0;
      }
    }
  }
}

static int convertaudio(WaveSound*wav,Uint16 rate,Uint8 ratemode,Uint16 form,Resample*resam) {
  if(config.test_mode) printf("convertaudio(%p,%d,%d,%d,%p)\n",wav,rate,ratemode,form,resam);
  if(resam && (rate!=spec.freq || !ratemode)) {
    Sint16 buf[512];
    char*out=0;
    size_t outsize=0;
    FILE*f=open_memstream(&out,&outsize);
    double r=((double)spec.freq)/(ratemode?1.0*rate:256000000.0/(65536-rate));
    if(!f) err(1,"Allocation failed");
    resample_reset(resam);
    resam->pin=0;
#if AUDIO_S16SYS!=AUDIO_S16LSB
    if(form==AUDIO_S16LSB && AUDIO_S16SYS!=AUDIO_S16LSB) {
      Uint32 i;
      for(i=0;i<wav->len;i++) wav->data[i]=((wav->data[i]&0xFF)<<8)|((wav->data[i]>>8)&0xFF);
      form=AUDIO_S16SYS;
    }
#endif
    if(form==AUDIO_U8) {
      // Convert audio from U8 to S16
      Uint32 i=wav->len;
      Uint8 __attribute__((__may_alias__))*d=(void*)wav->data;
      while(i--) wav->data[i]=d[i]*0x101-0x8000;
      form=AUDIO_S16SYS;
    }
    if(form==AUDIO_S16SYS) {
      for(;;) {
        resam->pout=0;
        resample_process_int16(resam,wav->data+resam->pin,wav->len-resam->pin,buf,512,r);
        if(resam->pin==wav->len && !resam->pout) break;
        fwrite(buf,sizeof(Sint16),resam->pout,f);
      }
    } else {
      fclose(f);
      free(out);
      return 1;
    }
    fclose(f);
    if(!out) err(1,"Allocation failed");
    free(wav->data);
    wav->data=(void*)out;
    wav->len=outsize/sizeof(Sint16);
    return 0;
  } else {
    SDL_AudioCVT cvt={};
    Uint32 r1=rate;
    Uint32 r2=spec.freq;
    if(!ratemode) {
      r1=2560000000L/(65536-rate);
      r2*=10;
    }
    if(SDL_BuildAudioCVT(&cvt,form,1,r1,AUDIO_S16SYS,1,r2)<0) return 1;
    wav->data=realloc(wav->data,2L*wav->len*(Uint32)cvt.len_mult);
    if(!wav->data) err(1,"Allocation failed");
    cvt.buf=(void*)wav->data;
    cvt.len=2L*wav->len;
    wav->len=(cvt.len*(Uint32)cvt.len_mult)>>(form&8?2:1);
    return SDL_ConvertAudio(&cvt);
  }
}

static inline Uint8 adpcm4bits(Uint8 v,Uint8 p,Uint8*m) {
  Uint8 d=*m*(v&7)+(*m>>1);
  Sint32 r=p+d*(v&8?1:-1);
  if(*m>1 && !(v&7)) *m>>=1;
  if(*m<8 && (v&7)>4) *m<<=1;
  return p=(r<0?0:r>255?255:r);
}

static inline Uint8 adpcm3bits(Uint8 v,Uint8 p,Uint8*m) {
  // This is not tested yet.
  static const Sint8 u[40]={
    0,1,2,3,-0,-1,-2,-3,
    1,3,5,7,-1,-3,-5,-7,
    2,6,10,14,-2,-6,-10,-14,
    4,12,20,28,-4,-12,-20,-28,
    5,15,25,35,-5,-15,-25,-35,
  };
  Sint32 r=p+u[v+*m];
  if((v&4)==3 && *m<32) *m+=8; else if((v&4)==0 && *m>4) *m-=8;
  return p=(r<0?0:r>255?255:r);
}

static inline Uint8 adpcm2bits(Uint8 v,Uint8 p,Uint8*m) {
  static const Sint8 u[24]={0,1,-0,-1,1,3,-1,-3,2,6,-2,-6,4,12,-4,-12,8,24,-8,-24,16,48,-16,-48};
  Sint32 r=p+u[v+*m];
  *m+=(v&1?(*m<20?4:0):(*m<4?0:-4));
  return p=(r<0?0:r>255?255:r);
}

static const Sint16 alaw[128]={
  5504, 5248, 6016, 5760, 4480, 4224, 4992, 4736,
  7552, 7296, 8064, 7808, 6528, 6272, 7040, 6784,
  2752, 2624, 3008, 2880, 2240, 2112, 2496, 2368,
  3776, 3648, 4032, 3904, 3264, 3136, 3520, 3392,
  22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
  30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
  11008, 10496, 12032, 11520, 8960, 8448, 9984, 9472,
  15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
  344, 328, 376, 360, 280, 264, 312, 296,
  472, 456, 504, 488, 408, 392, 440, 424,
  88, 72, 120, 104, 24, 8, 56, 40,
  216, 200, 248, 232, 152, 136, 184, 168,
  1376, 1312, 1504, 1440, 1120, 1056, 1248, 1184,
  1888, 1824, 2016, 1952, 1632, 1568, 1760, 1696,
  688, 656, 752, 720, 560, 528, 624, 592,
  944, 912, 1008, 976, 816, 784, 880, 848,
};

static const Sint16 mulaw[128]={
   32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
   23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
   15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
   11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
    7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
    5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
    3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
    2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
    1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
    1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
     876,   844,   812,   780,   748,   716,   684,   652,
     620,   588,   556,   524,   492,   460,   428,   396,
     372,   356,   340,   324,   308,   292,   276,   260,
     244,   228,   212,   196,   180,   164,   148,   132,
     120,   112,   104,    96,    88,    80,    72,    64,
      56,    48,    40,    32,    24,    16,     8,     0,
};

static int convertwave(FILE*f,Uint32 size,WaveSound*wav,Uint16 rate,Uint8 flag1,Uint8 flag2,Resample*resam) {
  // For the ADPCM formats, also see the files:
  //   https://github.com/TerrySoba/VocTool/raw/master/src/decode_creative_adpcm.h
  //   https://github.com/jacksonh/sox/raw/master/src/adpcms.c
  // You may also see the Sound Blaster and DOSBOX-X code.
  Uint8*d;
  Uint32 a;
  Uint8 c,m,p;
  if(size&0xFFC00000) return 1;
  switch(flag2&15) {
    case 0: // Unsigned 8-bits
      wav->data=(void*)(d=calloc(wav->len=size,2));
      if(!wav->data) err(1,"Memory error in wave sound conversion");
      fread(wav->data,1,wav->len,f);
      if(convertaudio(wav,rate,flag1&1,AUDIO_U8,resam)) return 1;
      break;
    case 1: // ADPCM 4-bits
      wav->data=(void*)(d=calloc((wav->len=size*2)+1,2));
      if(!wav->data) err(1,"Memory error in wave sound conversion");
      m=1; p=128;
      for(a=0;a<wav->len;) {
        c=fgetc(f);
        d[a++]=p=adpcm4bits(c>>4,p,&m);
        d[a++]=p=adpcm4bits(c&15,p,&m);
      }
      if(convertaudio(wav,rate,flag1&1,AUDIO_U8,resam)) return 1;
      break;
    case 2: // ADPCM 3-bits
      wav->data=(void*)(d=calloc((wav->len=size*3)+1,2));
      if(!wav->data) err(1,"Memory error in wave sound conversion");
      m=0; p=128;
      for(a=0;a<wav->len;) {
        c=fgetc(f);
        d[a++]=p=adpcm3bits((c>>5)&7,p,&m);
        d[a++]=p=adpcm3bits((c>>2)&7,p,&m);
        d[a++]=p=adpcm3bits((c<<1)&7,p,&m);
      }
      if(convertaudio(wav,rate,flag1&1,AUDIO_U8,resam)) return 1;
      break;
    case 3: // ADPCM 2-bits
      wav->data=(void*)(d=calloc((wav->len=size*4)+1,2));
      if(!wav->data) err(1,"Memory error in wave sound conversion");
      m=0; p=128;
      for(a=0;a<wav->len;) {
        c=fgetc(f);
        d[a++]=p=adpcm2bits((c>>6)&3,p,&m);
        d[a++]=p=adpcm2bits((c>>4)&3,p,&m);
        d[a++]=p=adpcm2bits((c>>2)&3,p,&m);
        d[a++]=p=adpcm2bits((c>>0)&3,p,&m);
      }
      if(convertaudio(wav,rate,flag1&1,AUDIO_U8,resam)) return 1;
      break;
    case 4: // Signed 16-bits
      wav->data=calloc(wav->len=size>>1,2);
      if(!wav->data) err(1,"Memory error in wave sound conversion");
      fread(wav->data,2,wav->len,f);
      if(rate!=spec.freq || !(flag1&1) || AUDIO_S16SYS!=AUDIO_S16LSB) if(convertaudio(wav,rate,flag1&1,AUDIO_S16LSB,resam)) return 1;
      break;
    case 6: // A-law
      wav->data=calloc(wav->len=size,2);
      if(!wav->data) err(1,"Memory error in wave sound conversion");
      for(a=0;a<wav->len;a++) p=fgetc(f),wav->data[a]=p&128?-alaw[p&127]:alaw[p&127];
      if(rate!=spec.freq || !(flag1&1)) if(convertaudio(wav,rate,flag1&1,AUDIO_S16SYS,resam)) return 1;
      break;
    case 7: // mu-law
      wav->data=calloc(wav->len=size,2);
      if(!wav->data) err(1,"Memory error in wave sound conversion");
      for(a=0;a<wav->len;a++) p=fgetc(f),wav->data[a]=p&128?mulaw[p&127]:(-mulaw[p&127]-(p==127));
      if(rate!=spec.freq || !(flag1&1)) if(convertaudio(wav,rate,flag1&1,AUDIO_S16SYS,resam)) return 1;
      break;
    default: fprintf(stderr,"Unknown audio codec %d\n",flag2&15); return 1;
  }
  if(flag1&2) {
    // Apply filter
    float t=config.audio_filter;
    float p=0.0;
    for(a=0;a<wav->len;a++) wav->data[a]=p=(1.0-t)*wav->data[a]+t*p;
  }
  return 0;
}

static void loadwaves(void) {
  Resample resam;
  FILE*f;
  const char**list=0;
  int count=0;
  int i,j,k,r;
  char useresam=0;
  list_lumps("*.SND",&list,&count);
  if(count) {
    if(config.wave_resample) {
      useresam=init_resampler(&resam,config.wave_resample);
      free(config.wave_resample);
      config.wave_resample=0;
    }
    if(count>0xFFFE) errx(1,"Too many .SND lumps");
    wavesound=calloc(nwavesound=count,sizeof(WaveSound));
    if(!wavesound) err(1,"Memory error in wave sound conversion");
    for(i=0;i<count;i++) {
      strncpy(wavesound[i].name,list[i],8);
      for(j=0;j<9;j++) if(wavesound[i].name[j]=='.') wavesound[i].name[j]=0;
      if(f=open_lump(list[i],"r")) {
        if(lump_size<6 || fgetc(f)!=4) {
          warnx("Sound \"%s\" has unrecognized header size",list[i]);
          goto end;
        }
        j=fgetc(f); k=fgetc(f);
        if((j&~3) || (k&~15)) {
          warnx("Sound \"%s\" has unimplemented flags",list[i]);
          goto end;
        }
        r=fgetc(f); r|=fgetc(f)<<8;
        if(convertwave(f,lump_size-5,wavesound+i,r,j,k,useresam?&resam:0) || !wavesound[i].data || !wavesound[i].len) {
          warnx("Sound \"%s\" cannot be converted",list[i]);
          free(wavesound[i].data);
          wavesound[i].data=0;
          wavesound[i].len=0;
        }
        end: fclose(f);
      }
    }
    if(useresam) resample_uninit(&resam);
  }
  free(list);
}

void audio_init(void) {
  int i;
  if(!config.audio_buffer || !config.audio_rate || (SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO)) return;
  // Initialize tables
  whole_note=config.audio_rate*1.81;
  for(i=0;i<128;i++) note_table[i]=32.0*pow(2.0,i/12.0)/(float)config.audio_rate;
  for(i=OVERTONE;i<UNDERTONE;i++) note_table[i]=(32.0*(i+2-OVERTONE))/(float)config.audio_rate;
  for(i=UNDERTONE;i<=NOTE_MASK;i++) note_table[i]=(2048.0/(i+2-UNDERTONE))/(float)config.audio_rate;
  for(i=0;i<256;i++) drum_table[i]=drum_const_table[i]/(float)config.audio_rate;
  // Initialize SDL audio
  spec.freq=config.audio_rate;
  spec.format=AUDIO_S16SYS;
  spec.channels=1;
  spec.samples=config.audio_buffer;
  spec.size=2*spec.samples;
  spec.callback=audiocb;
  if(SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    warnx("Cannot initialize SDL audio subsystem");
    return;
  }
  if(SDL_OpenAudio(&spec,0)<0) {
    warnx("Cannot initialize audio");
    return;
  }
  muted=0;
  if(volume=config.audio_volume) SDL_PauseAudio(0);
  if(config.wave_sound && !editor) loadwaves();
}

void audio_set_volume(Uint16 vol,Uint8 mut) {
  if(muted==255) return;
  volume=vol;
  SDL_PauseAudio(muted=mut);
  if(mut) {
    SDL_LockAudio();
    cfreq=-1;
    priority=qfirst=qlast=0;
    SDL_UnlockAudio();
  }
}

Sint32 audio_get_volume(void) {
  return volume|(((Sint32)muted)<<16);
}

static Uint32 find_wave(const char*m) {
  char a[9]={};
  Uint32 i;
  for(i=0;i<8 && *m && *m!=41;i++) {
    a[i]=*m++;
    if(a[i]>='a' && a[i]<='z') a[i]+='A'-'a';
  }
  for(i=0;i<nwavesound && (wavesound[i].len<1 || strcmp(a,wavesound[i].name));i++);
  return i;
}

void audio_set_sfx(const char*m) {
  Sint8 scale[8]={9,11,0,2,4,5,7};
  Uint8 c;
  Uint32 n,d;
  Uint16 pr;
  Uint16 oct=3*12;
  Uint32 dur=whole_note/32;
  float f;
  if(muted) return;
  SDL_LockAudio();
  if(*m=='@') {
    m++;
    n=0;
    while(*m>='0' && *m<='9') n=10*n+*m++-'0';
    if(n<priority || (n==priority && (n&1))) goto end;
    priority=n;
    qfirst=qlast=0;
  } else {
    if(!priority) priority=64;
  }
  while(*m && *m!='\n') {
    c=*m++;
    if(c>='a' && c<='z') c+='A'-'a';
    switch(c) {
      // Set note lengths; single characters
      case 'W': dur=whole_note/1; break;
      case 'H': dur=whole_note/2; break;
      case 'Q': dur=whole_note/4; break;
      case 'I': dur=whole_note/8; break;
      case 'S': dur=whole_note/16; break;
      case 'T': dur=whole_note/32; break;
      case 'Z': dur=whole_note/64; break;
      // Set note length custom
      case 'L':
        if(*m=='=') {
          m++;
          dur=0;
          while(*m>='0' && *m<='9') dur=10*dur+*m++-'0';
          dur*=whole_note/64;
        } else {
          n=0;
          while(*m>='0' && *m<='9') n=10*n+*m++-'0';
          if(n) dur=whole_note/n;
        }
        break;
      case '.': dur+=dur>>1; break;
      // Set octave
      case 'O': if(*m>='0' && *m<='9') oct=12*(*m-'0'); m++; break;
      case '<': if(oct) oct-=12; break;
      case '>': if(oct<120) oct+=12; break;
      // Normal notes
      case 'A' ... 'G':
        n=oct+scale[c-'A'];
        while(*m=='+' || *m=='#' || *m=='-' || *m=='\'' || *m==',') {
          c=*m++;
          if(c=='+' || c=='#') n++; else if(c=='-') n--; else if(c==',') n-=12; else n+=12;
        }
        n&=NOTE_MASK;
      noted:
        if(*m=='=') {
          m++;
          d=0;
          while(*m>='0' && *m<='9') d=10*d+*m++-'0';
          d*=whole_note/64;
        } else if(*m>='0' && *m<='9') {
          d=0;
          while(*m>='0' && *m<='9') d=10*n+*m++-'0';
          d=(d?whole_note/d:dur);
          if(*m=='.') m++,d+=d>>1;
        } else {
          d=dur;
        }
      noted2:
        if(((qlast+1)&MAXQUEUE_MASK)==qfirst) goto end;
        queue[qlast].note=n;
        queue[qlast].len=d;
        qlast=(qlast+1)&MAXQUEUE_MASK;
        break;
      // Rest
      case 'R': case 'X': n=0; goto noted;
      // Overtone/undertone
      case 'K': case 'U':
        n=0;
        while(*m>='0' && *m<='9') n=10*n+*m++-'0';
        if(n>1) n+=(c=='K'?OVERTONE:UNDERTONE)-2; else n=0;
        if(*m==',') m++;
        goto noted;
      // Percussions
      case '!': case '$': case '%': case '^': case '&': case '*': case '/':
      case '?': case '\\': case '`': case '~': case ':': case ';':
        c%=24; n=(c&15)+(c/16)+DRUM_NOTE;
        goto noted;
      // Wave sounds
      case 'Y': if(nwavesound) goto end; break;
      case '(':
        n=WAVE_NOTE;
        d=nwavesound?find_wave(m):0;
        while(*m && *m!='\n' && *m!=')') m++;
        if(d<nwavesound) goto noted2;
        break;
    }
  }
  end:
  if(cfreq==-1 && qfirst!=qlast) {
    cfreq=queue[qfirst].note;
    cpos=0;
    cmax=queue[qfirst].len;
    qfirst=(qfirst+1)&MAXQUEUE_MASK;
  }
  SDL_UnlockAudio();
}
