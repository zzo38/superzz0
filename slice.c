#if 0
gcc $CFLAGS -c -Wno-unused-result slice.c `sdl-config --cflags`
exit
#endif

#include "common.h"

typedef struct Slice Slice;

struct Slice {
  Uint8 flag,type;
  Sint16 width,height;
  Uint16 id;
  union {
    struct {
      // SLICE_OFFSET
      Slice*slice;
      Sint16 x,y;
    } offset;
    struct {
      // SLICE_BOX
      Slice**slices;
      Uint16 count;
      Sint8 mar;
      Uint8 border,borderz,fill,fillz,translucent,dir;
    } box;
    struct {
      // SLICE_SPACE
      Sint16 natural,stretch,shrink;
    } space;
    struct {
      // SLICE_TEXT
      DrawText draw;
      Sint8 xpad,ypad;
      Uint8 major;
      Uint16 minor,len,maxlen;
    } text;
  };
  Uint8 data[0];
};

static Slice**byid;
static Uint16 maxid;
static Slice*root[3];

// Slice:type
enum {
  SLICE_IDNUMBER=0,
  SLICE_OFFSET=1,
  SLICE_BOX=2,
  SLICE_SPACE=3,
  SLICE_SAVESTATE=4,
  SLICE_MANUAL=5,
  SLICE_IGNORE=6,
  SLICE_UNSEEN=7,
  SLICE_NOCLIP=8,
  SLICE_METER=9,
  SLICE_TEXT=10,
  SLICE_PICTURE=11,
  SLICE_ORIGINPICTURE=12,
  SLICE_BOARDGRID=13,
  SLICE_MINIMAP=14,
  NUMSLICETYPES
};

// Slice:flag
#define SLF_NUMBER 0x01
#define SLF_NOCLIP 0x02
#define SLF_UNSEEN 0x04
#define SLF_IGNORE 0x08
#define SLF_MANUAL 0x10
#define SLF_EXTRA1 0x20
#define SLF_EXTRA2 0x40
#define SLF_EXTRA3 0x80

typedef struct {
  Uint8 kind;
  Slice*(*load)(const ASN1_Value*);
  void(*save)(ASN1_Encoder*,const Slice*);
  void(*free)(Slice*);
  Sint32(*xmeasure)(Slice*,Sint32);
  Sint32(*ymeasure)(Slice*,Sint32);
  void(*render)(Slice*,Sint16 x,Sint16 y,Sint16 w,Sint16 h,const SDL_Rect*clip);
  Sint32(*control)(Slice*,Uint8 op,Sint32 in);
} SliceType;

// SliceType:kind
#define SLK_INVALID 0
#define SLK_NORMAL 1
#define SLK_ARRAY 2
#define SLK_MODIFIER 3

// SliceType:box.dir
#define TopToBottom 0
#define BottomToTop 1
#define LeftToRight 2
#define RightToLeft 3

static const SliceType slicetype[NUMSLICETYPES];
static Slice*load_one_slice(const ASN1_Value*v0);
static void save_one_slice(ASN1_Encoder*enc,const Slice*s);
static void free_slice(Slice*s);
static Sint32 xmeasure_slice(Slice*s,Sint32 in);
static Sint32 ymeasure_slice(Slice*s,Sint32 in);
static void render_one_slice(Slice*s,Sint16 x,Sint16 y,Sint16 w,Sint16 h,const SDL_Rect*clip);

// This macro is used in slice loading functions if there is an error loading the slice
#define SliceError(...) do{ fprintf(stderr,"Slice loading error: " __VA_ARGS__); fputc('\n',stderr); return 0; }while(0)

static int load_z_and_color(const ASN1_Value*v,Uint8*z,Uint8*c,Uint8*t) {
  // z (Z index) is required; c (color) and t (translucent) are optional
  if(c) *c=0;
  if(t) *t=0;
  *z=0;
  if(v->class!=ASN1_APPLICATION || v->type!=0 || v->constructed) return ASN1_IMPROPER_TYPE;
  if(v->length>(t?3:c?2:1)) return ASN1_IMPROPER_VALUE;
  if(!c && !v->length) return ASN1_IMPROPER_VALUE;
  if(v->length) *z=v->data[0];
  if(v->length>1 && c) *c=v->data[1];
  if(v->length>2 && t && v->data[2]) *t=1;
  if(c && *c && *c<0x30) *c|=0x30;
  return ASN1_OK;
}

static void save_z_and_color(ASN1_Encoder*enc,const Uint8*z,const Uint8*c,const Uint8*t) {
  Uint8 h[3];
  if(c && z && !*c && !*z && (!t || !*t)) {
    asn1_primitive(enc,ASN1_APPLICATION,0,h,0);
  } else if(c && t && *t) {
    h[0]=*z; h[1]=*c; h[2]=0xFF;
    asn1_primitive(enc,ASN1_APPLICATION,0,h,3);
  } else if(c) {
    h[0]=*z; h[1]=*c;
    asn1_primitive(enc,ASN1_APPLICATION,0,h,2);
  } else {
    asn1_primitive(enc,ASN1_APPLICATION,0,z,1);
  }
}

static void assign_slice_id(Slice*s,Uint16 id) {
  if(id>0x7FFF) errx(1,"Improper slice ID");
  if((s->flag&SLF_NUMBER) && s->id<=maxid && byid) byid[s->id]=0;
  if(byid && id<=maxid && byid[id]) byid[id]->flag&=~SLF_NUMBER;
  s->flag|=SLF_NUMBER;
  s->id=id;
  if(maxid<id || !byid) {
    byid=realloc(byid,(id+1)*sizeof(Slice*));
    if(!byid) err(1,"Allocation failed");
    while(maxid<id) byid[maxid++]=0;
  }
  byid[id]=s;
}

static Slice*load_IDNUMBER(const ASN1_Value*v0) {
  ASN1_Value v1;
  Uint16 id;
  Slice*s;
  if(asn1_first_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&id)) return 0;
  if(asn1_next_of(&v1,v0)) return 0;
  if(s->flag&SLF_NUMBER) SliceError("Slice already has ID number");
  if(s=load_one_slice(&v1)) assign_slice_id(s,id);
  return s;
}

static Slice*load_OFFSET(const ASN1_Value*v0) {
  ASN1_Value v1;
  Sint16 x,y;
  Slice*u;
  Slice*s;
  if(asn1_first_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&x)) SliceError("Improper offset");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&y)) SliceError("Improper offset");
  if(asn1_next_of(&v1,v0)) SliceError("");
  u=load_one_slice(&v1);
  if(!u) SliceError("Error loading slice contained in OFFSET");
  s=calloc(1,sizeof(Slice));
  if(!s) err(1,"Allocation failed");
  s->offset.x=x;
  s->offset.y=y;
  s->offset.slice=u;
  return s;
}

static void save_OFFSET(ASN1_Encoder*enc,const Slice*s) {
  asn1_encode_integer(enc,s->offset.x);
  asn1_encode_integer(enc,s->offset.y);
  save_one_slice(enc,s->offset.slice);
}

static void free_OFFSET(Slice*s) {
  free_slice(s->offset.slice);
}

static Sint32 xmeasure_OFFSET(Slice*s,Sint32 in) {
  return xmeasure_slice(s->offset.slice,in);
}

static Sint32 ymeasure_OFFSET(Slice*s,Sint32 in) {
  return ymeasure_slice(s->offset.slice,in);
}

static void render_OFFSET(Slice*s,Sint16 x,Sint16 y,Sint16 w,Sint16 h,const SDL_Rect*clip) {
  render_one_slice(s->offset.slice,x+s->offset.x,y+s->offset.y,w,h,clip);
}

static Slice*load_BOX(const ASN1_Value*v0) {
  ASN1_Value v1,v2;
  int n;
  Slice*s=calloc(1,sizeof(Slice));
  if(!s) err(1,"Allocation failed");
  if(asn1_first_of(&v1,v0) || v1.class || v1.type!=ASN1_ENUMERATED || v1.length!=1 || v1.data[0]>3) SliceError("Improper direction");
  s->box.dir=v1.data[0];
  if(asn1_next_of(&v1,v0) || load_z_and_color(&v1,&s->box.borderz,&s->box.border,0)) SliceError("Improper color/Z-index");
  if(asn1_next_of(&v1,v0) || load_z_and_color(&v1,&s->box.fillz,&s->box.fill,&s->box.translucent)) SliceError("Improper color/Z-index");
  if(asn1_next_of(&v1,v0)) SliceError("");
  if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_BOOLEAN && v1.length==1) {
    if(v1.data[0]) s->flag|=SLF_EXTRA2;
    if(asn1_next_of(&v1,v0)) SliceError("");
  }
  if(v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->box.mar)) SliceError("Improper number");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_BOOLEAN || v1.length!=1) SliceError("");
  if(v1.data[0]) s->flag|=SLF_EXTRA1;
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_BOOLEAN || v1.length!=1) SliceError("");
  if(v1.data[0]) s->flag|=SLF_EXTRA3;
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_SEQUENCE || !v1.constructed) SliceError("");
  if(!v1.length) return s;
  s->box.count=asn1_count(&v1);
  if(!s->box.count) SliceError("");
  s->box.slices=calloc(s->box.count,sizeof(Slice*));
  if(!s->box.slices) err(1,"Allocation failed");
  for(n=0;n<s->box.count;n++) {
    if(n?asn1_next_of(&v2,&v1):asn1_first_of(&v2,&v1)) SliceError("Cannot find slice in BOX");
    if(!(s->box.slices[n]=load_one_slice(&v2))) SliceError("Error loading slice contained in BOX");
  }
  return s;
}

static void save_BOX(ASN1_Encoder*enc,const Slice*s) {
  int n;
  asn1_implicit(enc,ASN1_UNIVERSAL,ASN1_ENUMERATED);
  asn1_encode_integer(enc,s->box.dir);
  save_z_and_color(enc,&s->box.borderz,&s->box.border,0);
  save_z_and_color(enc,&s->box.fillz,&s->box.fill,&s->box.translucent);
  if(s->flag&SLF_EXTRA2) asn1_encode_boolean(enc,1);
  asn1_encode_integer(enc,s->box.mar);
  asn1_encode_boolean(enc,s->flag&SLF_EXTRA1);
  asn1_encode_boolean(enc,s->flag&SLF_EXTRA3);
  asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    for(n=0;n<s->box.count;n++) save_one_slice(enc,s->box.slices[n]);
  asn1_end(enc);
}

static Sint32 xmeasure_BOX(Slice*s,Sint32 in) {
  // This works like ymeasure_BOX but switch around the use of horizontal and vertical
  // Ensure any changes are also made the corresponding changes in the other function
  Slice*t;
  int n;
  if(s->box.dir<2) {
    // Vertical
    Sint32 x;
    Sint32 y=0;
    for(n=0;n<s->box.count;n++) {
      t=s->box.slices[n];
      if(!(t->flag&SLF_IGNORE)) {
        x=xmeasure_slice(t,in-2*s->box.mar);
        if(y<x) y=x;
      }
    }
    for(n=0;n<s->box.count;n++) if(!(s->box.slices[n]->flag&SLF_MANUAL)) s->box.slices[n]->width=y;
    return y+2*s->box.mar;
  } else {
    // Horizontal
    Sint32 x,y;
    Sint32 na=2*s->box.mar;
    Sint32 st=0;
    Sint32 sh=0;
    if(!s->box.count) return (s->flag&SLF_EXTRA3?in:0);
    if((s->flag&SLF_EXTRA2) && s->box.mar>0) na+=s->box.mar*(s->box.count-1);
    if(s->flag&SLF_EXTRA3) {
      for(n=x=y=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(!(t->flag&SLF_IGNORE)) {
          if(t->type==SLICE_SPACE) y+=t->space.natural; else if(t->flag&SLF_MANUAL) y+=t->width; else x++;
        }
      }
      y+=2*s->box.mar;
      if((s->flag&SLF_EXTRA2) && s->box.mar>0) y+=s->box.mar*(s->box.count-1);
      y=(in-y)/x;
    } else {
      y=in-2*s->box.mar;
    }
    for(n=0;n<s->box.count;n++) {
      t=s->box.slices[n];
      if(!(t->flag&SLF_IGNORE)) {
        if(t->type==SLICE_SPACE) {
          na+=t->space.natural;
          st+=t->space.stretch;
          sh+=t->space.shrink;
          if(!(t->flag&SLF_MANUAL)) t->width=t->space.natural;
        } else {
          x=xmeasure_slice(t,y);
          if(t->flag&SLF_MANUAL) x=t->width; else t->width=x;
          na+=x;
        }
      }
    }
    if(na<in && st>0) {
      for(n=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(t->type==SLICE_SPACE && t->space.stretch && !(t->flag&SLF_MANUAL)) t->width+=(t->space.stretch*(in-na))/st;
      }
    } else if(na>in && sh>0) {
      for(n=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(t->type==SLICE_SPACE && t->space.shrink && !(t->flag&SLF_MANUAL)) t->width-=(t->space.shrink*(na-in))/sh;
      }
    } else if(s->flag&SLF_EXTRA3) {
      for(n=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(t->type!=SLICE_SPACE && !(t->flag&(SLF_IGNORE|SLF_MANUAL))) t->width=y;
      }
    } else {
      return na;
    }
    return in;
  }
}

static Sint32 ymeasure_BOX(Slice*s,Sint32 in) {
  // This works like xmeasure_BOX but switch around the use of horizontal and vertical
  // Ensure any changes are also made the corresponding changes in the other function
  Slice*t;
  int n;
  if(s->box.dir<2) {
    // Vertical
    Sint32 x,y;
    Sint32 na=2*s->box.mar;
    Sint32 st=0;
    Sint32 sh=0;
    if(!s->box.count) return (s->flag&SLF_EXTRA3?in:0);
    if((s->flag&SLF_EXTRA2) && s->box.mar>0) na+=s->box.mar*(s->box.count-1);
    if(s->flag&SLF_EXTRA3) {
      for(n=x=y=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(!(t->flag&SLF_IGNORE)) {
          if(t->type==SLICE_SPACE) y+=t->space.natural; else if(t->flag&SLF_MANUAL) y+=t->height; else x++;
        }
      }
      y+=2*s->box.mar;
      if((s->flag&SLF_EXTRA2) && s->box.mar>0) y+=s->box.mar*(s->box.count-1);
      y=(in-y)/x;
    } else {
      y=in-2*s->box.mar;
    }
    for(n=0;n<s->box.count;n++) {
      t=s->box.slices[n];
      if(!(t->flag&SLF_IGNORE)) {
        if(t->type==SLICE_SPACE) {
          na+=t->space.natural;
          st+=t->space.stretch;
          sh+=t->space.shrink;
          if(!(t->flag&SLF_MANUAL)) t->height=t->space.natural;
        } else {
          x=ymeasure_slice(t,y);
          if(t->flag&SLF_MANUAL) x=t->height; else t->height=x;
          na+=x;
        }
      }
    }
    if(na<in && st>0) {
      for(n=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(t->type==SLICE_SPACE && t->space.stretch && !(t->flag&SLF_MANUAL)) t->height+=(t->space.stretch*(in-na))/st;
      }
    } else if(na>in && sh>0) {
      for(n=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(t->type==SLICE_SPACE && t->space.shrink && !(t->flag&SLF_MANUAL)) t->height-=(t->space.shrink*(na-in))/sh;
      }
    } else if(s->flag&SLF_EXTRA3) {
      for(n=0;n<s->box.count;n++) {
        t=s->box.slices[n];
        if(t->type!=SLICE_SPACE && !(t->flag&(SLF_IGNORE|SLF_MANUAL))) t->height=y;
      }
    } else {
      return na;
    }
    return in;
  } else {
    // Horizontal
    Sint32 x;
    Sint32 y=0;
    for(n=0;n<s->box.count;n++) {
      t=s->box.slices[n];
      if(!(t->flag&SLF_IGNORE)) {
        x=ymeasure_slice(t,in-2*s->box.mar);
        if(y<x) y=x;
      }
    }
    for(n=0;n<s->box.count;n++) if(!(s->box.slices[n]->flag&SLF_MANUAL)) s->box.slices[n]->height=y;
    return y+2*s->box.mar;
  }
}

static void render_BOX(Slice*s,Sint16 x,Sint16 y,Sint16 w,Sint16 h,const SDL_Rect*clip) {
  Slice*t;
  SDL_Rect r={.x=x,.y=y,.w=w,.h=h};
  int n;
  Sint16 m=(s->flag&SLF_EXTRA2?s->box.mar:0);
  if(s->box.border || s->box.fill) g_draw_box(clip,&r,s->box.border,s->box.borderz,s->box.fill,s->box.fillz,s->box.translucent);
  if(s->flag&SLF_EXTRA1) {
    if(g_clip(clip,&r,&r)) return;
  } else {
    r=*clip;
  }
  x+=(s->box.dir==RightToLeft?s->width+m-s->box.mar:s->box.mar);
  y+=(s->box.dir==BottomToTop?s->height+m-s->box.mar:s->box.mar);
  w-=2*s->box.mar; h-=2*s->box.mar;
  for(n=0;n<s->box.count;n++) {
    t=s->box.slices[n];
    switch(s->box.dir) {
      case TopToBottom:
        render_one_slice(t,x,y,w,t->height,&r);
        y+=t->height+m;
        break;
      case BottomToTop:
        y-=t->height+m;
        render_one_slice(t,x,y,w,t->height,&r);
        break;
      case LeftToRight:
        render_one_slice(t,x,y,t->width,h,&r);
        x+=t->width+m;
        break;
      case RightToLeft:
        x-=t->width+m;
        render_one_slice(t,x,y,t->width,h,&r);
        break;
    }
  }
}

static void free_BOX(Slice*s) {
  int i;
  for(i=0;i<s->box.count;i++) free_slice(s->box.slices[i]);
  free(s->box.slices);
}

static Slice*load_SPACE(const ASN1_Value*v0) {
  Slice*s;
  ASN1_Value v1;
  s=calloc(1,sizeof(Slice));
  if(!s) err(1,"Allocation failed");
  if(asn1_first_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->space.natural)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->space.stretch)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->space.shrink)) SliceError("");
  return s;
}

static void save_SPACE(ASN1_Encoder*enc,const Slice*s) {
  asn1_encode_integer(enc,s->space.natural);
  asn1_encode_integer(enc,s->space.stretch);
  asn1_encode_integer(enc,s->space.shrink);
}

static Slice*load_SAVESTATE(const ASN1_Value*v0) {
  ASN1_Value v1;
  Slice*s;
  if(asn1_first_of(&v1,v0)) SliceError("");
  s=load_one_slice(&v1);
  if(!s) return 0;
  if(s->flag&SLF_NUMBER) SliceError("Improper save state");
  if(asn1_next_of(&v1,v0) || asn1_decode_number(&v1,ASN1_AUTO,&s->id)) SliceError("");
  if(asn1_next_of(&v1,v0) || asn1_decode_number(&v1,ASN1_AUTO,&s->width)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.length!=1) SliceError("");
  if(v1.data[0]&SLF_NUMBER) assign_slice_id(s,s->id);
  s->flag=v1.data[0];
  return s;
}

static Slice*load_MANUAL(const ASN1_Value*v0) {
  Sint16 w,h;
  ASN1_Value v1;
  Slice*s;
  if(asn1_first_of(&v1,v0)) SliceError("Error loading MANUAL slice");
  if(v1.class!=ASN1_CONTEXT_SPECIFIC) {
    if(v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&w)) SliceError("Error loading MANUAL slice");
    if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&h)) SliceError("Error loading MANUAL slice");
    if(asn1_next_of(&v1,v0)) SliceError("Error loading MANUAL slice");
  }
  s=load_one_slice(&v1);
  if(!s) SliceError("Error loading MANUAL slice");
  s->flag|=SLF_MANUAL;
  s->width=w;
  s->height=h;
  return s;
}

static Slice*load_IGNORE(const ASN1_Value*v0) {
  ASN1_Value v1;
  Slice*s;
  if(asn1_first_of(&v1,v0) || !(s=load_one_slice(&v1))) SliceError("Error loading modifier slice");
  s->flag|=0x200>>v0->type;
  return s;
}

static Slice*load_TEXT(const ASN1_Value*v0) {
  ASN1_Value v1,v2,v3;
  Slice*s;
  Uint16 x;
  if(asn1_first_of(&v1,v0) || v1.class!=ASN1_CONTEXT_SPECIFIC || !v1.constructed) SliceError("");
  if(v1.type) {
    s=calloc(1,sizeof(Slice));
    if(!s) err(1,"Allocation failed");
    s->text.major=v1.type;
    switch(v1.type) {
      case 1:
        if(asn1_first_of(&v2,&v1) || v2.class || v2.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.minor)) SliceError("");
        break;
      case 2: case 4:
        // Nothing to do in this case
        break;
      case 3:
        if(asn1_first_of(&v2,&v1) || v2.class || v2.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.minor)) SliceError("");
        if(asn1_next_of(&v2,&v1) || v2.class || v2.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.len)) SliceError("");
        break;
      default: SliceError("");
    }
  } else {
    if(asn1_first_of(&v2,&v1) || v2.class || v2.type!=ASN1_PC_STRING || v2.length>32768) SliceError("");
    v3=v2;
    if(asn1_next_of(&v2,&v1)) {
      s=calloc(1,sizeof(Slice)+v3.length+1);
      if(!s) err(1,"Allocation failed");
      s->text.len=s->text.maxlen=v3.length;
    } else {
      if(v2.class || v2.type!=ASN1_INTEGER || asn1_decode_number(&v2,ASN1_INTEGER,&x) || x<v2.length || x>32768 || !x) SliceError("");
      s=calloc(1,sizeof(Slice)+x+1);
      if(!s) err(1,"Allocation failed");
      s->text.len=v3.length;
      s->text.maxlen=x;
    }
    memcpy(s->data,v3.data,v3.length);
  }
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.xpad)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.ypad)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.draw.tracking)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_ENUMERATED || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.draw.align) || s->text.draw.align>2) SliceError("");
  if(asn1_next_of(&v1,v0) || load_z_and_color(&v1,&s->text.draw.textz,&s->text.draw.text,0)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_ENUMERATED || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.draw.font) || s->text.draw.font>4) SliceError("");
  if(asn1_next_of(&v1,v0) || load_z_and_color(&v1,&s->text.draw.backz,&s->text.draw.back,0)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_ENUMERATED || asn1_decode_number(&v1,ASN1_INTEGER,&s->text.draw.style)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_BOOLEAN || v1.length!=1) SliceError("");
  if(v1.data[0]) s->flag|=SLF_EXTRA3;
  return s;
}

static void save_TEXT(ASN1_Encoder*enc,const Slice*s) {
  asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,s->text.major,0);
    switch(s->text.major) {
      case 0:
        asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_PC_STRING,s->data,s->text.len);
        if(s->text.len!=s->text.maxlen) asn1_encode_integer(enc,s->text.maxlen);
        break;
      case 1:
        asn1_encode_integer(enc,s->text.minor);
        break;
      case 3:
        asn1_encode_integer(enc,s->text.minor);
        asn1_encode_integer(enc,s->text.len);
        break;
    }
  asn1_end(enc);
  asn1_encode_integer(enc,s->text.xpad);
  asn1_encode_integer(enc,s->text.ypad);
  asn1_encode_integer(enc,s->text.draw.tracking);
  asn1_implicit(enc,ASN1_UNIVERSAL,ASN1_ENUMERATED); asn1_encode_integer(enc,s->text.draw.align);
  save_z_and_color(enc,&s->text.draw.textz,&s->text.draw.text,0);
  asn1_implicit(enc,ASN1_UNIVERSAL,ASN1_ENUMERATED); asn1_encode_integer(enc,s->text.draw.font);
  save_z_and_color(enc,&s->text.draw.backz,&s->text.draw.back,0);
  asn1_implicit(enc,ASN1_UNIVERSAL,ASN1_ENUMERATED); asn1_encode_integer(enc,s->text.draw.style);
  asn1_encode_boolean(enc,s->flag&SLF_EXTRA3);
}

static void decide_text(Slice*s,const Uint8**text,Uint16*len) {
  int i,j;
  switch(s->text.major) {
    case 0: *text=s->data; *len=s->text.len; break;
    case 1:
      if(s->text.minor<1 || s->text.minor>ndynastr) goto empty;
      *text=dynastr[s->text.minor-1].text; *len=dynastr[s->text.minor-1].len;
      break;
    case 2: *text=vtextbuf; *len=nvtextbuf; break;
    case 3:
      j=s->text.minor;
      *text=v_char+j;
      for(i=0;i<s->text.len && i+j<2000 && v_char[i+j] && !(v_font[i+j]&VF_SYSTEM);i++);
      *len=i;
      break;
    case 4:
      if(cur_board_id>maxboard) goto empty;
      *text=boardnames[cur_board_id];
      *len=strlen(*text);
      break;
    default: empty: *text=""; *len=0;
  }
}

static Sint32 xmeasure_TEXT(Slice*s,Sint32 in) {
  // TODO: Fix to handle multiline text (and automatic line breaking) later
  const Uint8*text;
  Uint16 len;
  decide_text(s,&text,&len);
  return s->flag&SLF_EXTRA3?in:(2*s->text.xpad+g_measure_text(&s->text.draw,text,len,0));
}

static Sint32 ymeasure_TEXT(Slice*s,Sint32 in) {
  // TODO: Fix to handle multiline text later
  return s->flag&SLF_EXTRA3?in:(2*s->text.ypad+g_measure_text(&s->text.draw,"",0,1));
}

static void render_TEXT(Slice*s,Sint16 x,Sint16 y,Sint16 w,Sint16 h,const SDL_Rect*clip) {
  SDL_Rect r={.x=x+s->text.xpad,.y=y+s->text.ypad,.w=w-2*s->text.xpad,.h=h-2*s->text.ypad};
  const Uint8*text;
  Uint16 len;
  decide_text(s,&text,&len);
  g_draw_text(clip,&r,&s->text.draw,text,len);
}

static Sint32 control_TEXT(Slice*s,Uint8 op,Sint32 in) {
  switch(op) {
    case 0x40:
      if(s->text.major==1 || s->text.major==3) return s->text.minor;
      break;
    case 0x50:
      if(s->text.major==1) s->text.minor=in;
      if(s->text.major==3 && in>=0 && in<2000) s->text.minor=in;
      break;
  }
  return in;
}

static const SliceType slicetype[NUMSLICETYPES]={
  [SLICE_IDNUMBER]={.kind=SLK_MODIFIER,.load=load_IDNUMBER},
  [SLICE_OFFSET]={.kind=SLK_NORMAL,.load=load_OFFSET,.save=save_OFFSET,.free=free_OFFSET,.xmeasure=xmeasure_OFFSET,.ymeasure=ymeasure_OFFSET,.render=render_OFFSET},
  [SLICE_BOX]={.kind=SLK_NORMAL,.load=load_BOX,.save=save_BOX,.free=free_BOX,.xmeasure=xmeasure_BOX,.ymeasure=ymeasure_BOX,.render=render_BOX},
  [SLICE_SPACE]={.kind=SLK_NORMAL,.load=load_SPACE,.save=save_SPACE},
  [SLICE_SAVESTATE]={.kind=SLK_MODIFIER,.load=load_SAVESTATE},
  [SLICE_MANUAL]={.kind=SLK_MODIFIER,.load=load_MANUAL},
  [SLICE_IGNORE]={.kind=SLK_MODIFIER,.load=load_IGNORE},
  [SLICE_UNSEEN]={.kind=SLK_MODIFIER,.load=load_IGNORE},
  [SLICE_NOCLIP]={.kind=SLK_MODIFIER,.load=load_IGNORE},
  [SLICE_TEXT]={.kind=SLK_NORMAL,.load=load_TEXT,.save=save_TEXT,.xmeasure=xmeasure_TEXT,.ymeasure=ymeasure_TEXT,.render=render_TEXT,.control=control_TEXT},
};

static Slice*load_one_slice(const ASN1_Value*v0) {
  const SliceType*t;
  Slice*s;
  if(v0->class!=ASN1_CONTEXT_SPECIFIC || !v0->constructed || v0->type>=NUMSLICETYPES) SliceError("Incorrect ASN.1 tag when trying to determine the slice type (found %d%c)",(int)v0->type,"UACP"[v0->class&3]);
  t=slicetype+v0->type;
  if(t->kind==SLK_INVALID || !t->load) SliceError("Trying to load a slice of an unloadable type");
  s=t->load(v0);
  if(!s) SliceError("Error loading slice of type %d",v0->type);
  if(t->kind!=SLK_MODIFIER) s->type=v0->type;
  return s;
}

static void save_one_slice(ASN1_Encoder*enc,const Slice*s) {
  if(!s) {
    asn1_encode_null(enc);
    return;
  }
  asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,SLICE_SAVESTATE,0);
    asn1_construct(enc,ASN1_CONTEXT_SPECIFIC,s->type,0);
      slicetype[s->type].save(enc,s);
    asn1_end(enc);
    asn1_encode_integer(enc,s->id);
    asn1_encode_integer(enc,s->width);
    asn1_encode_integer(enc,s->height);
    asn1_primitive(enc,ASN1_UNIVERSAL,ASN1_OCTET_STRING,&s->flag,1);
  asn1_end(enc);
}

static void free_slice(Slice*s) {
  if(!s) return;
  if(root[0]==s) root[0]=0;
  if(root[1]==s) root[1]=0;
  if(root[2]==s) root[2]=0;
  if(s->flag&SLF_NUMBER) byid[s->id]=0;
  if(slicetype[s->type].free) slicetype[s->type].free(s);
  free(s);
}

void unload_slices(Uint8 level) {
  if(level<3) free_slice(root[level]);
  //TODO: restore ID numbers from previous level if applicable
}

void load_slices(FILE*fp,Uint8 level) {
  ASN1_Value v={};
  unload_slices(level);
  if(!fp) return;
  if(asn1_read_item(fp,&v,0)) errx(1,"Error loading slices at level %d",level);
  root[level]=load_one_slice(&v);
  asn1_free(&v);
  if(!root[level]) errx(1,"Error loading slices at level %d",level);
  if((root[level]->flag&SLF_NUMBER) && root[level]->id) errx(1,"Root slice has incorrect ID number (expected 0; found %d)",root[level]->id);
  assign_slice_id(root[level],0);
  if(!root[level]->width && !root[level]->height) {
    root[level]->width=640;
    root[level]->height=350;
  }
}

void load_screen_slices(Uint8 level) {
  FILE*fp=open_lump_by_number(cur_screen_id,"EGS","r");
  if(!fp) return;
  load_slices(fp,level);
  fclose(fp);
  control_slices(0,0x02,0,0);
}

void save_slices(FILE*fp,Uint8 level) {
  ASN1_Encoder*enc=asn1_create_encoder(fp);
  if(!enc) err(1,"Allocation failed");
  save_one_slice(enc,root[level]);
  asn1_finish_encoder(enc);
}

static const SDL_Rect fullrect={.w=640,.h=350};

static void render_one_slice(Slice*s,Sint16 x,Sint16 y,Sint16 w,Sint16 h,const SDL_Rect*clip) {
  const SliceType*t;
  if(!(s->flag&SLF_UNSEEN)) {
    t=slicetype+s->type;
    if(t->render) t->render(s,x,y,s->flag&SLF_MANUAL?s->width:w,s->flag&SLF_MANUAL?s->height:h,s->flag&SLF_NOCLIP?&fullrect:clip);
  }
}

void render_slices(void) {
  // Only to be called by the redisplay function in display.c
  int n;
  for(n=0;n<3;n++) if(root[n]) render_one_slice(root[n],0,0,640,350,&fullrect);
}

static Uint8 testlevel=0;

static Sint32 xmeasure_slice(Slice*s,Sint32 in) {
  const SliceType*t=slicetype+s->type;
  if(config.test_mode&1) {
    Sint32 x;
    printf("%*sT%d ",testlevel,"",s->type);
    if(s->flag&SLF_NUMBER) printf("#%d ",s->id);
    printf("xmeasure_slice(%p,%ld)\n",s,(long)in);
    ++testlevel;
    x=t->xmeasure?t->xmeasure(s,s->flag&SLF_MANUAL?s->width:in):s->width;
    --testlevel;
    printf("%*s= %ld\n",testlevel,"",(long)x);
    return x;
  }
  return t->xmeasure?t->xmeasure(s,s->flag&SLF_MANUAL?s->width:in):s->width;
}

static Sint32 ymeasure_slice(Slice*s,Sint32 in) {
  const SliceType*t=slicetype+s->type;
  if(config.test_mode&1) {
    Sint32 x;
    printf("%*sT%d ",testlevel,"",s->type);
    if(s->flag&SLF_NUMBER) printf("#%d ",s->id);
    printf("ymeasure_slice(%p,%ld)\n",s,(long)in);
    ++testlevel;
    x=t->ymeasure?t->ymeasure(s,s->flag&SLF_MANUAL?s->height:in):s->height;
    --testlevel;
    printf("%*s= %ld\n",testlevel,"",(long)x);
    return x;
  }
  return t->ymeasure?t->ymeasure(s,s->flag&SLF_MANUAL?s->height:in):s->height;
}

Sint32 control_slices(Uint16 id,Uint8 op,Sint32 value,Uint8 misc) {
  Slice*s;
  if(id>maxid || !byid[id]) return value;
  s=byid[id];
  switch(op) {
    case 0x00: case 0x01:
      if(!(s->flag&SLF_NUMBER)) assign_slice_id(s,1);
      memory[MEM_PRIMARY_SLICE]=s->id;
      break;
    case 0x02:
      xmeasure_slice(s,s->width);
      ymeasure_slice(s,s->height);
      break;
    case 0x03: code0x03:
      s->width=xmeasure_slice(s,s->width);
      s->height=ymeasure_slice(s,s->height);
      break;
    case 0x04:
      if(s->flag&SLF_MANUAL) {
        s->flag&=~SLF_MANUAL;
        xmeasure_slice(s,s->width);
        ymeasure_slice(s,s->height);
        s->flag|=SLF_MANUAL;
      } else {
        goto code0x03;
      }
      break;
    default:
      if(slicetype[s->type].control) value=slicetype[s->type].control(s,op,value);
  }
  return value;
}

