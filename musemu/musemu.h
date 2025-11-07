#include <stdint.h>

typedef struct MUSEMU {
  void*userdata;
  void*(*create)(void*userdata,double samrate,const uint8_t*parameters,size_t size);
  void(*destroy)(void*object);
  void(*reset)(void*object);
  void(*render)(void*object,size_t count,float*outl,float ampl,float*outr,float ampr);
  void(*poke)(void*object,uint16_t port,uint16_t data);
  uint16_t(*peek)(void*object,uint16_t port);
  void(*load)(void*object,uint32_t addr,const uint8_t*data,size_t size);
  void(*send)(void*object,const uint8_t*data,size_t size);
} MUSEMU;

typedef struct MUSEMUinf {
  const uint8_t*oid;
  size_t oidlen;
} MUSEMUinf;

typedef int(*MUSEMUcb)(void*cbarg,const MUSEMUinf*inf,const MUSEMU*impl);

#define MUSEMU_STEREO 0x0001
#define MUSEMU_DEBUG 0x0002
#define MUSEMU_ISO2022 0x0004

const char*musemu_main(uint16_t flag,const char*arg,MUSEMUcb callback,void*cbarg);
