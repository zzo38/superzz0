#if 0
gcc -O3 -fno-signed-zeros -fno-trapping-math -fassociative-math -c resample.c
exit
#endif

// This is based on David Bryant's resampler program in C, from: https://github.com/dbry/audio-resampler
// However, it is almost entirely rewritten compared from the original implementation.
// That file suggests using -mavx2 but that is not supported on my computer, so is omitted.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "resample.h"

static void init_filter(Resample*obj,double*temp,float*filter,double fract,double ratio) {
  double s,e,d,r,v;
  int i;
  s=e=0.0;
  for(i=0;i<obj->ntap;i++) {
    d=fabs((obj->ntap/2+1)+fract-i)*M_PI;
    r=d/(obj->ntap/2);
    if(d!=0.0) {
      v=sin(d*ratio)/(d*ratio);
      if(obj->flag&RESAMPLE_BLACKMAN_HARRIS) v*=0.35875+0.48829*cos(r)+0.14128*cos(2.0*r)+0.01168*cos(3.0*r);
      else v*=0.5*(1.0+cos(r));
    } else {
      v=1.0;
    }
    s+=temp[i]=v;
  }
  s=1.0/s;
  for(i=obj->ntap/2;i<obj->ntap;i=obj->ntap-i-(i>=obj->ntap/2)) {
    filter[i]=(temp[i]*=s)-e;
    e+=filter[i]-temp[i];
  }
}

int resample_init(Resample*obj,int ntap,int nfil,double lpratio,uint8_t flag) {
  double*temp;
  int i,j;
  if(!obj) return RESAMPLE_ERROR_ARGUMENT;
  if(flag&RESAMPLE_SHARE) {
    flag=obj->flag;
    ntap=obj->ntap;
    nfil=obj->nfil;
    obj->buf=0;
    obj->flag|=RESAMPLE_SHARE;
  } else {
    if((ntap&3) || ntap<4 || ntap>1024 || nfil<1 || nfil>1024) return RESAMPLE_ERROR_ARGUMENT;
    memset(obj,0,sizeof(Resample));
    obj->flag=flag;
    obj->ntap=ntap;
    obj->nfil=nfil;
    obj->nsam=16*ntap;
    obj->filters=calloc(nfil+1,sizeof(float*));
    if(!obj->filters) return RESAMPLE_ERROR_MEMORY;
    temp=malloc(ntap*sizeof(double));
    if(!temp) return RESAMPLE_ERROR_MEMORY;
    obj->filters[0]=calloc(ntap*(nfil+1),sizeof(float));
    if(!obj->filters[0]) return free(temp),RESAMPLE_ERROR_MEMORY;
    for(i=0;i<nfil;i++) obj->filters[i+1]=obj->filters[i]+ntap;
    for(i=0;i<nfil;i++) init_filter(obj,temp,obj->filters[i],i/(double)nfil,lpratio);
    free(temp);
    for(j=0;j<ntap;j++) obj->filters[nfil][(j+1)%ntap]=obj->filters[0][j];
  }
  obj->buf=calloc(obj->nsam,sizeof(float));
  if(!obj->buf) return RESAMPLE_ERROR_MEMORY;
  obj->offset=ntap/2;
  obj->index=ntap;
  obj->pin=obj->pout=0;
  return RESAMPLE_OK;
}

void resample_uninit(Resample*obj) {
  if(!obj) return;
  if(!(obj->flag&RESAMPLE_SHARE)) {
    free(obj->filters[0]);
    free(obj->filters);
    obj->filters=0;
  }
  free(obj->buf);
  obj->buf=0;
}

void resample_reset(Resample*obj) {
  if(!obj) return;
  memset(obj->buf,0,obj->nsam*sizeof(float));
  obj->offset=obj->ntap/2;
  obj->index=obj->ntap;
}

static double apply_filter(float*a,float*b,int n) {
  float s=0.0;
  n--;
  do s+=a[0]*b[0]+a[n]*b[n],a++,b++; while((n-=2)>0);
  return s;
}

static double subsample(Resample*obj,double o) {
  float*a=obj->buf;
  double fr,s;
  int fi;
  if(obj->flag&RESAMPLE_SUBSAMPLE_INTERPOLATE) {
    fr=o-floor(o);
    fi=(int)floor(fr*=obj->nfil);
    a+=(int)floor(o)-obj->ntap/2+1;
    s=apply_filter(obj->filters[fi],a,obj->ntap)*(1.0-(fr-=fi));
    return s+apply_filter(obj->filters[fi+1],a,obj->ntap)*fr;
  } else {
    fi=(int)floor((o-floor(o))*obj->nfil+0.5);
    a+=(int)floor(o)-obj->ntap/2+1;
    if((obj->flag&RESAMPLE_LOWPASS) || (fi%obj->nfil)) return apply_filter(obj->filters[fi],a-obj->ntap/2+1,obj->ntap);
    return a[fi/obj->nfil];
  }
}

void resample_process_int16(Resample*obj,int16_t*in,uint32_t nin,int16_t*out,uint32_t nout,double ratio) {
  double o=0.0;
  uint32_t g=0;
  int h=obj->ntap/2;
  while(nout) {
    if(obj->offset+o>=obj->index-h) {
      if(!nin--) break;
      if(obj->index==obj->nsam) {
        memmove(obj->buf,obj->buf+obj->nsam-obj->ntap,obj->ntap*sizeof(float));
        obj->offset-=obj->nsam-obj->ntap;
        obj->index-=obj->nsam-obj->ntap;
      }
      obj->buf[obj->index++]=*in++;
      obj->pin++;
    } else {
      *out++=fmin(32767.0,fmax(-32767.0,subsample(obj,obj->offset+o)));
      o=++g/ratio;
      obj->pout++;
      nout--;
    }
  }
  obj->offset+=o;
}

void resample_process_int16_to_float_mix(Resample*obj,int16_t*in,uint32_t nin,float*out,uint32_t nout,float vol,double ratio) {
  double o=0.0;
  uint32_t g=0;
  int h=obj->ntap/2;
  while(nout) {
    if(obj->offset+o>=obj->index-h) {
      if(!nin--) break;
      if(obj->index==obj->nsam) {
        memmove(obj->buf,obj->buf+obj->nsam-obj->ntap,obj->ntap*sizeof(float));
        obj->offset-=obj->nsam-obj->ntap;
        obj->index-=obj->nsam-obj->ntap;
      }
      obj->buf[obj->index++]=*in++;
      obj->pin++;
    } else {
      *out++ +=vol*subsample(obj,obj->offset+o);
      o=++g/ratio;
      obj->pout++;
      nout--;
    }
  }
  obj->offset+=o;
}
