#include "SDL.h"
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// === Miscellaneous ===

#define DIR_N 0
#define DIR_S 1
#define DIR_E 2
#define DIR_W 3

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

// ElementDef:attrib
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
#define AP_LINES 0x08 // Line joining; low nybble of app[1] is which line classes, high nybble is appearance_mapping offset, high bit=edge join

// If bit5 of app[0] is set then it has a different meaning.

extern Uint8 appearance_mapping[128];

// === Board/stats ===

typedef struct {
  Uint8 kind,color,param,stat;
} Tile;

typedef struct {
  Uint16 x,y;
  Sint16 instptr;
  Uint8 layer,delay;
  // Layer: low 2-bits (1=under, 2=main, 3=overlay), bit6=user, bit7=lock
} StatXY;

typedef struct {
  Uint16 misc1,misc2,misc3;
  Uint8*text;
  Uint16 length;
  Uint16 count;
  StatXY*xy;
  Uint8 speed,timer;
} Stat;

typedef struct {
  Uint16 width,height;
  Uint16 screen;
  Uint16 exits[4];
  Uint16 userdata;
  Uint16 flag;
} BoardInfo;

// BoardInfo:flag
#define BF_USER0 0x0001
#define BF_USER1 0x0002
#define BF_USER2 0x0004
#define BF_USER3 0x0008
#define BF_PERSIST 0x0010  // save board state before going to another board
#define BF_NO_GLOBAL 0x0020  // suspend execution of global scripts
#define BF_OVERLAY 0x0040  // display overlay

// === Screens ===

typedef struct {
  Uint8 code;
  Uint8 lead; // character code
  Uint8 mark; // character code
  Uint8 div;
} NumericFormat;

// NumericFormat:code
#define NF_DECIMAL 'd'
#define NF_HEX_UPPER 'X'
#define NF_HEX_LOWER 'x'
#define NF_OCTAL 'o'
#define NF_COMMA ','
#define NF_ROMAN 'R'
#define NF_LSD_MONEY 'L'
#define NF_METER 'm'
#define NF_METER_HALF 'h'
#define NF_METER_EXT 'M'
#define NF_METER_HALF_EXT 'H'
#define NF_BINARY 'b'
#define NF_BINARY_EXT 'B'
#define NF_SCIENTIFIC_GIGA '0'
#define NF_SCIENTIFIC_MEGA '1'
#define NF_SCIENTIFIC_KILO '2'
#define NF_SCIENTIFIC '3'
#define NF_SCIENTIFIC_MILLI '4'
#define NF_SCIENTIFIC_MICRO '5'
#define NF_SCIENTIFIC_NANO '6'
#define NF_CHARACTER 'c'
#define NF_APPEARANCE '?'
#define NF_NONZERO '!'
#define NF_BOARD_NAME 'n'
#define NF_BOARD_NAME_EXT 'N'

typedef struct {
  Uint8 command[80*25];
  Uint8 color[80*25];
  Uint8 parameter[80*25];
  Uint8 view_x,view_y;  // coordinates of centre of viewport
  Uint8 message_x,message_y;
  Uint8 message_l,message_r;
  Uint8 flag;
  Uint8 soft_edge[4];
  Uint8 hard_edge[4];
  Uint8 border[4];  // character codes for default borders; 0=none
} Screen;

// Screen:command (high nybble)
#define SC_BACKGROUND 0x00  // bit0=display character (parameter); bit1=display colour; clear bits mean show previous screen
#define SC_BOARD 0x10  // show board; use color/parameter if out of range
#define SC_NUMERIC 0x20  // command low nybble=variable index; parameter: low nybble=digit position, high nybble=format index
#define SC_NUMERIC_SPECIAL 0x30
#define SC_MEMORY 0x40
#define SC_INDICATOR 0x50
#define SC_TEXT 0x60
#define SC_ITEM 0x70
#define SC_BITS_0_LO 0x80
#define SC_BITS_0_HI 0x90
#define SC_BITS_1_LO 0xA0
#define SC_BITS_1_HI 0xB0
#define SC_BITS_2_LO 0xC0
#define SC_BITS_2_HI 0xD0
#define SC_BITS_3_LO 0xE0
#define SC_BITS_3_HI 0xF0

#define SC_SPEC_PLAYER_X 0x30
#define SC_SPEC_PLAYER_Y 0x31
#define SC_SPEC_CAMERA_X 0x32
#define SC_SPEC_CAMERA_Y 0x33
#define SC_SPEC_TEXT_SCROLL_PERCENT 0x34
#define SC_SPEC_TEXT_LINE_NUMBER 0x35
#define SC_SPEC_TEXT_LINE_COUNT 0x36
#define SC_SPEC_BOARD_NUMBER 0x37
#define SC_SPEC_EXIT_N 0x38
#define SC_SPEC_EXIT_S 0x39
#define SC_SPEC_EXIT_E 0x3A
#define SC_SPEC_EXIT_W 0x3B
#define SC_SPEC_WIDTH 0x3C
#define SC_SPEC_HEIGHT 0x3D
#define SC_SPEC_USERDATA 0x3E

#define SC_IND_CURSOR 0x51
#define SC_IND_SCROLL 0x52
#define SC_IND_EXIT_N 0x58
#define SC_IND_EXIT_S 0x59
#define SC_IND_EXIT_E 0x5A
#define SC_IND_EXIT_W 0x5B
#define SC_IND_USER0 0x5C
#define SC_IND_USER1 0x5D
#define SC_IND_USER2 0x5E
#define SC_IND_USER3 0x5F

// Screen:flag
#define SF_LEFT_ALIGN_MESSAGE 0x01
#define SF_NO_SCROLL 0x02

extern NumericFormat num_format[16];
extern Screen cur_screen;

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

