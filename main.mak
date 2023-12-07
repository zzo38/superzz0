main.c display.o edit.o editbrd.o editscr.o game.o lumped.o window.o world.o common.h config.inc -> $ : bash main.c
display.c common.h config.inc -> display.o : bash display.c
edit.c common.h config.inc -> edit.o : bash edit.c
editbrd.c common.h config.inc -> editbrd.o : bash editbrd.c
editscr.c common.h config.inc -> editscr.o : bash editscr.c
game.c opcodes.h common.h config.inc -> game.o : bash game.c
lumped.c common.h -> lumped.o : bash lumped.c
window.c common.h -> window.o : bash window.c
world.c common.h config.inc -> world.o : bash world.c
opcodes.doc -> opcodes.h : sed -rn 's/^\[(...) (.*)]$/#define OP_\2 0x\1/p' < opcodes.doc > opcodes.h
