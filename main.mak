!echo =CFLAGS=`cat cflags`
cflags common.h config.inc -> *c : *
main.c asn1.o audio.o display.o edit.o editbrd.o editscr.o edittext.o game.o lumped.o printer.o savegame.o slice.o special.o window.o world.o *c version.inc -> $ : bash main.c
asn1.c asn1.h -> asn1.o : bash asn1.c
audio.c asn1.h resample.o musemu/musemu.h *c -> audio.o : bash audio.c
display.c asn1.h *c -> display.o : bash display.c
edit.c *c asn1.h -> edit.o : bash edit.c
editbrd.c *c -> editbrd.o : bash editbrd.c
editscr.c *c -> editscr.o : bash editscr.c
edittext.c *c -> edittext.o : bash edittext.c
game.c asn1.h opcodes.h *c -> game.o : bash game.c
hash.c hash.h -> hash.o : bash hash.c
lumped.c *c -> lumped.o : bash lumped.c
printer.c *c -> printer.o : bash printer.c
resample.c resample.h -> resample.o : bash resample.c
savegame.c *c asn1.h -> savegame.o : bash savegame.c
slice.c *c asn1.h -> slice.o : bash slice.c
special.c *c asn1.h -> special.o : bash special.c
window.c *c -> window.o : bash window.c
world.c *c asn1.h -> world.o : bash world.c
opcodes.doc -> opcodes.h : sed -rn 's/^\[(...) (.*)]$/#define OP_\2 0x\1/p' < opcodes.doc > opcodes.h
version.c hash.o -> sz0version : bash version.c
sz0version asn1.c asn1.h audio.c display.c edit.c editbrd.c editscr.c edittext.c game.c lumped.c main.c main.mak opcodes.h opcodes.inc printer.c resample.c resample.h savegame.c slice.c window.c world.c *c -> version.inc version.status : ./sz0version update
