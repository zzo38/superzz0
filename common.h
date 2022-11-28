#include "SDL.h"
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// === Game definitions ===

typedef struct {
  Uint8 name[16];
  Uint8 app[2];
  Uint32 attrib;
  Uint16 event[16];
} ElementDef;

// Index into event array; 8-15 are user-defined events
#define EV_FRAME 0
#define EV_STAT 1
#define EV_PUSH 2
#define EV_TRANSPORT 3

// attrib
#define A_CLASS1 0x00000001 // class number
#define A_CLASS2 0x00000002
#define A_CLASS4 0x00000004
#define A_CLASS8 0x00000008
#define A_PUSH_NS 0x00000010
#define A_PUSH_EW 0x00000020
#define A_OVER_COLOR 0x00000040 // colour of over layer; if also A_UNDER_COLOR then colour of screen
#define A_UNDER_COLOR 0x00000080 // colour of under layer
#define A_CRUSH 0x00000100
#define A_FLOOR 0x00000200
#define A_UNDER_BGCOLOR 0x00000400 // if colour<15 then use background colour of under layer
#define A_AUTO_STAT 0x00000800 // automatically add a stat by default when adding this element to the board
#define A_LIGHT 0x00001000 // suppress overlay
#define A_SPECIAL 0x00002000 // ???
#define A_TRANSPORT0 0x00004000
#define A_TRANSPORT1 0x00008000
#define A_MOVE_C0 0x00010000 // allow movement on class 0
#define A_MOVE_C1 0x00020000
#define A_MOVE_C2 0x00040000
#define A_MOVE_C3 0x00080000
#define A_MOVE_C4 0x00100000
#define A_MOVE_C5 0x00200000
#define A_MOVE_C6 0x00400000
#define A_MOVE_C7 0x00800000
#define A_MISC_A 0x01000000 // user-defined
#define A_MISC_B 0x02000000
#define A_MISC_C 0x04000000
#define A_MISC_D 0x08000000
#define A_MISC_E 0x10000000
#define A_MISC_F 0x20000000
#define A_MISC_G 0x40000000
#define A_MISC_H 0x80000000

// The high 2-bits of app[0] are the line joining class.
// The low 6-bits of app[0] is one of:
#define AP_FIXED 0x00 // Always use app[1]
#define AP_PARAM 0x01 // Tile param + app[1] (mod 256)
#define AP_OVER 0x02 // Character of over layer
#define AP_UNDER 0x03 // Character of under layer, or app[1] if the under layer is also AP_UNDER
#define AP_SCREEN 0x04 // Character of screen
#define AP_MISC1 0x05 // Stat misc1 (or app[1] if no stat)
#define AP_MISC2 0x06 // Stat misc2 (or app[1] if no stat)
#define AP_MISC3 0x07 // Stat misc3 (or app[1] if no stat)
#define AP_LINES_3 0x0C // Join to line class 3; app[1] is base offset of appearance mapping
#define AP_LINES_1_3 0x0D
#define AP_LINES_2_3 0x0E
#define AP_LINES_1_2_3 0x0F

// === Board/stats ===

typedef struct {
  Uint8 kind,color,param,stat;
} Tile;

typedef struct {
  Uint16 x,y;
  Sint16 instptr;
  Uint8 layer,delay;
  // Layer: low 2-bits (1=under, 2=main, 3=overlay), bit7=lock
} StatXY;

typedef struct {
  Uint16 misc1,misc2,misc3;
  Uint8*text;
  Uint16 length;
  Uint16 count;
  StatXY*xy;
  Uint8 speed,timer;
} Stat;

// === Screens ===

typedef struct {
  Uint8 command[80*25];
  Uint8 color[80*25];
  Uint8 parameter[80*25];
  Sint8 view_x,view_y;
} Screen;

// === File access (Hamster archives) ===

extern char*world_name;

// Any of these functions with int return type will be: -1 for I/O error, 1 for other errors, 0 if successful
FILE*open_lump(const char*name,const char*mode);
FILE*open_lump_by_number(Uint16 id,const char*ext,const char*mode);
void revert_lump(const char*name);
void revert_lump_by_number(Uint16 id,const char*ext);
int open_world(const char*name);
void close_world(void);
int save_world(const char*name); // set name to null to overwrite the current file (safely)
int save_game(FILE*fp);
int restore_game(FILE*fp);

