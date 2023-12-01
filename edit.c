#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 edit.c `sdl-config --cflags`
exit
#endif

#include "common.h"

int run_editor(int bn) {
  win_form("Test form") {
    win_heading("Dimensions");
    win_numeric('W',"Width:  ",board_info.width,1,16384);
    win_numeric('H',"Height: ",board_info.height,1,16384);
    win_heading("Flags");
    win_boolean('0',"User0",board_info.flag,BF_USER0);
    win_boolean('1',"User1",board_info.flag,BF_USER1);
    win_boolean('2',"User2",board_info.flag,BF_USER2);
    win_boolean('3',"User3",board_info.flag,BF_USER3);
    win_boolean('P',"Persist",board_info.flag,BF_PERSIST);
    win_boolean('G',"No Global",board_info.flag,BF_NO_GLOBAL);
    win_boolean('O',"Show Overlay",board_info.flag,BF_OVERLAY);
    win_heading("Commands");
    win_command('M',"More...") {
      win_form("More") {
        win_heading("Hello, World!");
        win_command('B',"Back") {
          break;
        }
      }
    }
    win_command('Q',"Quit") {
      break;
    }
  }
  //TODO
}
