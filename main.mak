main.c asn1.o audio.o display.o edit.o editbrd.o editscr.o edittext.o game.o lumped.o printer.o savegame.o window.o world.o common.h config.inc version.inc -> $ : bash main.c
asn1.c asn1.h -> asn1.o : bash asn1.c
audio.c common.h config.inc -> audio.o : bash audio.c
display.c common.h config.inc -> display.o : bash display.c
edit.c common.h config.inc -> edit.o : bash edit.c
editbrd.c common.h config.inc -> editbrd.o : bash editbrd.c
editscr.c common.h config.inc -> editscr.o : bash editscr.c
edittext.c common.h config.inc -> edittext.o : bash edittext.c
game.c opcodes.h common.h config.inc -> game.o : bash game.c
lumped.c common.h config.inc -> lumped.o : bash lumped.c
printer.c common.h config.inc -> printer.o : bash printer.c
window.c common.h config.inc -> window.o : bash window.c
world.c common.h config.inc asn1.h -> world.o : bash world.c
savegame.c common.h config.inc -> savegame.o : bash savegame.c
opcodes.doc -> opcodes.h : sed -rn 's/^\[(...) (.*)]$/#define OP_\2 0x\1/p' < opcodes.doc > opcodes.h
version.c -> sz0version : bash version.c
sz0version asn1.c asn1.h audio.c common.h config.inc display.c edit.c editbrd.c editscr.c edittext.c game.c lumped.c main.c main.mak opcodes.h opcodes.inc printer.c savegame.c window.c world.c -> version.inc version.status : ./sz0version update
