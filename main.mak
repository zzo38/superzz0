main.c display.o edit.o game.o lumped.o world.o common.h config.inc -> $ : bash main.c
display.c common.h config.inc -> display.o : bash display.c
edit.c common.h config.inc -> edit.o : bash edit.c
game.c common.h config.inc -> game.o : bash game.c
lumped.c common.h -> lumped.o : bash lumped.c
world.c common.h config.inc -> world.o : bash world.c