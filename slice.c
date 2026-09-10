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
  };
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
  void(*render)(Slice*,const SDL_Rect*clip,Sint16 x,Sint16 y);
  Sint32(*control)(Slice*,Uint8 op,Uint16 addr,Sint32 in);
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
  if(v->length>2 && t) *t=v->data[2];
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

static Slice*load_BOX(const ASN1_Value*v0) {
  ASN1_Value v1,v2;
  int n;
  Slice*s=calloc(1,sizeof(Slice));
  if(!s) err(1,"Allocation failed");
  if(asn1_first_of(&v1,v0) || v1.class || v1.type!=ASN1_ENUMERATED || v1.length!=1 || v1.data[0]>3) SliceError("Improper direction");
  s->box.dir=v1.data[0];
  if(asn1_next_of(&v1,v0) || load_z_and_color(&v1,&s->box.borderz,&s->box.border,0)) SliceError("Improper color/Z-index");
  if(asn1_next_of(&v1,v0) || load_z_and_color(&v1,&s->box.fillz,&s->box.fill,&s->box.translucent)) SliceError("Improper color/Z-index");
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_INTEGER || asn1_decode_number(&v1,ASN1_INTEGER,&s->box.mar)) SliceError("Improper number");
  if(asn1_next_of(&v1,v0)) SliceError("");
  if(v1.class==ASN1_UNIVERSAL && v1.type==ASN1_BOOLEAN && v1.length==1) {
    if(v1.data[0]) s->flag|=SLF_EXTRA2;
    if(asn1_next_of(&v1,v0)) SliceError("");
  }
  if(asn1_next_of(&v1,v0) || v1.class || v1.type!=ASN1_BOOLEAN || v1.length!=1) SliceError("");
  if(v1.data[0]) s->flag|=SLF_EXTRA1;
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
  asn1_construct(enc,ASN1_UNIVERSAL,ASN1_SEQUENCE,0);
    for(n=0;n<s->box.count;n++) save_one_slice(enc,s->box.slices[n]);
  asn1_end(enc);
}

static Sint32 xmeasure_BOX(Slice*s,Sint32 in) {
}

static Sint32 ymeasure_BOX(Slice*s,Sint32 in) {
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
  if(asn1_next_of(&v1,v0) || asn1_decode_number(&v1,ASN1_AUTO,&s->id)) SliceError("");
  if(asn1_next_of(&v1,v0) || asn1_decode_number(&v1,ASN1_AUTO,&s->width)) SliceError("");
  if(asn1_next_of(&v1,v0) || v1.length!=1) SliceError("");
  s->flag=v1.data[0];
  return s;
}

static const SliceType slicetype[NUMSLICETYPES]={
  [SLICE_IDNUMBER]={.kind=SLK_MODIFIER,.load=load_IDNUMBER},
  [SLICE_OFFSET]={.kind=SLK_NORMAL,.load=load_OFFSET,.save=save_OFFSET,.free=free_OFFSET,.xmeasure=xmeasure_OFFSET,.ymeasure=ymeasure_OFFSET},
  [SLICE_BOX]={.kind=SLK_NORMAL,.load=load_BOX,.save=save_BOX,.free=free_BOX,.xmeasure=xmeasure_BOX,.ymeasure=ymeasure_BOX},
  [SLICE_SPACE]={.kind=SLK_NORMAL,.load=load_SPACE,.save=save_SPACE},
  [SLICE_SAVESTATE]={.kind=SLK_MODIFIER,.load=load_SAVESTATE},
};

static Slice*load_one_slice(const ASN1_Value*v0) {
  const SliceType*t;
  Slice*s;
  if(v0->class!=ASN1_APPLICATION || !v0->constructed || v0->type>=NUMSLICETYPES) SliceError("Incorrect ASN.1 tag when trying to determine the slice type (found %d%c)",(int)v0->type,"UACP"[v0->class&3]);
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
    root[level]->height=480;
  }
}

void load_screen_slices(Uint8 level) {
  FILE*fp=open_lump_by_number(cur_screen_id,"EGS","r");
  if(!fp) return;
  load_slices(fp,level);
  fclose(fp);
}

void save_slices(FILE*fp,Uint8 level) {
  ASN1_Encoder*enc=asn1_create_encoder(fp);
  if(!enc) err(1,"Allocation failed");
  save_one_slice(enc,root[level]);
  asn1_finish_encoder(enc);
}

void render_slices(void) {
  // Only to be called by the redisplay function in display.c
  static const SDL_Rect r={.w=640,.h=350};
  int n;
  for(n=0;n<3;n++) {
    
  }
}

Sint32 control_slices(Uint16 id,Uint8 op,Uint16 addr,Sint32 level,Uint8 misc) {
  
}

static Sint32 xmeasure_slice(Slice*s,Sint32 in) {
  const SliceType*t;
  if((s->flag&SLF_MANUAL) || ((t=slicetype+s->type) && !t->xmeasure)) {
    return s->width;
  } else {
    return t->xmeasure(s,in);
  }
}

static Sint32 ymeasure_slice(Slice*s,Sint32 in) {
  const SliceType*t;
  if((s->flag&SLF_MANUAL) || ((t=slicetype+s->type) && !t->ymeasure)) {
    return s->height;
  } else {
    return t->ymeasure(s,in);
  }
}

