#if 0
gcc -s -O2 -c -Wno-unused-result world.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

#define N_FEATURES 1
static const Uint32 feature_avail[N_FEATURES]={0x00000000};
static Uint8 feature_bits[(N_FEATURES+7)/8];

const char*init_world(void) {
  // Returns 0 if OK, error message if error
  int i,j;
  Uint32 u;
  FILE*fp;
  // "FEATURE.REQ"
  if(fp=open_lump("FEATURE.REQ","r")) {
    while(u=read32(fp)) {
      for(i=0;i<N_FEATURES;i++) if(feature_avail[i]==u) break;
      if(i==N_FEATURES) return "Feature unavailable";
      feature_bits[i>>3]|=1<<(i&7);
    }
    fclose(fp);
  }
  // "FEATURE.OPT"
  if(fp=open_lump("FEATURE.OPT","r")) {
    while(u=read32(fp)) {
      for(i=0;i<N_FEATURES;i++) if(feature_avail[i]==u) break;
      if(i!=N_FEATURES) feature_bits[i>>3]|=1<<(i&7);
    }
    fclose(fp);
  }
  // "MEMORY"
  fp=open_lump("MEMORY","r");
  if(!fp) return "Cannot open MEMORY lump";
  u=lump_size>>1;
  if(u>0x10000) u=0x10000;
  free(memory);
  memory=calloc(0x10000,sizeof(Uint16));
  if(!memory) err(1,"Allocation failed");
  for(i=0;i<u;i++) memory[i]=read16(fp);
  fclose(fp);
  // "START"
  fp=open_lump("START","r");
  if(!fp) return "Cannot open START lump";
  u=read16(fp);
  if(u<1 || u>SUPER_ZZ_ZERO_VERSION) return "Wrong version";
  cur_screen.message_l=222;
  cur_board_id=read16(fp);
  read32(fp); // used later
  read32(fp); // used later
  for(i=0;i<16;i++) status_vars[i]=read32(fp);
  fclose(fp);
  // "NUMFORM"
  if(fp=open_lump("NUMFORM","r")) {
    for(i=0;i<16;i++) {
      num_format[i].code=read8(fp);
      num_format[i].lead=read8(fp);
      num_format[i].mark=read8(fp);
      num_format[i].div=read8(fp);
    }
    fclose(fp);
  } else {
    num_format[0].code=num_format[1].code='d';
    num_format[0].lead=' '; num_format[1].lead='0';
    num_format[0].div=num_format[1].div=1;
  }
  // "ELEMENT"
  memset(elem_def,0,sizeof(elem_def));
  fp=open_lump("ELEMENT","r");
  if(!fp) return "Cannot open ELEMENT lump";
  fread(appearance_mapping,1,128,fp);
  for(i=0;i<4;i++) {
    animation[i].mode=read8(fp);
    fread(animation[i].step,1,4,fp);
  }
  for(i=0;i<256;i++) {
    u=read8(fp);
    if(u&15) {
      fread(elem_def[i].name,1,u&15,fp);
      if(u&0x80) elem_def[i].app[0]=fgetc(fp);
      if(u&0x40) elem_def[i].app[1]=fgetc(fp);
      if(!(u&0xC0)) elem_def[i].app[0]=AP_PARAM;
      if(u&0x20) elem_def[i].attrib=read32(fp); else elem_def[i].attrib=i?elem_def[i-1].attrib:0;
      if(u&0x10) {
        u=read16(fp);
        for(j=0;j<16;j++) if(u&(1<<j)) elem_def[i].event[j]=read16(fp);
      }
    } else {
      i+=u>>4;
    }
  }
  fclose(fp);
  // "TEXT"
  free(vgtext),vgtext=0;
  free(gtext),gtext=0;
  if(fp=open_lump("TEXT","r")) {
    Uint8*p;
    vgtext=malloc(lump_size+1);
    if(!vgtext) err(1,"Allocation failed");
    fread(vgtext,1,lump_size,fp);
    vgtext[lump_size]=0;
    ngtext=1;
    for(i=0;i<lump_size;i++) if(!vgtext[i]) ++ngtext;
    gtext=malloc(ngtext*sizeof(Uint8*));
    if(!gtext) err(1,"Allocation failed");
    for(i=1,u=0,p=vgtext;i<ngtext;i++) {
      gtext[i]=p;
      p+=strlen(p)+1;
    }
    fclose(fp);
  } else {
    gtext=malloc(sizeof(Uint8*));
    if(!gtext) err(1,"Allocation failed");
    ngtext=1;
  }
  *gtext="";
  // done
  return 0;
}
