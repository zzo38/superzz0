#if 0
gcc $CFLAGS -c -Wno-unused-result slice.c `sdl-config --cflags`
exit
#endif

#include "common.h"

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
