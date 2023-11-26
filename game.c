#if 0
gcc -s -O2 -c -Wno-unused-result game.c `sdl-config --cflags`
exit
#endif

#include "common.h"

ElementDef elem_def[256];
Uint8 appearance_mapping[128];
Animation animation[4];
Uint16 cur_board_id;
BoardInfo board_info;
Tile*b_under;
Tile*b_main;
Tile*b_over;
NumericFormat num_format[16];
Screen cur_screen;
Uint16 cur_screen_id;
Uint16*memory;
Uint8**gtext;
Uint8*vgtext;
Uint16 ngtext;
Sint32 status_vars[16];

int run_game(int bn) {
  //TODO
}

