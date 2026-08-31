#if 0
gcc $CFLAGS -c -Wno-unused-result slice.c `sdl-config --cflags`
exit
#endif

#include "common.h"

typedef struct Slice Slice;

struct Slice {
  Uint8 flag,type;
  SDL_Rect xy;
  union {
    struct {
      // SLICE_OFFSET
      Slice*slice;
      Sint16 x,y;
    } offset;
    struct {
      // SLICE_HBOX, SLICE_VBOX
      Slice**slices;
      Uint16 count;
      Sint8 rpad,cpad;
      Uint8 columns;
      Uint8 border,borderz,fill,fillz,translucent;
    } box;
    struct {
      // SLICE_SPACE
      Sint32 natural,stretch,shrink;
    } space;
  };
};

// Slice:type
#define SLICE_NOTHING 0
#define SLICE_OFFSET 1
#define SLICE_HBOX 2
#define SLICE_VBOX 3
#define SLICE_SPACE 4

// Slice:flag
#define SLF_REFLOW 0x02
#define SLF_NOCLIP 0x04
#define SLF_UNSEEN 0x08
#define SLF_IGNORE 0x10
#define SLF_MANUAL 0x20

typedef struct {
  Uint8 kind;
  Slice*(*load)(const ASN1_Value*);
  void(*save)(ASN1_Encoder*,const Slice*);
} SliceType;

// SliceType:kind
#define SLK_INVALID 0
#define SLK_NORMAL 1
#define SLK_ARRAY 2
#define SLK_MODIFIER 3

void unload_slices(Uint8 level) {
  
}

const char*load_slices(FILE*fp,Uint8 level) {
  
}

const char*load_screen_slices(Uint8 level) {
  FILE*fp=open_lump_by_number(cur_screen_id,"EGS","r");
  const char*e;
  unload_slices(level);
  if(!fp) return 0;
  e=load_slices(fp,level);
  fclose(fp);
  return e;
}

void save_slices(FILE*fp,Uint8 level) {
  
}
