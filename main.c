#if 0
gcc -s -O2 -o ~/bin/superzz0 main.c display.o edit.o game.o lumped.o world.o `sdl-config --cflags --libs`
exit
#endif

#include "common.h"

Config config={
#define B(n,t,d) d,
#define F(n,t,d) d,
#define I(n,t,d) d,
#define S(n,t,d) d,
#include "config.inc"
#undef B
#undef F
#undef I
#undef S
};
Uint8 editor=0;

int main(int argc,char**argv) {
  int b=-1;
  int i;
  while((i=getopt(argc,argv,"+b:e"))>0) switch(i) {
    case 'b': b=strtol(optarg,0,10); break;
    case 'e': editor=1; break;
    default: errx(1,"Wrong switches");
  }
  if(optind>=argc) errx(1,"Too few arguments");
  if(open_world(argv[optind])) err(1,"Error opening world");
  if(init_world()) errx(1,"Cannot initialize world settings");
  init_display();
//  if(editor) run_editor(b); else run_game(b);
  return 0;
}

