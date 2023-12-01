#undef _GNU_SOURCE
#define _GNU_SOURCE
#include "SDL.h"
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// === Configuration ===

#define B(n,t,d) t n;
#define F(n,t,d) t n;
#define I(n,t,d) t n;
#define S(n,t,d) t n;
typedef struct {
#include "config.inc"
} Config;
#undef B
#undef F
#undef I
#undef S
extern Config config;

// === Display ===

extern Uint8 v_color[80*25];
extern Uint8 v_char[80*25];
extern Uint8 v_status[82];
extern SDL_Event event;

void init_display(void);
void redisplay(void);
void display_title(const char*);
void set_timer(Uint32);
Uint8 draw_text(Uint8 x,Uint8 y,const char*t,Uint8 c,int n);
int next_event(void);

// === Miscellaneous ===

#define SUPER_ZZ_ZERO_VERSION 1

#define DIR_E 0
#define DIR_N 1
#define DIR_W 2
#define DIR_S 3

extern Uint8 editor;

const char*init_world(void);

// === Game definitions ===

typedef struct {
  Uint8 name[16];
  Uint8 app[2];
  Uint32 attrib;
  Uint16 event[16];
} ElementDef;

typedef struct {
  Uint8 step[4];
  Uint8 mode;
  // Maybe this will be changed to something else entirely?
} Animation;

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
#define A_TRANSPORTER 0x00004000
#define A_TRANSPORTABLE 0x00008000
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
#define AP_ANIMATE 0x09 // Animation without using tile parameter

// If bit5 of app[0] is set then it has a different meaning.
//  app[0] bit2-bit0 = Shift amount of tile parameter
//  app[0] bit4-bit3 = How many bits (1-4) of tile parameter to use
//  app[1] bit0      = Animation select (ignored unless bit7 is set)
//  app[1] bit6-bit1 = Offset of appearance_mapping
//  app[1] bit7      = Animation enable

// AP_ANIMATE:
//  app[1] bit1-bit0 = Animation select
//  app[1] bit6-bit2 = Offset of appearance_mapping
//  app[1] bit7      = Clear for space only (ignore time)

// Animation:mode
#define AM_X1 0x01
#define AM_X2 0x02
#define AM_Y1 0x04
#define AM_Y2 0x08
#define AM_SLOW 0x80

extern ElementDef elem_def[256];
extern Uint8 appearance_mapping[128];
extern Animation animation[4];

// === Board/stats ===

typedef union {
  struct {
    Uint8 kind,color,param,stat;
  };
  Uint8 values[4];
} Tile;

typedef struct {
  Uint16 x,y;
  Uint16 instptr; // if 65535 then stop
  Uint8 layer,delay;
  // Layer: low 2-bits (1=under, 2=main, 3=overlay), bit6=user, bit7=lock
} StatXY;

typedef struct {
  Uint16 misc1,misc2,misc3;
  Uint8*text;
  Uint16 length;
  Uint16 count;
  StatXY*xy;
  Uint8 speed;
} Stat;

typedef struct {
  Uint16 width,height;
  Uint16 screen;
  Uint16 exits[4];
  //Uint16 music; // 0=quiet, 65535=continue
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

// Overlay kind bits (the low nybble has user-defined meanings)
#define OVER_SOLID 0x10  // affects movement of stats in overlay
#define OVER_RESERVED 0x20  // reserved for future use (possibly wide characters)
#define OVER_BG_THRU 0x40  // show through background colour
#define OVER_VISIBLE 0x80  // overlay is visible (if not set, it is transparent)

extern Uint16 cur_board_id;
extern BoardInfo board_info;
extern Tile*b_under;
extern Tile*b_main;
extern Tile*b_over;
extern Stat*stats;
extern Uint8 maxstat;

StatXY*add_statxy(int n);

const char*load_board(FILE*fp);
const char*save_board(FILE*fp,int m);

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
  Uint8 border_color;  // 0=same colour
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
#define SC_BITS_0_LO 0x80  // low 16-bits of global variable 0; parameter character if set, space if clear
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
#define SC_SPEC_CURRENT_BOARD 0x37
#define SC_SPEC_EXIT_E 0x38
#define SC_SPEC_EXIT_N 0x39
#define SC_SPEC_EXIT_W 0x3A
#define SC_SPEC_EXIT_S 0x3B
#define SC_SPEC_WIDTH 0x3C
#define SC_SPEC_HEIGHT 0x3D
#define SC_SPEC_USERDATA 0x3E

#define SC_IND_CURSOR 0x51
#define SC_IND_SCROLL 0x52
#define SC_IND_EXIT_E 0x58
#define SC_IND_EXIT_N 0x59
#define SC_IND_EXIT_W 0x5A
#define SC_IND_EXIT_S 0x5B
#define SC_IND_USER0 0x5C
#define SC_IND_USER1 0x5D
#define SC_IND_USER2 0x5E
#define SC_IND_USER3 0x5F

// Screen:flag
#define SF_LEFT_ALIGN_MESSAGE 0x01
#define SF_FLASHY_MESSAGE 0x02
#define SF_EXIT_BORDER 0x04
#define SF_USER_BORDER 0x08

extern NumericFormat num_format[16];
extern Screen cur_screen;
extern Uint16 cur_screen_id;
extern Sint32 scroll_x,scroll_y;

const char*load_screen(FILE*fp);

// === Program memory / instructions ===

// Instruction format:
//   bit15-bit12 = Input operand
//   bit11-bit9 = Output operand or extra operand
//   bit8-bit0 = Opcode
// Output operand:
//   0-7 = Registers A-H (if opcode bit8 and bit7 are set, then S-Z instead)
// Input operand:
//   0-1 = Short immediate
//   2 = Immediate (unsigned 16-bits)
//   3 = Extension
//   4-7 = Registers W-Z
//   8-15 = Registers A-H
// Extension format:
//   bit15-bit12 = Source
//   bit11-bit8 = Type
//   bit7-bit0 = Value
// Extension source:
//   0-1 = Short immediate
//   2 = Immediate 32-bits (PDP-endian)
//   3 = Absolute 16-bits
//   4-7 = Registers W-Z
//   8-15 = Registers A-H
// Special outputs:
//   S = Return value from subroutine
//   T = Set condition flag to true if nonzero, or false if zero
//   U = (reserved)
//   V = (reserved)

#define XOP_ADD 0x0000 // source+value
#define XOP_ADD_NEG 0x0100 // source+value-256
#define XOP_ADD_INDIRECT 0x0200 // [source+value]
#define XOP_ADD_NEG_INDIRECT 0x0300 // [source+value-256]
#define XOP_LEFT_SHIFT 0x0400 // source<<value
#define XOP_SIGNED_RIGHT_SHIFT 0x0500 // source>>value
#define XOP_UNSIGNED_RIGHT_SHIFT 0x0600 // source>>value
#define XOP_EXTRACT_BITS 0x0700 // (source>>(value&15))&~((-1)<<((value>>4)&15))
#define XOP_SUBTRACT 0x0800 // value-source
#define XOP_SUBTRACT_NEG 0x0900 // value-source-256
#define XOP_XDIR 0x0A00 // bit7-bit4=direction register, bit3=reverse, bit2-bit0=shift amount of direction register
#define XOP_YDIR 0x0B00 // source=the original X or Y coordinate (OK to go out of range, in this case)
#define XOP_RANDOM 0x0C00 // random 0 to source+value-1
#define XOP_SPECIAL 0x0D00 // one of XOP_S_ based on bit7-bit4 of value

#define XOP_S_EVENT 0x0D10 // event (value&15) of element (source)
#define XOP_S_STATUS_PLUS 0x0D20 // status variable (value&15), plus (source)
#define XOP_S_STATUS_MINUS 0x0D30 // status variable (value&15), minus (source)
#define XOP_S_WIDTH 0x0D40 // board width plus (source+(value&15)-8)
#define XOP_S_HEIGHT 0x0D50 // board height plus (source+(value&15)-8)
#define XOP_S_PLAYER_X 0x0D60 // X coordinate of first XY record of stat 1, plus ((value&15)-8-source)
#define XOP_S_PLAYER_Y 0x0D70 // Y coordinate of first XY record of stat 1, plus ((value&15)-8-source)
#define XOP_S_BOARD_ID 0xD80 // current board number
#define XOP_S_SCREEN_ID 0xD90 // current screen number

extern Uint16*memory;
extern Sint32 regs[8];
extern Uint8 condflag;

extern Uint8**gtext;
extern Uint8*vgtext;
extern Uint16 ngtext;

// === Game state ===

extern Sint32 status_vars[16];

// === File access (Hamster archives) ===

extern char*world_name;
extern Uint32 lump_size;

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

// === File access (data) ===

#ifdef USING_RW_DATA

static inline Uint8 read8(FILE*fp) {
  int c=fgetc(fp);
  return c==EOF?0:c;
}

static inline Uint16 read16(FILE*fp) {
  int c=fgetc(fp);
  if(c==EOF) return 0;
  return c|(fgetc(fp)<<8);
}

static inline Uint32 read32(FILE*fp) {
  Uint32 r=0;
  int c=fgetc(fp);
  if(c==EOF) return 0;
  r=c|(fgetc(fp)<<8);
  r|=fgetc(fp)<<16;
  r|=fgetc(fp)<<24;
  return r;
}

static inline void write8(FILE*fp,Uint8 v) {
  fputc(v,fp);
}

static inline void write16(FILE*fp,Uint16 v) {
  fputc(v,fp);
  fputc(v>>8,fp);
}

static inline void write32(FILE*fp,Uint32 v) {
  fputc(v,fp);
  fputc(v>>8,fp);
  fputc(v>>16,fp);
  fputc(v>>24,fp);
}

#endif

// === Window ===

typedef struct {
  Uint8 line,cur,state,ncur;
} win_memo;

#define win_form(xxx) for(win_memo win_mem=win_begin_();;win_step_(&win_mem,xxx))
#define win_refresh() win_begin_()
win_memo win_begin_(void);
void win_step_(win_memo*,const char*);

#define win_numeric(aaa,bbb,ccc,ddd,eee) if(win_numeric_(&win_mem,aaa,bbb,&(ccc),sizeof(ccc),ddd,eee))
int win_numeric_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 lo,Uint32 hi);

#define win_boolean(aaa,bbb,ccc,ddd) if(win_boolean_(&win_mem,aaa,bbb,&(ccc),sizeof(ccc),ddd))
int win_boolean_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 b);

#define win_command(aaa,bbb) if(win_command_(&win_mem,aaa,bbb))
int win_command_(win_memo*wm,Uint8 key,const char*label);

#define win_blank() win_heading_(&win_mem,0)
#define win_heading(aaa) win_heading_(&win_mem,aaa)
void win_heading_(win_memo*wm,const char*label);

