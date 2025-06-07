#if 0
gcc $CFLAGS -c -Wno-unused-result -fwrapv audio.c `sdl-config --cflags`
exit
#endif

#include "common.h"
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

static int convertwave(FILE*f,Uint32 size,WaveSound*wav,Uint16 rate,Uint8 flag1,Uint8 flag2) {
  Uint32 a;
  switch(flag2&15) {
    case 0: // Unsigned 8-bits
      //TODO: incomplete
      break;
    case 4: // Signed 16-bits
      if(rate==spec.freq && !(flag1&1) && AUDIO_S16SYS==AUDIO_S16LSB) {
        wav->data=calloc(wav->len=size>>1,2);
        if(!wav->data) err(1,"Allocation failed");
        fread(wav->data,2,wav->len,f);
      } else {
        //TODO: incomplete
      }
      break;
    default: fprintf(stderr,"Unknown codec %d\n",flag2&15); return 1;
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
  FILE*f;
  const char**list=0;
  int count=0;
  int i,j,k,r;
  list_lumps("*.SND",&list,&count);
  if(count) {
    if(count>0xFFFE) errx(1,"Too many .SND lumps");
    wavesound=calloc(nwavesound=count,sizeof(WaveSound));
    if(!wavesound) err(1,"Allocation failed");
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
        if(convertwave(f,lump_size-5,wavesound+i,r,j,k) || !wavesound[i].data || !wavesound[i].len) {
          warnx("Sound \"%s\" cannot be converted",list[i]);
          free(wavesound[i].data);
          wavesound[i].data=0;
          wavesound[i].len=0;
        }
        end: fclose(f);
      }
    }
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
  for(i=0;i<nwavesound && strcmp(a,wavesound[i].name) && wavesound[i].len;i++);
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
