#if 0
gcc $CFLAGS -c -Wno-unused-result -fwrapv audio.c `sdl-config --cflags`
exit
#endif

#include "common.h"
#include "resample.h"
#include <math.h>
#include <dlfcn.h>
#include "musemu/musemu.h"
#define TAU (2.0*M_PI)

typedef struct {
  MUSEMUinf id;
  const MUSEMU*em;
} Emulator;

static Emulator*emulator;
static Uint8 nemulator;

#define DRUM_NOTE 0x7F00
#define LOOP_NOTE 0x7EFE
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

#define INST_WAVE 1
#define INST_DELAYLINE 2
#define INST_DELAYLINE_COPY 3

#define CHAN_USE 0x01
#define CHAN_SOUND 0x02
#define CHAN_LOOP 0x04
#define CHAN_EMULATE 0x08
#define CHAN_ENV1 0x40
#define CHAN_ENV2 0x80

#define CHAN_FLAG_MASK (CHAN_USE|CHAN_EMULATE)

#define MaxFeedbackItems 8

typedef struct {
  Resample resam;
  union {
    Sint16*d16;
    Uint8*d8;
  };
  float*line;
  double rate;
  float fb[MaxFeedbackItems];
  float amp,ampdecay,filter,prev;
  float lfox,lfoy,lfoe,lfod;
  Uint32 dlen,dpos,len,ls,duse;
  Uint16 fbat[MaxFeedbackItems];
  Uint8 is8,nfb,lfok,lfos,option;
} DelayLine;

#define DLOP_CONSTRATE 0x80
#define DLOP_DUSEFREQ 0x40
#define DLOP_AUTOFEEDBACK 0x20

#define LFO_NONE 0
#define LFO_OUT_AMPLITUDE 1
#define LFO_FEEDBACK 2
#define LFO_OUT_FREQUENCY 3
#define LFO_IN_FREQUENCY 4
#define LFO_FILTER 5

typedef struct {
  Uint8 t;
  union {
    struct {
      union {
        Sint16*d16;
        Uint8*d8;
      };
      Uint32 len,ls,le;
      Uint8 is8;
    } wave;
    DelayLine*dline;
  };
  double rate;
} Instrument;

typedef struct {
  Resample resam;
  void*state;
  double freq;
  float amp;
  Uint16 env1,env2;
  Uint8 instrument,flag,emu;
  Uint8 pos1,pos2;
} Channel;

typedef struct {
  double x,y;
  Uint16 s[8];
  Uint16 a,b,l,m,p,r;
  Uint8 c,n,t,w;
} Thread;

typedef struct {
  Uint8*rom;
  Instrument*in;
  Channel*ch;
  Thread*th;
  double*rc;
  double z;
  Uint32 tcur,tmax;
  Uint16 sc[4];
  Uint16 g,size,tempo;
  Uint8 nin,nch,nth,nrc;
} Music;

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

static Music*music;
char music_name[9];
Uint16 music_song;
char music_on;

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

static inline void render_music_frame(float*buf,int len) {
  int i,m;
  Sint32 p,r;
  float f,g;
  double d;
  Uint8 k;
  Instrument*ins;
  Channel*cha;
  for(m=0;m<music->nch;m++) {
    if((music->ch[m].flag&CHAN_SOUND) && !(music->ch[m].flag&CHAN_EMULATE)) {
      cha=music->ch+m;
      if(i=cha->instrument) {
        ins=music->in+i-1;
        switch(ins->t) {
          case INST_WAVE:
            p=cha->resam.pin;
            r=(cha->flag&CHAN_LOOP)?ins->wave.le:ins->wave.len;
            cha->resam.pout=0;
            while(r-p>0 && cha->resam.pout<len) {
              if(ins->wave.is8) {
                resample_process_uint8_to_float_mix(&cha->resam,ins->wave.d8+p,r-p,buf+cha->resam.pout,len-cha->resam.pout,cha->amp,cha->freq);
              } else {
                resample_process_int16_to_float_mix(&cha->resam,ins->wave.d16+p,r-p,buf+cha->resam.pout,len-cha->resam.pout,cha->amp,cha->freq);
              }
              p=cha->resam.pin;
              if(p==ins->wave.len && (ins->wave.ls==ins->wave.len || !(cha->flag&CHAN_LOOP))) {
                cha->flag&=~(CHAN_SOUND|CHAN_LOOP);
                break;
              }
              if((cha->flag&CHAN_LOOP) && p==ins->wave.le) p=cha->resam.pin=ins->wave.ls;
            }
            break;
          case INST_DELAYLINE: case INST_DELAYLINE_COPY:
            r=ins->dline->len;
            k=ins->dline->lfok;
            for(ins->dline->resam.pout=0;ins->dline->resam.pout<len;) {
              if(k) {
                i=ins->dline->lfos;
                if(i<6) {
                  ins->dline->lfox-=ins->dline->lfoe*ins->dline->lfoy;
                  ins->dline->lfoy+=ins->dline->lfoe*ins->dline->lfox;
                }
                switch(i) {
                  case 0: g=ins->dline->lfox; break;
                  case 1: g=fabs(ins->dline->lfox); break;
                  case 2: g=ins->dline->lfox+1.0; break;
                  case 3: g=1.0-fabs(ins->dline->lfox); break;
                  case 4: g=fmax(0.0,ins->dline->lfox); break;
                  case 5: g=ins->dline->lfox*ins->dline->lfoy; break;
                  case 6: g=ins->dline->lfoy; ins->dline->lfoy*=ins->dline->lfoe; break;
                  case 7: g=ins->dline->lfox; ins->dline->lfox=fmod(g+ins->dline->lfoe,ins->dline->lfod); break;
                  case 8: g=ins->dline->lfoy; ins->dline->lfoy*=1.0-ins->dline->lfoe; break;
                  case 9: g=1.0-ins->dline->lfoy; ins->dline->lfoy*=1.0-ins->dline->lfoe; break;
                }
              }
              if(ins->dline->dpos>=ins->dline->dlen) {
                ins->dline->dpos=0;
                cha->resam.pout=0;
                d=cha->freq;
                if(k==LFO_IN_FREQUENCY) d=fmax(0.0,(1.0+g)*d);
                again1:
                if(ins->dline->is8) {
                  resample_process_uint8_to_float_mix(&cha->resam,ins->dline->d8+cha->resam.pin,r-cha->resam.pin,ins->dline->line,ins->dline->dlen-cha->resam.pout,ins->dline->amp,d);
                } else {
                  resample_process_int16_to_float_mix(&cha->resam,ins->dline->d16+cha->resam.pin,r-cha->resam.pin,ins->dline->line,ins->dline->dlen-cha->resam.pout,ins->dline->amp,d);
                }
                if(cha->resam.pout<ins->dline->dlen && cha->resam.pin==ins->dline->len && (cha->flag&CHAN_LOOP) && ins->dline->ls<r) {
                  cha->resam.pin=ins->dline->ls;
                  goto again1;
                }
                ins->dline->amp*=ins->dline->ampdecay;
              }
              if(!resample_full(&ins->dline->resam)) {
                f=ins->dline->line[ins->dline->dpos];
                ins->dline->line[ins->dline->dpos]=(k!=LFO_FEEDBACK?0.0:f*g);
                for(i=0;i<ins->dline->nfb;i++) ins->dline->line[(ins->dline->dpos+ins->dline->fbat[i])&(ins->dline->dlen-1)]+=f*ins->dline->fb[i];
                if(ins->dline->dpos<ins->dline->duse) {
                  if(k==LFO_OUT_AMPLITUDE) f*=g;
                  if(k==LFO_FILTER) ins->dline->filter=g;
                  f=ins->dline->prev=(1.0-ins->dline->filter)*f+ins->dline->filter*ins->dline->prev;
                  resample_push(&ins->dline->resam,f);
                  if(ins->dline->option&DLOP_AUTOFEEDBACK) ins->dline->line[ins->dline->dpos]+=f;
                }
                ins->dline->dpos++;
              }
              resample_process_to_float_mix(&ins->dline->resam,buf+ins->dline->resam.pout,len-ins->dline->resam.pout,cha->amp,k!=LFO_OUT_FREQUENCY?ins->dline->rate:fmax(0.0,ins->dline->rate*(g+1.0)));
            }
            break;
        }
      }
    } else if(music->ch[m].flag&CHAN_EMULATE) {
      cha=music->ch+m;
      emulator[cha->emu].em->render(cha->state,len,buf,cha->amp*32767.0,0,0);
    }
  }
}

static Uint16 get_special_i(const Channel*cha,Uint8 id,Uint8 th) {
  if((id&0x80) && !cha) return 0;
  if(id>=0xC0 && (!cha || !(th=cha->instrument))) return 0;
  switch(id) {
    case 0x00: return th;
    case 0x01: return music->tempo;
    case 0x02: return music->tcur;
    case 0x03: return memory[MEM_MUSIC_EXTRA];
    case 0x80: return cha->instrument;
    case 0x81: return cha->resam.pin;
    case 0x82: return cha->flag;
    case 0x90: return cha->env1;
    case 0x91: return cha->pos1;
    case 0x92: return cha->env2;
    case 0x93: return cha->pos2;
    case 0xC0: return (music->in[th-1].t==INST_DELAYLINE || music->in[th-1].t==INST_DELAYLINE_COPY)?music->in[th-1].dline->duse:0;
    default: return 0;
  }
}

static void put_special_i(Channel*cha,Uint8 id,Uint16 v) {
  if((id&0x80) && !cha) return;
  if(id>=0xC0 && (!cha || !cha->instrument)) return;
  switch(id) {
    case 0x01: if(v>music->tmax/2) v=music->tmax/2; music->tempo=v; break;
    case 0x02: music->tcur=v; break;
    case 0x7F: if(config.music_debug==255) printf("MUSIC DEBUG: $%04X\n",v); break;
    case 0x80: cha->instrument=(v<=music->nin && v && music->in[v-1].t?v:0); break;
    case 0x81: cha->resam.pin=v; break;
    case 0x82: if(cha->flag&CHAN_USE) cha->flag=(cha->flag&CHAN_FLAG_MASK)|(v&~CHAN_FLAG_MASK); break;
    case 0x90: cha->env1=(v+6<music->size?v:0); cha->pos1=0; break;
    case 0x91: cha->pos1=v; break;
    case 0x92: cha->env2=(v+6<music->size?v:0); cha->pos2=0; break;
    case 0x93: cha->pos2=v; break;
    case 0xC0: if(cha->instrument && ((12>>music->in[cha->instrument-1].t)&1)) music->in[cha->instrument-1].dline->duse=v; break;
  }
}

static double get_special_r(const Channel*cha,Uint8 id) {
  if((id&0x40) && !cha) return 0.0;
  switch(id) {
    case 0x40: return cha->freq/(cha->instrument && music->in[cha->instrument-1].t?music->in[cha->instrument-1].rate:1.0);
    case 0x41: return cha->amp;
    case 0x42: return cha->freq;
    case 0x43: return cha->resam.offset;
    case 0x60: if(cha->instrument && ((12>>music->in[cha->instrument-1].t)&1)) return music->in[cha->instrument-1].dline->duse/(float)music->in[cha->instrument-1].dline->dlen; else return 0.0; break;
    case 0x61: if(cha->instrument && ((12>>music->in[cha->instrument-1].t)&1)) return music->in[cha->instrument-1].dline->filter; else return 0.0; break;
    case 0x62: if(cha->instrument && ((12>>music->in[cha->instrument-1].t)&1)) return music->in[cha->instrument-1].dline->lfod; else return 0.0; break;
    default: return 0.0;
  }
}

static void put_special_r(Channel*cha,Uint8 id,double v) {
  if((id&0x40) && !cha) return;
  switch(id) {
    case 0x40: if(cha->instrument) cha->freq=v*music->in[cha->instrument-1].rate; break;
    case 0x41: cha->amp=v; break;
    case 0x42: cha->freq=v; break;
    case 0x43: cha->resam.offset=v; break;
    case 0x60: if(cha->instrument && ((12>>music->in[cha->instrument-1].t)&1)) music->in[cha->instrument-1].dline->duse=fmin(1.0,fmax(v,0.0))*(float)music->in[cha->instrument-1].dline->dlen; break;
    case 0x61: if(cha->instrument && ((12>>music->in[cha->instrument-1].t)&1)) music->in[cha->instrument-1].dline->filter=v; break;
    case 0x62: if(cha->instrument && ((12>>music->in[cha->instrument-1].t)&1)) music->in[cha->instrument-1].dline->lfod=v; break;
  }
}

static double get_real(Uint8 id,const Thread*thr) {
  double v=0.0;
  switch(id&0x7F) {
    case 0: v=0.0; break;
    case 1: v=1.0; break;
    case 2: v=2.0; break;
    case 3: v=0.5; break;
    case 4: v=thr->x; break;
    case 5: v=thr->y; break;
    case 6: v=music->z; break;
    case 8: v=thr->a; break;
    case 9: v=thr->b; break;
    case 10: v=music->g; break;
    case 11: v=thr->n; break;
    case 16 ... 127: if((id&0x7F)-16<music->nrc) v=music->rc[(id&0x7F)-16]; break;
  }
  return id&0x80?-v:v;
}

static char do_envelope(Channel*cha,Uint16 addr,Uint8*pos) {
  const Uint8*p=music->rom+addr;
  Uint8 c;
  Uint8 m=*pos;
  double v;
  if(cha->flag&CHAN_LOOP) {
    if(*pos>=p[2]) {
      if(p[1]==255) return 1;
      *pos=p[1];
    }
    c=p[4+*pos];
  } else {
    c=p[m+4];
    if(c==128 && ((p[1]&0x80) || !(*p&1))) return 1;
  }
  ++*pos;
  m=(m || !(*p&0x10))?(*p&1):0;
  if(!m && (p[1]&0x80)) {
    // Integer, absolute
    put_special_i(cha,p[1],c);
  } else if(!m) {
    // Real, absolute
    put_special_r(cha,p[1],get_real(c,music->th));
  } else if(p[1]&0x80) {
    // Integer, relative
    put_special_i(cha,p[1],get_special_i(cha,p[1],0)+c-((c&0x80)<<1));
  } else if(c&0x80) {
    // Real, multiplication
    put_special_r(cha,p[1],get_special_r(cha,p[1])*get_real(c&0x7F,music->th));
  } else {
    // Real, addition
    put_special_r(cha,p[1],get_special_r(cha,p[1])+get_real(c,music->th));
  }
  return (!c && !(cha->flag&CHAN_LOOP));
}

static void emulator_multi_poke(Channel*ch,Uint16 x,Uint16 y) {
  Uint8 f=music->rom[x++];
  Uint8 n=music->rom[x++];
  Uint16 a=0;
  Uint16 b;
  while(n--) {
    if(f&2) {
      a=music->rom[x++];
      if(f&4) a|=music->rom[x++]<<8;
    } else {
      a++;
    }
    b=music->rom[x++];
    if(f&1) b|=music->rom[x++]<<8;
    emulator[ch->emu].em->poke(ch->state,a+y,b);
  }
}

#define StackReq(A,B) if(thr->t>=8+B || thr->t<A) break;
static inline void render_music(Sint16*buf,int len) {
  Uint8 midi[4];
  Channel*cha;
  Thread*thr;
  float v;
  double x,y;
  static float*fbuf=0;
  static int flen=0;
  int c,m,pos,bpos;
  if(flen<len) {
    fbuf=realloc(fbuf,(flen=len)*sizeof(float));
    if(!fbuf) err(1,"Allocation failed during audio callback");
  }
  for(pos=0;pos<len;pos++) fbuf[pos]=0.0;
  for(bpos=pos=0;pos<len;pos++) {
    if((music->tcur+=music->tempo)>=music->tmax) {
      render_music_frame(fbuf+bpos,pos-bpos);
      music->tcur-=music->tmax;
      bpos=pos+1;
      for(m=0;m<music->nch;m++) {
        if((music->ch[m].flag&CHAN_ENV1) && music->ch[m].env1 && do_envelope(music->ch+m,music->ch[m].env1-1,&music->ch[m].pos1)) music->ch[m].flag&=~CHAN_ENV1;
        if((music->ch[m].flag&CHAN_ENV2) && music->ch[m].env2 && do_envelope(music->ch+m,music->ch[m].env2-1,&music->ch[m].pos2)) music->ch[m].flag&=~CHAN_ENV2;
      }
      for(m=0;m<music->nth;m++) {
        thr=music->th+m;
        while(!thr->w && thr->p!=0xFFFF) {
#if 0
          for(c=0;c<thr->t;c++) printf(" $%04X",thr->s[c]);
          printf(" :: p=$%04X op=$%02X x=%f y=%f\n",thr->p,music->rom[thr->p],thr->x,thr->y);
#endif
          c=music->rom[thr->p++];
          reswitch: switch(c) {
            case 0x00 ... 0x1F: StackReq(0,1); thr->r=thr->p; thr->s[thr->t++]=c; thr->p=music->sc[0]; break;
            case 0x20 ... 0x3F: StackReq(0,1); thr->r=thr->p; thr->s[thr->t++]=c; thr->p=music->sc[1]; break;
            case 0x40 ... 0x5F: StackReq(0,1); thr->r=thr->p; thr->s[thr->t++]=c; thr->p=music->sc[2]; break;
            case 0x60 ... 0x7F: StackReq(0,1); thr->r=thr->p; thr->s[thr->t++]=c; thr->p=music->sc[3]; break;
            case 0x80: StackReq(2,1); thr->t--; thr->s[thr->t-1]+=thr->s[thr->t]; break;
            case 0x81: StackReq(2,1); thr->t--; thr->s[thr->t-1]-=thr->s[thr->t]; break;
            case 0x82: StackReq(2,1); thr->t--; thr->s[thr->t-1]*=thr->s[thr->t]; break;
            case 0x83: StackReq(2,1); thr->t--; thr->s[thr->t-1]/=thr->s[thr->t]; break;
            case 0x84: StackReq(2,1); thr->t--; thr->s[thr->t-1]/=(Sint16)thr->s[thr->t]; break;
            case 0x85: StackReq(2,1); thr->t--; thr->s[thr->t-1]%=thr->s[thr->t]; break;
            case 0x86: StackReq(2,1); thr->t--; thr->s[thr->t-1]<<=thr->s[thr->t]; break;
            case 0x87: StackReq(2,1); thr->t--; thr->s[thr->t-1]>>=thr->s[thr->t]; break;
            case 0x88: StackReq(2,1); thr->t--; thr->s[thr->t-1]=((Sint16)thr->s[thr->t-1])>>thr->s[thr->t]; break;
            case 0x89: StackReq(2,1); thr->t--; thr->s[thr->t-1]&=thr->s[thr->t]; break;
            case 0x8A: StackReq(2,1); thr->t--; thr->s[thr->t-1]|=thr->s[thr->t]; break;
            case 0x8B: StackReq(2,1); thr->t--; thr->s[thr->t-1]^=thr->s[thr->t]; break;
            case 0x8C: StackReq(2,1); thr->t--; thr->s[thr->t-1]+=thr->s[thr->t]; break;
            case 0x8D: StackReq(2,1); thr->t--; thr->s[thr->t-1]+=thr->s[thr->t]; break;
            case 0x8E: StackReq(2,1); thr->t--; thr->s[thr->t-1]=music->rom[(thr->s[thr->t-1]+thr->s[thr->t])&0xFFFF]; break;
            case 0x8F: StackReq(2,1); thr->t--; c=(thr->s[thr->t-1]*2+thr->s[thr->t])&0xFFFF; thr->s[thr->t-1]=music->rom[c]|(music->rom[c+1]<<8); break;
            case 0x90: StackReq(0,1); thr->s[thr->t++]=thr->a; break;
            case 0x91: StackReq(0,1); thr->s[thr->t++]=thr->b; break;
            case 0x92: StackReq(0,1); thr->s[thr->t++]=thr->c; break;
            case 0x93: StackReq(0,1); thr->s[thr->t++]=music->g; break;
            case 0x94: StackReq(0,1); thr->s[thr->t++]=thr->s[0]; break;
            case 0x95: StackReq(0,1); thr->s[thr->t++]=thr->s[1]; break;
            case 0x96: StackReq(0,1); thr->s[thr->t++]=thr->n; break;
            case 0x97: StackReq(0,1); thr->s[thr->t++]=thr->r; break;
            case 0x98: StackReq(1,0); thr->a=thr->s[--thr->t]; break;
            case 0x99: StackReq(1,0); thr->b=thr->s[--thr->t]; break;
            case 0x9A: StackReq(1,0); thr->c=thr->s[--thr->t]; if(thr->c>music->nch || !thr->c || !(music->ch[thr->c-1].flag&CHAN_USE)) thr->c=0; break;
            case 0x9B: StackReq(1,0); music->g=thr->s[--thr->t]; break;
            case 0x9C: StackReq(1,0); thr->s[0]=thr->s[--thr->t]; break;
            case 0x9D: StackReq(1,0); thr->s[1]=thr->s[--thr->t]; break;
            case 0x9E: StackReq(1,0); thr->n=thr->s[--thr->t]; break;
            case 0x9F: StackReq(1,0); thr->r=thr->s[--thr->t]; break;
            case 0xA0: c=music->rom[thr->p++]; thr->p+=c-(c&0x80?256:0); break;
            case 0xA1: c=music->rom[thr->p++]; if(thr->t && !thr->s[--thr->t]) thr->p+=c-(c&0x80?256:0); break;
            case 0xA2: c=music->rom[thr->p++]; if(thr->t && thr->s[--thr->t]) thr->p+=c-(c&0x80?256:0); break;
            case 0xA3: c=music->rom[thr->p++]; StackReq(2,0); thr->t-=2; if(thr->s[thr->t]<thr->s[thr->t+1]) thr->p+=c-(c&0x80?256:0); break;
            case 0xA4: c=music->rom[thr->p++]; StackReq(2,0); thr->t-=2; if(thr->s[thr->t]==thr->s[thr->t+1]) thr->p+=c-(c&0x80?256:0); break;
            case 0xA5: c=music->rom[thr->p++]; StackReq(2,0); thr->t-=2; if(thr->s[thr->t]!=thr->s[thr->t+1]) thr->p+=c-(c&0x80?256:0); break;
            case 0xA6: c=music->rom[thr->p++]; thr->r=thr->p; thr->p+=c-(c&0x80?256:0); break;
            case 0xA7: c=music->rom[thr->p++]; StackReq(0,1); thr->s[thr->t++]=thr->p; thr->p+=c-(c&0x80?256:0); break;
            case 0xA8: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; thr->p=c; break;
            case 0xA9: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; if(thr->t && !thr->s[--thr->t]) thr->p=c; break;
            case 0xAA: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; if(thr->t && thr->s[--thr->t]) thr->p=c; break;
            case 0xAB: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; StackReq(2,0); thr->t-=2; if(thr->s[thr->t]<thr->s[thr->t+1]) thr->p=c; break;
            case 0xAC: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; StackReq(2,0); thr->t-=2; if(thr->s[thr->t]==thr->s[thr->t+1]) thr->p=c; break;
            case 0xAD: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; StackReq(2,0); thr->t-=2; if(thr->s[thr->t]!=thr->s[thr->t+1]) thr->p=c; break;
            case 0xAE: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; thr->r=thr->p; thr->p=c; break;
            case 0xAF: c=music->rom[thr->p++]; c|=music->rom[thr->p++]<<8; StackReq(0,1); thr->s[thr->t++]=thr->p; thr->p=c; break;
            case 0xB0 ... 0xB3: StackReq(0,1); thr->s[thr->t++]=c&3; break;
            case 0xB4: StackReq(0,1); thr->s[thr->t++]=music->rom[thr->p++]; break;
            case 0xB5: StackReq(0,1); thr->s[thr->t++]=music->rom[thr->p]|(music->rom[thr->p+1]<<8); thr->p+=2; break;
            case 0xB6: if(thr->t) thr->s[thr->t-1]++; break;
            case 0xB7: if(thr->t) thr->s[thr->t-1]--; break;
            case 0xB8: StackReq(0,1); thr->s[thr->t++]=get_special_i(thr->c?music->ch+thr->c-1:0,music->rom[thr->p++],m); break;
            case 0xB9: StackReq(1,0); put_special_i(thr->c?music->ch+thr->c-1:0,music->rom[thr->p++],thr->s[--thr->t]); break;
            case 0xBA: StackReq(1,1); c=music->rom[thr->p++]; put_special_i(thr->c?music->ch+thr->c-1:0,c,get_special_i(thr->c?music->ch+thr->c-1:0,c,0)+thr->s[--thr->t]); break;
            case 0xBB: StackReq(1,1); c=music->rom[thr->p++]; put_special_i(thr->c?music->ch+thr->c-1:0,c,get_special_i(thr->c?music->ch+thr->c-1:0,c,0)-thr->s[--thr->t]); break;
            case 0xBC: c=music->rom[thr->p++]; x=get_special_r(thr->c?music->ch+thr->c-1:0,c&0x7F); if(c&0x80) thr->y=x; else thr->x=x; break;
            case 0xBD: c=music->rom[thr->p++]; put_special_r(thr->c?music->ch+thr->c-1:0,c&0x7F,c&0x80?thr->y:thr->x); break;
            case 0xBE: StackReq(0,1); thr->p++; thr->s[thr->t++]=0; break;
            case 0xBF: thr->p++; if(thr->t) thr->s[thr->t-1]=0; break;
            case 0xC0: thr->x=get_real(music->rom[thr->p++],thr); break;
            case 0xC1: thr->y=get_real(music->rom[thr->p++],thr); break;
            case 0xC2: thr->x+=get_real(music->rom[thr->p++],thr); break;
            case 0xC3: thr->x*=get_real(music->rom[thr->p++],thr); break;
            case 0xC4: thr->x/=get_real(music->rom[thr->p++],thr); break;
            case 0xC5: thr->x=pow(thr->x,get_real(music->rom[thr->p++],thr)); break;
            case 0xC6: thr->x=fmod(thr->x,get_real(music->rom[thr->p++],thr)); break;
            case 0xC7: thr->x=fmin(thr->x,get_real(music->rom[thr->p++],thr)); break;
            case 0xC8:
              c=music->rom[thr->p++];
              switch(c>>6) {
                case 0: x=thr->x; break;
                case 1: x=thr->y; break;
                case 2: x=music->z; break;
              }
              if(c&0x08) x*=TAU;
              if(c&0x10) x=-x;
              switch(c&7) {
                case 0: x=thr->x; break;
                case 1: y=thr->x; thr->x=x; x=y; break;
                case 2: x=fabs(x); break;
                case 3: x=sqrt(x); break;
                case 4: x=sin(x); break;
                case 5: x=cos(x); break;
                case 6: x=floor(x); break;
              }
              if(c&7) switch(c>>6) {
                case 0: thr->x=(c&0x20?-x:x); break;
                case 1: thr->y=(c&0x20?-x:x); break;
                case 2: music->z=(c&0x20?-x:x); break;
              }
              break;
            case 0xC9: StackReq(1,0); thr->x=get_real((thr->s[--thr->t]+music->rom[thr->p++])&0x7F,thr); break;
            case 0xCA: thr->w=255; thr->p=0xFFFF; break;
            case 0xCB: if(thr->t>1) c=thr->s[thr->t-1],thr->s[thr->t-1]=thr->s[thr->t-2],thr->s[thr->t-2]=c; break;
            case 0xCC: if(thr->t) --thr->t; break;
            case 0xCD: if(thr->t<8) thr->s[thr->t]=thr->s[thr->t-1],thr->t++; break;
            case 0xCE: StackReq(2,3); thr->s[thr->t]=thr->s[thr->t-2]; thr->t++; break;
            case 0xCF: StackReq(3,3); thr->s[thr->t]=thr->s[thr->t-3]; thr->t++; break;
            case 0xD0:
              StackReq(2,0); thr->t-=2;
              if(!thr->c) break;
              cha=music->ch+thr->c-1;
              if(cha->flag&CHAN_EMULATE) emulator[cha->emu].em->send(cha->state,music->rom+thr->s[thr->t],thr->s[thr->t+1]);
              break;
            case 0xD1:
              StackReq(1,1);
              if(!thr->c) break;
              cha=music->ch+thr->c-1;
              if(cha->flag&CHAN_EMULATE) thr->s[thr->t-1]=emulator[cha->emu].em->peek(cha->state,thr->s[thr->t-1]);
              break;
            case 0xD2:
              StackReq(2,0); thr->t-=2;
              if(thr->s[thr->t+1]>=0 && thr->s[thr->t+1]<music->size) music->rom[thr->s[thr->t+1]]=thr->s[thr->t];
              break;
            case 0xD3:
              StackReq(2,0); thr->t-=2;
              if(thr->s[thr->t+1]>=0 && thr->s[thr->t+1]<music->size-1) music->rom[thr->s[thr->t+1]]=thr->s[thr->t],music->rom[thr->s[thr->t+1]+1]=thr->s[thr->t]>>8;
              break;
            case 0xD4:
              StackReq(2,0); thr->t-=2;
              if(!thr->c) break;
              cha=music->ch+thr->c-1;
              if(cha->flag&CHAN_EMULATE) emulator_multi_poke(cha,thr->s[thr->t+1],thr->s[thr->t]);
              break;
            case 0xE0 ... 0xEF: if(thr->t) thr->w=thr->s[--thr->t]; c+=0x10; goto reswitch;
            case 0xF0: thr->p=thr->r; break;
            case 0xF1: if(thr->t) thr->p=thr->s[--thr->t]; break;
            case 0xF2: thr->l=thr->p; if(thr->t) thr->n=thr->s[--thr->t]; break;
            case 0xF3: if(!thr->n) thr->p=thr->m; break;
            case 0xF4: thr->m=thr->p; if(thr->n) thr->n--,thr->p=thr->l; break;
            case 0xF5:
              if(!thr->c) break;
              cha=music->ch+thr->c-1;
              if(!(cha->flag&CHAN_EMULATE)) {
                if(!cha->instrument) break;
                cha->flag=CHAN_USE|CHAN_SOUND|CHAN_LOOP;
                c=cha->instrument-1;
                cha->resam.pin=cha->resam.pout=0;
                if(thr->x>0.0) cha->freq=music->in[c].rate*thr->x;
                resample_reset(&cha->resam);
                switch(music->in[c].t) {
                  case INST_DELAYLINE: case INST_DELAYLINE_COPY:
                    music->in[c].dline->resam.pin=music->in[c].dline->resam.pout=0;
                    resample_reset(&music->in[c].dline->resam);
                    memset(music->in[c].dline->line,0,music->in[c].dline->dlen*sizeof(float));
                    music->in[c].dline->dpos=music->in[c].dline->dlen;
                    music->in[c].dline->amp=1.0;
                    music->in[c].dline->lfox=0.0;
                    music->in[c].dline->lfoy=music->in[c].dline->lfod;
                    music->in[c].dline->prev=0.0;
                    if(music->in[c].dline->option&DLOP_DUSEFREQ) music->in[c].dline->duse=((Uint32)(music->in[c].dline->dlen/cha->freq+0.5))?:1;
                    if(music->in[c].dline->option&DLOP_CONSTRATE) cha->freq=1.0;
                    break;
                }
              } else {
                cha->flag=CHAN_USE|CHAN_SOUND|CHAN_LOOP|CHAN_EMULATE;
              }
              if(cha->env1) {
                cha->flag|=CHAN_ENV1;
                if(music->rom[cha->env1-1]&0x02) {
                  cha->pos1=0;
                  do_envelope(cha,cha->env1-1,&cha->pos1);
                }
              }
              if(cha->env2) {
                cha->flag|=CHAN_ENV2;
                if(music->rom[cha->env2-1]&0x02) {
                  cha->pos2=0;
                  do_envelope(cha,cha->env2-1,&cha->pos2);
                }
              }
              break;
            case 0xF6:
              if(!thr->c) break;
              cha=music->ch+thr->c-1;
              cha->flag&=~CHAN_LOOP;
              if(cha->env1 && !(music->rom[cha->env1-1]&0x24)) cha->flag&=~CHAN_ENV1;
              if(cha->env2 && !(music->rom[cha->env2-1]&0x24)) cha->flag&=~CHAN_ENV2;
              break;
            case 0xF7:
              if(!thr->c) break;
              cha=music->ch+thr->c-1;
              cha->flag&=~(CHAN_SOUND|CHAN_LOOP);
              if(cha->env1 && !(music->rom[cha->env1-1]&0x08)) cha->flag&=~CHAN_ENV1;
              if(cha->env2 && !(music->rom[cha->env2-1]&0x08)) cha->flag&=~CHAN_ENV2;
              break;
            case 0xF8:
              StackReq(2,0); thr->t-=2; if(!thr->c) break; cha=music->ch+thr->c-1;
              if(cha->flag&CHAN_EMULATE) emulator[cha->emu].em->poke(cha->state,thr->s[thr->t+1],thr->s[thr->t]);
              break;
            case 0xF9 ... 0xFB:
              StackReq((c-0xF8),0); thr->t-=(c-0xF8); if(!thr->c) break; cha=music->ch+thr->c-1;
              midi[0]=thr->s[thr->t]; if(c>0xF9) midi[1]=thr->s[thr->t+1]; if(c>0xFA) midi[2]=thr->s[thr->t+2];
              if(cha->flag&CHAN_EMULATE) emulator[cha->emu].em->send(cha->state,midi,c-0xF8);
              break;
            case 0xFF: break;
          }
        }
        if(thr->w) --thr->w;
      }
    }
  }
  if(bpos!=pos) render_music_frame(fbuf+bpos,pos-bpos);
  for(pos=0;pos<len;pos++) buf[pos]=fmax(-32767.5,fmin(fbuf[pos]*config.music_amp+buf[pos],+32767.0));
}
#undef StackReq

static void dump_music_state(void) {
  Channel*cha;
  Thread*thr;
  int i;
  if(!music) return;
  printf("\n*** Music ***\nname=\"%s\" song=%u on=%u\n",music_name,music_song,music_on);
  printf("z=%f tcur=%lu tmax=%lu g=%u size=%u tempo=%u\n",music->z,(unsigned long)music->tcur,(unsigned long)music->tmax,music->g,music->size,music->tempo);
  printf("*** Channels ***\n");
  for(i=0;i<music->nch;i++) {
    cha=music->ch+i;
    printf("%d: resam.pin=%lu resam.pout=%lu freq=%f amp=%f env1=%d env2=%d instrument=%d flag=0x%X pos1=%d pos2=%d\n",i+1,
     (unsigned long)cha->resam.pin,(unsigned long)cha->resam.pout,cha->freq,cha->amp,cha->env1,cha->env2,cha->instrument,cha->flag,cha->pos1,cha->pos2);
  }
  printf("*** Threads ***\n");
  for(i=0;i<music->nth;i++) {
    thr=music->th+i;
    printf("%d: x=%f y=%f s={%d,%d,%d,%d,%d,%d,%d,%d} a=%u b=%u l=%u m=%u p=%u r=%u c=%u n=%u t=%u w=%u\n",i,
     thr->x,thr->y,thr->s[0],thr->s[1],thr->s[2],thr->s[3],thr->s[4],thr->s[5],thr->s[6],thr->s[7],
     thr->a,thr->b,thr->l,thr->m,thr->p,thr->r,thr->c,thr->n,thr->t,thr->w);
  }
  printf("*** End ***\n");
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
      break;
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
        next:
        cfreq=queue[qfirst].note;
        cpos=0;
        cmax=queue[qfirst].len;
        qfirst=(qfirst+1)&MAXQUEUE_MASK;
        if(cfreq==LOOP_NOTE) {
          qfirst=cmax;
          goto next;
        }
        pha=0.0;
      }
    }
  }
  if(music_on && music && music->rom) render_music(buf,len);
}

static int convertaudio(WaveSound*wav,Uint16 rate,Uint8 ratemode,Uint16 form,Resample*resam) {
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
  static const Sint8 scale[8]={9,11,0,2,4,5,7};
  Uint8 c;
  Uint32 n,d;
  Uint16 pr;
  Uint16 oct=3*12;
  Uint32 dur=whole_note/32;
  Sint32 loo=-1;
  float f;
  if(muted) return;
  SDL_LockAudio();
  if(*m=='@') {
    m++;
    if(config.music_debug==255 && *m=='|') {
      dump_music_state();
      SDL_UnlockAudio();
      return;
    }
    if((c=*m)&0x40) {
      if(c>='a' && c<='z') c+='A'-'a';
      m++;
    }
    n=0;
    while(*m>='0' && *m<='9') n=10*n+*m++-'0';
    if(c!='I' && (n<priority || (n==priority && (n&1)))) goto end;
    if(c!='Q' || !priority) {
      priority=n;
      qfirst=qlast=0;
    }
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
      // Loop
      case 'J': loo=qlast; break;
    }
  }
  end:
  if(loo!=-1 && loo!=qlast && ((qlast+1)&MAXQUEUE_MASK)!=qfirst) {
    queue[qlast].note=LOOP_NOTE;
    queue[qlast].len=loo;
    qlast=(qlast+1)&MAXQUEUE_MASK;
  }
  if(cfreq==-1 && qfirst!=qlast) {
    cfreq=queue[qfirst].note;
    cpos=0;
    cmax=queue[qfirst].len;
    qfirst=(qfirst+1)&MAXQUEUE_MASK;
  }
  SDL_UnlockAudio();
}

static void unload_bgm(void) {
  int i;
  *music_name=0;
  if(!music) return;
  for(i=0;i<music->nch;i++) if(music->ch[i].flag) {
    if(music->ch[i].flag&CHAN_EMULATE) emulator[music->ch[i].emu].em->destroy(music->ch[i].state);
    else if(i) resample_uninit(&music->ch[i].resam);
    music->ch[i].state=0;
    music->ch[i].flag=0;
  }
  if(music->in) for(i=0;i<music->nin;i++) switch(music->in[i].t) {
    case INST_WAVE: free(music->in[i].wave.d8); break;
    case INST_DELAYLINE:
      free(music->in[i].dline->d8);
      // fall through
    case INST_DELAYLINE_COPY:
      free(music->in[i].dline->line);
      free(music->in[i].dline);
      break;
  }
  music->ch=realloc(music->ch,sizeof(Channel))?:music->ch;
  free(music->rom);
  free(music->in);
  free(music->th);
  free(music->rc);
  music->rom=0;
  music->in=0;
  music->th=0;
  music->rc=0;
  music->nch=1;
  music->nin=music->nth=music->nrc=0;
  music->size=0;
}

static void load_emulation_channel(Channel*o,const ASN1_Value*v0) {
  double freq=1.0;
  int i;
  ASN1_Value v1;
  if(asn1_first_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || (v1.type!=ASN1_OID && v1.type!=ASN1_RELATIVE_OID)) {
    asn1error:
    if(config.music_debug) fprintf(stderr,"ASN.1 error in emulation channel in %s.BGM\n",music_name);
    return;
  }
  for(i=0;i<nemulator;i++) {
    if(v1.type==ASN1_RELATIVE_OID) {
      if(emulator[i].id.oidlen<0x14 || memcmp(emulator[i].id.oid,"\x69\x82\xA8\xAC\x87\xDD\x97\x84\xA0\xC7\xDF\x96\xE9\x80\x84\xE1\xC3\xD1\xA8\x16",0x14)) continue;
      if(v1.length==emulator[i].id.oidlen-0x14 && !memcmp(emulator[i].id.oid+0x14,v1.data,v1.length)) break;
    } else {
      if(v1.length==emulator[i].id.oidlen && !memcmp(emulator[i].id.oid,v1.data,v1.length)) break;
    }
  }
  if(i==nemulator) {
    if(config.music_debug>3) {
      fputs("No emulator for OID ",stderr);
      if(asn1_print_decimal_oid(&v1,ASN1_AUTO,stderr)) fputs("<Invalid OID>",stderr);
      fputc('\n',stderr);
    }
    return;
  }
  o->emu=i;
  i=0;
  if(asn1_next_of(&v1,v0)) goto ok;
  if(v1.class!=ASN1_UNIVERSAL) goto asn1error;
  if(v1.type==ASN1_REAL) {
    if(asn1_decode_number(&v1,ASN1_REAL,&freq)) goto asn1error;
    if(asn1_next_of(&v1,v0)) goto ok;
  }
  if(v1.type!=ASN1_SEQUENCE) goto asn1error;
  i=1;
  ok:
  o->state=emulator[o->emu].em->create(emulator[o->emu].em->userdata,freq*spec.freq,i?v1.data:0,i?v1.length:0);
  if(o->state) o->flag=CHAN_USE|CHAN_EMULATE; else o->flag=0;
}

static void load_instrument_codec(Uint8*d8,Uint16*d16,Uint8 c,const Uint8*d,Uint32 len) {
  Uint32 a;
  Uint8 m,p;
  switch(c) {
    case 0: // Unsigned 8-bits
      memcpy(d8,d,len); break;
    case 1: // ADPCM 4-bits
      for(a=0,m=1,p=128;a<len;) {
        c=*d++;
        d8[a++]=adpcm4bits(c>>4,p,&m);
        d8[a++]=adpcm4bits(c&15,p,&m);
      }
      break;
    case 2: // ADPCM 3-bits
      for(a=0,m=0,p=128;a<len;) {
        c=*d++;
        d8[a++]=adpcm3bits((c>>5)&7,p,&m);
        d8[a++]=adpcm3bits((c>>2)&7,p,&m);
        d8[a++]=adpcm3bits((c<<1)&7,p,&m);
      }
      break;
    case 3: // ADPCM 2-bits
      for(a=0,m=0,p=128;a<len;) {
        c=*d++;
        d8[a++]=adpcm2bits((c>>6)&3,p,&m);
        d8[a++]=adpcm2bits((c>>4)&3,p,&m);
        d8[a++]=adpcm2bits((c>>2)&3,p,&m);
        d8[a++]=adpcm2bits((c>>0)&3,p,&m);
      }
      break;
    case 4: // Signed 16-bits
      for(a=0;a<len;a++,d+=2) d16[a]=d[0]|(d[1]<<8);
      break;
    case 6: // A-law
      for(a=0;a<len;a++) d16[a]=d[a]&128?-alaw[d[a]&127]:alaw[d[a]&127];
      break;
    case 7: // mu-law
      for(a=0;a<len;a++) d16[a]=d[a]&128?mulaw[d[a]&127]:(-mulaw[d[a]&127]-(d[a]==127));
      break;
    default: errx(1,"Unknown codec (%d) in %s.BGM",c,music_name);
  }
}

static void load_instrument(Instrument*o,const ASN1_Value*v0) {
  int i;
  double r;
  ASN1_Value v1,v2;
  Uint32 a;
  Uint8 c;
  const Uint8*d;
  if(v0->class==ASN1_UNIVERSAL && v0->type==ASN1_NULL) return;
  if(v0->class!=ASN1_CONTEXT_SPECIFIC || !v0->constructed) error: errx(1,"Invalid instrument in %s.BGM",music_name);
  if(asn1_first_of(&v1,v0)) goto error;
  if(v1.class==ASN1_UNIVERSAL && (v1.type==ASN1_GRAPHIC_STRING || v1.type==ASN1_TRON_STRING) && asn1_next_of(&v1,v0)) goto error;
  switch(o->t=v0->type) {
    case INST_WAVE:
      if(v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_REAL || asn1_decode_double(&v1,ASN1_REAL,&o->rate)) goto error;
      o->rate/=(double)spec.freq;
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_ENUMERATED || asn1_decode_number(&v1,ASN1_INTEGER,&c)) goto error;
      if(c<0 || c>7 || c==5) errx(1,"Unknown codec (%d) in %s.BGM",c,music_name);
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_OCTET_STRING || v1.constructed || !v1.length || v1.length>0x7FFFFFULL) goto error;
      if(c==4 && (v1.length&1)) goto error;
      o->wave.len=(v1.length*"\x01\x02\x03\x04\x01\x00\x01\x01"[c])>>(c==4);
      if(o->wave.is8=1&(0x000F>>c)) o->wave.d8=malloc(o->wave.len); else o->wave.d16=malloc(o->wave.len*sizeof(Sint16));
      if(!(o->wave.is8?(void*)o->wave.d8:(void*)o->wave.d16)) err(1,"Allocation failed");
      load_instrument_codec(o->wave.d8,o->wave.d16,c,v1.data,v1.length);
      o->wave.ls=o->wave.le=o->wave.len;
      if(asn1_next_of(&v1,v0)) break;
      if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_BIT_STRING && v1.length==1 && asn1_next_of(&v1,v0)) break;
      if(v1.class!=ASN1_UNIVERSAL) goto error;
      if(v1.type==ASN1_INTEGER) {
        if(asn1_decode_number(&v1,ASN1_INTEGER,&o->wave.ls)) goto error;
        if(asn1_next_of(&v1,v0)) {
          o->wave.le=o->wave.len;
        } else {
          if(asn1_decode_number(&v1,ASN1_INTEGER,&o->wave.le)) goto error;
        }
        if(o->wave.le>o->wave.len || o->wave.ls>=o->wave.le) goto error;
      } else if(v1.type!=ASN1_NULL) {
        goto error;
      }
      break;
    case INST_DELAYLINE:
      if(!(o->dline=calloc(sizeof(DelayLine),1))) err(1,"Allocation failed");
      o->dline->resam=music->ch->resam;
      if(resample_init(&o->dline->resam,0,0,0,RESAMPLE_SHARE)) errx(1,"Error copying resample object");
      if(v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_REAL || asn1_decode_double(&v1,ASN1_REAL,&o->dline->rate)) goto error;
      o->dline->rate/=(double)spec.freq;
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_REAL || asn1_decode_double(&v1,ASN1_REAL,&o->rate)) goto error;
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&c) || c<2 || c>16) goto error;
      o->dline->duse=o->dline->dlen=1UL<<c;
      if(!(o->dline->line=calloc(o->dline->dlen,sizeof(float)))) err(1,"Allocation failed");
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_ENUMERATED || asn1_decode_number(&v1,ASN1_INTEGER,&c)) goto error;
      if(c<0 || c>7 || c==5) errx(1,"Unknown codec (%d) in %s.BGM",c,music_name);
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_OCTET_STRING || v1.constructed || !v1.length || v1.length>0x7FFFFFULL) goto error;
      if(c==4 && (v1.length&1)) goto error;
      o->dline->len=(v1.length*"\x01\x02\x03\x04\x01\x00\x01\x01"[c])>>(c==4);
      if(o->dline->is8=1&(0x000F>>c)) o->dline->d8=malloc(o->dline->len); else o->dline->d16=malloc(o->dline->len*sizeof(Sint16));
      if(!(o->dline->is8?(void*)o->dline->d8:(void*)o->dline->d16)) err(1,"Allocation failed");
      load_instrument_codec(o->dline->d8,o->dline->d16,c,v1.data,v1.length);
      o->dline->ls=o->dline->len;
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL) goto error;
      if(v1.type==ASN1_INTEGER?(asn1_decode_number(&v1,ASN1_INTEGER,&o->dline->ls) || o->dline->ls>=o->dline->len):(v1.type!=ASN1_NULL)) goto error;
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_KEY_VALUE_LIST) goto error;
      if(!asn1_first_of(&v2,&v1)) for(c=0;;) {
        if(v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_INTEGER || asn1_decode_number(&v2,ASN1_INTEGER,&i)) goto error;
        o->dline->nfb=c+1;
        o->dline->fbat[c]=i;
        if(asn1_next_of(&v2,&v1) || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_REAL || asn1_decode_number(&v2,ASN1_REAL,o->dline->fb+c)) goto error;
        if(asn1_next_of(&v2,&v1)) break;
        if(++c==MaxFeedbackItems) goto error;
      }
      if(asn1_next_of(&v1,v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_REAL || asn1_decode_float(&v1,ASN1_REAL,&o->dline->ampdecay)) goto error;
      if(asn1_next_of(&v1,v0)) break;
      if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_SEQUENCE) {
        if(asn1_first_of(&v2,&v1) || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_ENUMERATED || asn1_decode_number(&v2,ASN1_INTEGER,&o->dline->lfok)) goto error;
        if(asn1_next_of(&v2,&v1) || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_ENUMERATED || asn1_decode_number(&v2,ASN1_INTEGER,&o->dline->lfos)) goto error;
        if(asn1_next_of(&v2,&v1) || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_REAL || asn1_decode_number(&v2,ASN1_REAL,&o->dline->lfoe)) goto error;
        if(asn1_next_of(&v2,&v1) || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_REAL || asn1_decode_number(&v2,ASN1_REAL,&o->dline->lfod)) goto error;
        if(asn1_next_of(&v1,v0)) break;
      }
      if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_INTEGER) {
        if(asn1_decode_number(&v1,ASN1_INTEGER,&o->dline->duse) || o->dline->duse<1 || o->dline->duse>o->dline->dlen) goto error;
        if(asn1_next_of(&v1,v0)) break;
      }
      if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_REAL) {
        if(asn1_decode_number(&v1,ASN1_REAL,&o->dline->filter)) goto error;
        if(asn1_next_of(&v1,v0)) break;
      }
      if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_BIT_STRING) {
        if(v1.constructed || v1.length<1 || v1.length>2) goto error;
        if(v1.length==2) o->dline->option=v1.data[1];
        if(asn1_next_of(&v1,v0)) break;
      }
      break;
    case INST_DELAYLINE_COPY:
      if(v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&a)) goto error;
      if(a<1 || a>music->nin || music->in[a-1].t!=INST_DELAYLINE) goto error;
      if(!(o->dline=malloc(sizeof(DelayLine)))) err(1,"Allocation failed");
      memcpy(o->dline,music->in[a-1].dline,sizeof(DelayLine));
      o->dline->resam=music->ch->resam;
      if(resample_init(&o->dline->resam,0,0,0,RESAMPLE_SHARE)) errx(1,"Error copying resample object");
      if(!(o->dline->line=calloc(o->dline->dlen,sizeof(float)))) err(1,"Allocation failed");
      break;
    default: goto error;
  }
}

static double load_realvalue(const ASN1_Value*v,Uint16 n) {
  ASN1_Value u;
  double r;
  if(v->class==ASN1_UNIVERSAL && v->type==ASN1_REAL && !v->constructed) {
    if(asn1_decode_number(v,ASN1_REAL,&r)) goto error; else return r;
  } else if(v->class!=ASN1_CONTEXT_SPECIFIC) {
    error: errx(1,"Improper real value in %s.BGM",music_name);
  } else {
    switch(v->type) {
      case 0: return spec.freq;
      case 1:
        r=0.0;
        if(asn1_first_of(&u,v)) goto error;
        do r+=load_realvalue(&u,n); while(!asn1_next_of(&u,v));
        return r;
      case 2:
        r=1.0;
        if(asn1_first_of(&u,v)) goto error;
        do r*=load_realvalue(&u,n); while(!asn1_next_of(&u,v));
        return r;
      case 3:
        if(asn1_first_of(&u,v)) goto error;
        r=load_realvalue(&u,n);
        if(asn1_next_of(&u,v)) goto error;
        return r-load_realvalue(&u,n);
      case 4:
        if(asn1_first_of(&u,v)) goto error;
        r=load_realvalue(&u,n);
        if(asn1_next_of(&u,v)) goto error;
        return r/load_realvalue(&u,n);
      case 5:
        if(asn1_first_of(&u,v)) goto error;
        return sqrt(load_realvalue(&u,n));
      case 6:
        if(asn1_first_of(&u,v)) goto error;
        return sin(load_realvalue(&u,n));
      case 7:
        if(asn1_first_of(&u,v)) goto error;
        return cos(load_realvalue(&u,n));
      case 8:
        if(asn1_first_of(&u,v)) goto error;
        r=load_realvalue(&u,n);
        if(asn1_next_of(&u,v)) goto error;
        return pow(r,load_realvalue(&u,n));
      case 9: return n;
      case 11: return M_PI;
      case 12: return TAU;
      default: goto error;
    }
  }
}

static void load_bgm(const char*name,Uint16 song) {
  ASN1_Value v0={};
  ASN1_Value v1,v2,v3;
  char same=0;
  FILE*f;
  char nam[13];
  int i;
  uint8_t cons,clas;
  uint32_t typ,a,b;
  size_t len;
  uint64_t remain;
  music_song=song;
  if(*name) {
    for(i=0;i<8;i++) {
      if(name[i]>='a' && name[i]<='z') nam[i]=name[i]+'A'-'a';
      else if(name[i]>47 && name[i]<96) nam[i]=name[i];
      else break;
    }
    nam[i]=0;
    if(*music_name && !strcmp(nam,music_name)) same=1;
    nam[i++]='.'; nam[i++]='B'; nam[i++]='G'; nam[i++]='M'; nam[i]=0;
  } else {
    same=1;
    snprintf(nam,13,"%s.BGM",music_name);
  }
  f=open_lump(nam,"r");
  if(!f) {
    if(config.music_debug>0) fprintf(stderr,"Cannot open music lump \"%s\"\n",nam);
    stop:
    unload_bgm();
    if(f) fclose(f);
    return;
  }
  if(asn1_read(f,&cons,&clas,&typ,&len,0)) {
    asn1err:
    if(config.music_debug>0) fprintf(stderr,"ASN.1 error in music lump \"%s\"\n",nam);
    goto stop;
  }
  if(clas!=ASN1_UNIVERSAL) goto asn1err;
  if(typ==ASN1_IDENTIFIED_DATA) {
    //if(same) {
    if(asn1_read(f,&cons,&clas,&typ,&len,0)) goto asn1err;
    fseek(f,len,SEEK_CUR);
    if(asn1_read(f,&cons,&clas,&typ,&len,0) || !cons || clas!=ASN1_UNIVERSAL || typ!=ASN1_SEQUENCE || !len) goto asn1err;
    //} else {
    //  if(asn1_read_item(f,&v1,0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_SET) goto asn1err;
    //  if(!asn1_first_of(&v0,&v1)) do {
    //    if(v0.class==ASN1_UNIVERSAL && v0.type==ASN1_OBJECT_IDENTIFIER && !v0.constructed && v0.length==22
    //     && !memcmp(v0.data,"\x69\x82\xA8\xAC\x87\xDD\x97\x84\xA0\xC7\xDF\x96\xE9\x80\x84\xE1\xC3\xD1\xA8\x16\x06\x01",22)) goto set_ok;
    //  } while(!asn1_next_of(&v0,&v1));
    //  asn1_free(&v1);
    //  if(config.music_debug>0) fprintf(stderr,"The lump \"%s\" is not identified as a music file\n",nam);
    //  goto stop;
    //  set_ok:
    //  asn1_free(&v1);
    //}
  } else if(typ!=ASN1_SEQUENCE) {
    goto asn1err;
  }
  remain=len;
  if(!same) unload_bgm();
  for(i=0;i<8;i++) {
    if(name[i]>='a' && name[i]<='z') music_name[i]=name[i]+'A'-'a';
    else if(name[i]>47 && name[i]<96) music_name[i]=name[i];
    else break;
  }
  music_name[i]=0;
  // Song list
  if(asn1_read_item(f,&v0,&remain)) err1: errx(1,"Error in music lump \"%s\"",nam);
  if(asn1_first_of(&v1,&v0)) errx(1,"Cannot find song %d in %s",song,nam);
  for(i=0;i<song;i++) if(asn1_next_of(&v1,&v0)) errx(1,"Cannot find song %d in %s",song,nam);
  if(v1.class!=ASN1_UNIVERSAL) goto err1;
  if(v1.type==ASN1_NULL) errx(1,"Cannot find song %d in %s",song,nam);
  if(asn1_first_of(&v2,&v1)) goto err1;
  if(v2.class==ASN1_UNIVERSAL && (v2.type==ASN1_GRAPHIC_STRING || v2.type==ASN1_TRON_STRING) && asn1_next_of(&v2,&v1)) goto err1;
  if(v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_INTEGER || asn1_decode_number(&v2,ASN1_INTEGER,&music->tempo) || !music->tempo) goto err1;
  if(asn1_next_of(&v2,&v1)) goto err1;
  if(v2.class==ASN1_UNIVERSAL && v2.type==ASN1_INTEGER && (asn1_decode_number(&v2,ASN1_INTEGER,&music_song) || asn1_next_of(&v2,&v1))) goto err1;
  for(;;) {
    Thread th={};
    if(v2.class!=ASN1_CONTEXT_SPECIFIC || v2.type!=0 || !v2.constructed || !v2.length) goto err1;
    if(asn1_first_of(&v3,&v2) || v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_INTEGER) goto err1;
    if(asn1_decode_number(&v3,ASN1_INTEGER,&th.p)) goto err1;
    if(!asn1_next_of(&v3,&v2)) {
      if(v3.class!=ASN1_UNIVERSAL || v3.type!=ASN1_INTEGER || asn1_decode_number(&v3,ASN1_INTEGER,&th.a)) goto err1;
    }
    music->th=realloc(music->th,++music->nth*sizeof(Thread));
    if(!music->th) err(1,"Allocation failed");
    music->th[music->nth-1]=th;
    if(music->nth>127) errx(1,"Error in music lump \"%s\": Too many threads in song %d",nam,song);
    if(asn1_next_of(&v2,&v1)) break;
  }
  asn1_free(&v0);
  if(same) goto endfile;
  // Tempo ratio
  if(asn1_read_item(f,&v0,&remain)) goto err1;
  if(v0.class!=ASN1_UNIVERSAL || v0.type!=ASN1_RATIONAL || !v0.length || !v0.constructed) goto err1;
  if(asn1_first_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&a)) goto err1;
  if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&b)) goto err1;
  asn1_free(&v0);
  if(!a || !b) goto err1;
  music->tmax=(spec.freq*(uint64_t)b)/a;
  if(music->tmax<2) music->tmax=2;
  // Instruments
  if(asn1_read_item(f,&v0,&remain) || v0.class!=ASN1_UNIVERSAL || v0.type!=ASN1_SEQUENCE) goto err1;
  if(v0.length) {
    music->nin=a=asn1_count(&v0);
    if(a&~0xFF) errx(1,"Error in music lump \"%s\": Too many instruments",nam);
    music->in=calloc(music->nin,sizeof(Instrument));
    if(!music->in) err(1,"Allocation failed");
    for(i=0;i<music->nin;i++) {
      if(i?asn1_next_of(&v1,&v0):asn1_first_of(&v1,&v0)) goto err1;
      load_instrument(music->in+i,&v1);
    }
  } else {
    music->nin=0; music->in=0;
  }
  asn1_free(&v0);
  // Channels
  if(asn1_read_item(f,&v0,&remain) || v0.class!=ASN1_UNIVERSAL || v0.type!=ASN1_SEQUENCE || !v0.length || !v0.constructed) goto err1;
  music->nch=a=asn1_count(&v0);
  if(a<0 || a>64) errx(1,"Error in music lump \"%s\": Too many channels",nam);
  music->ch=realloc(music->ch,music->nch*sizeof(Channel));
  if(!a || !music->ch) err(1,"Allocation failed");
  for(i=0;i<music->nch;i++) {
    if(i?asn1_next_of(&v1,&v0):asn1_first_of(&v1,&v0)) goto err1;
    if(i) memset(music->ch+i,0,sizeof(Channel)); else music->ch->flag=0,music->ch->instrument=0;
    if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_NULL) continue;
    if(v1.class!=ASN1_CONTEXT_SPECIFIC || v1.type>4) goto err1;
    if(v1.type<2) {
      music->ch[i].flag=CHAN_USE;
      if(i) {
        music->ch[i].resam=music->ch->resam;
        if(resample_init(&music->ch[i].resam,0,0,0,RESAMPLE_SHARE)) errx(1,"Error copying resample object");
      }
    } else if(v1.type==3 || v1.type==4) {
      load_emulation_channel(music->ch+i,&v1);
    }
  }
  asn1_free(&v0);
  // Short call addresses
  if(asn1_read_item(f,&v0,&remain) || v0.class!=ASN1_UNIVERSAL || v0.type!=ASN1_SEQUENCE) goto err1;
  switch(asn1_count(&v0)) {
    case 0: music->sc[0]=music->sc[1]=music->sc[2]=music->sc[3]=0; break;
    case 1:
      if(asn1_first_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+0)) goto err1;
      music->sc[3]=music->sc[2]=music->sc[1]=music->sc[0];
      break;
    case 2:
      if(asn1_first_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+0)) goto err1;
      if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+2)) goto err1;
      music->sc[3]=music->sc[2],music->sc[1]=music->sc[0];
      break;
    case 3:
      if(asn1_first_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+0)) goto err1;
      if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+1)) goto err1;
      if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+2)) goto err1;
      music->sc[3]=music->sc[2];
      break;
    case 4:
      if(asn1_first_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+0)) goto err1;
      if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+1)) goto err1;
      if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+2)) goto err1;
      if(asn1_next_of(&v1,&v0) || v1.class!=ASN1_UNIVERSAL || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,music->sc+3)) goto err1;
      break;
    default: errx(1,"Error in music lump \"%s\": Too many short call addresses",music_name);
  }
  asn1_free(&v0);
  // Real constants
  if(asn1_read_item(f,&v0,&remain) || v0.class!=ASN1_UNIVERSAL || v0.type!=ASN1_SEQUENCE) goto err1;
  if(music->nrc=a=asn1_count(&v0)) {
    if(a<1 || a>0x70) err2: errx(1,"Error in music lump \"%s\": Too many real constants",nam);
    music->rc=malloc(a*sizeof(double));
    if(!music->rc) err(1,"Allocation failed");
    for(i=0;i<music->nrc;i++) {
      if(i?asn1_next_of(&v1,&v0):asn1_first_of(&v1,&v0)) goto err1;
      if(i==music->nrc-1 && v1.class==ASN1_CONTEXT_SPECIFIC && v1.type==10) break;
      music->rc[i]=load_realvalue(&v1,0);
    }
    if(v1.class==ASN1_CONTEXT_SPECIFIC && v1.type==10) {
      if(asn1_first_of(&v2,&v1) || v2.class!=ASN1_UNIVERSAL || v2.type!=ASN1_INTEGER || asn1_decode_number(&v2,ASN1_INTEGER,&a)) goto err1;
      if(!a || (a&~0x7F) || a+i>0x70) goto err2;
      music->rc=realloc(music->rc,(music->nrc=i+a)*sizeof(double));
      if(!music->rc) err(1,"Allocation failed");
      if(asn1_next_of(&v2,&v1)) goto err1;
      for(a=0;i<music->nrc;i++) music->rc[i]=load_realvalue(&v2,a++);
    }
  }
  asn1_free(&v0);
  // VM codes
  if(asn1_read(f,&cons,&clas,&typ,&len,&remain) || cons || clas!=ASN1_UNIVERSAL || typ!=ASN1_OCTET_STRING || len<1 || len>0xFFFE) goto err1;
  music->rom=malloc(music->size=len);
  if(!music->rom) err(1,"Allocation failed");
  fread(music->rom,1,len,f);
  // Done with file
  endfile:
  fclose(f);
  if(config.music_debug>98) {
    printf("Loaded music \"%s\", %d, %d\n",music_name,song,music_song);
    for(i=0;i<music->nin;i++) {
      printf("Instrument #%d: Type(%d) Rate(%f)\n",i+1,music->in[i].t,music->in[i].rate);
      switch(music->in[i].t) {
        case INST_WAVE:
          printf("  len=%lu ls=%lu le=%lu is8=%u\n",(unsigned long)music->in[i].wave.len,(unsigned long)music->in[i].wave.ls,(unsigned long)music->in[i].wave.le,music->in[i].wave.is8);
          break;
        case INST_DELAYLINE: case INST_DELAYLINE_COPY:
          printf("  rate=%f\n",music->in[i].dline->rate);
          for(a=0;a<music->in[i].dline->nfb;a++) printf("  %d %f\n",music->in[i].dline->fbat[a],music->in[i].dline->fb[a]);
          break;
      }
    }
    for(i=0;i<music->nrc;i++) printf("Real #%d: %4.8g\n",i+16,music->rc[i]);
    for(i=0;i<music->nch;i++) printf("Channel #%d: flag=0x%02X emu=0x%02X\n",i+1,music->ch[i].flag,music->ch[i].emu);
  }
  // Reset state
  music->tcur=music->tmax-1;
  for(i=0;i<music->nch;i++) {
    if(music->ch[i].flag&=CHAN_FLAG_MASK) {
      if(music->ch[i].flag&CHAN_EMULATE) emulator[music->ch[i].emu].em->reset(music->ch[i].state);
      else if(music->ch[i].flag&CHAN_USE) resample_reset(&music->ch[i].resam);
    }
    music->ch[i].amp=1.0;
    music->ch[i].freq=1.0;
    music->ch[i].instrument=0;
  }
  for(i=0;i<music->nth;i++) if(i<music->nch && (music->ch[i].flag&CHAN_USE)) music->th[i].c=i+1;
}

void audio_set_music(const char*name,Uint16 song) {
  if(!spec.freq) return;
  if(name && !*name && !*music_name) return;
  if(name && *name && !music && config.music_resample) {
    music=calloc(1,sizeof(Music));
    if(!music) err(1,"Allocation failed");
    music->nch=1;
    music->ch=calloc(1,sizeof(Channel));
    if(!music->ch) err(1,"Allocation failed");
    if(!init_resampler(&music->ch->resam,config.music_resample)) errx(1,"Improper setting for music_resample");
    config.music_resample=0;
    music_on=1;
  } else if(!music) {
    // This is ensuring that the music state is stored in the save game file, in case it is later restored with music enabled.
    snprintf(music_name,9,"%s",name);
    music_song=song;
    return;
  }
  SDL_LockAudio();
  if(name) {
    // Activate music
    load_bgm(name,song);
  } else {
    // Disactivate music
    *music_name=0;
    unload_bgm();
  }
  SDL_UnlockAudio();
}

static int emulator_callback(void*cbarg,const MUSEMUinf*inf,const MUSEMU*impl) {
  int i;
  if(nemulator==128) {
    warnx("Too many emulators are already loaded; any further emulators will be ignored");
    return 1;
  }
  for(i=0;i<nemulator;i++) if(inf->oidlen==emulator[i].id.oidlen && !memcmp(inf->oid,emulator[i].id.oid,inf->oidlen)) return 1;
  emulator=realloc(emulator,(nemulator+1)*sizeof(Emulator));
  if(!emulator) err(1,"Allocation failed");
  emulator[nemulator].id=*inf;
  emulator[nemulator].em=impl;
  nemulator++;
  return 0;
}

void audio_load_emulator(const char*name,const char*arg) {
  const char*e;
  void*d=dlopen(name,RTLD_LAZY|RTLD_LOCAL);
  typeof(&musemu_main) m;
  if(!d) {
    warnx("Error loading emulator: %s",dlerror()?:"(unknown error)");
    return;
  }
  m=dlsym(d,"musemu_main");
  if(!m) {
    dlclose(d);
    warnx("Error loading emulator \"%s\": Cannot find 'musemu_main': %s",name,dlerror()?:"(no error message)");
    return;
  }
  if(e=m(config.music_debug>1?MUSEMU_DEBUG:0,arg,emulator_callback,d)) warnx("Error loading emulator \"%s\": Main function returned error: %s",name,e);
}

void convert_sound_file(FILE*in,FILE*out,Uint8 k,Uint16 s,Uint8 u,Uint8 w) {
  int c,d;
  switch(k) {
    case 1: // Raw PCM
      c=fgetc(in); if(c==EOF) goto error;
      fputc(4,out); fputc(u&3,out); fputc(w?4:0,out); fputc(s,out); fputc(s>>8,out);
      if(w) {
        for(;c!=EOF;c=fgetc(in)) {
          d=fgetc(in); if(d==EOF) goto error;
          fputc((w==1?c:d),out); fputc((w==1?d:c)^(128&~u),out);
        }
      } else {
        for(;c!=EOF;c=fgetc(in)) fputc(c^(u&128),out);
      }
      break;
#if 0
    case 2: // Creative Voice
      
      break;
    case 3: // RIFF WAVE
      
      break;
#endif
  }
  return;
  error: alert_text("Error converting sound file");
}
