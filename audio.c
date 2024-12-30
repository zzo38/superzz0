#if 0
gcc -s -O2 -c -Wno-unused-result -fwrapv audio.c `sdl-config --cflags`
exit
#endif

#include "common.h"
#include <math.h>

#define DRUM_NOTE 0x7F00
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

static float note_table[NOTE_MASK+1];

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
    } else if(cfreq) {
      //TODO
      return;
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

void audio_init(void) {
  int i;
  if(!config.audio_buffer || !config.audio_rate || (SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO)) return;
  // Initialize tables
  whole_note=config.audio_rate*1.81;
  for(i=0;i<128;i++) note_table[i]=32.0*pow(2.0,i/12.0)/(float)config.audio_rate;
  for(i=OVERTONE;i<UNDERTONE;i++) note_table[i]=(32.0*(i+2-OVERTONE))/(float)config.audio_rate;
  for(i=UNDERTONE;i<=NOTE_MASK;i++) note_table[i]=(2048.0/(i+2-UNDERTONE))/(float)config.audio_rate;
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
        //TODO
        goto noted;
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

