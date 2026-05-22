#if 0
gcc $CFLAGS -c -Wno-unused-result special.c `sdl-config --cflags`
exit
#endif

#include "common.h"

Uint16 nspecopt;
SpecialOption*specopt;

void special_option_menu(void) {
  FILE*fp;
  ASN1_Value root={};
  if((v_status[1] && v_status[1]!=32) || !nspecopt) return;
  v_status[1]=21;
  fp=open_lump("OPTION.DER","r");
  if(!fp) goto stop;
  
  
  stop:
  v_status[1]=0;
  asn1_free(&root);
  
}

const char*load_special_options(FILE*f) {
  ASN1_Value v={};
  if(asn1_read_item(f,&v,0)) return "ASN.1 error in OPTION.DER lump";
  asn1_free(&v);
  if(!nspecopt) {
    nspecopt=1;
    if(!(specopt=malloc(sizeof(SpecialOption)))) err(1,"Allocation failed");
    *specopt=(SpecialOption){.key=SPECI_VACANT};
  }
  return 0;
}
