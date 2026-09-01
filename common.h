#undef _GNU_SOURCE
#define _GNU_SOURCE
#include "SDL.h"
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "asn1.h"

// === Version ===

extern const Uint8 version_hash[32];
extern const Uint8 version_rel_oid[];
extern const Uint8 version_rel_oid_length;
extern const Uint8 version_rel_oid_length_2;
extern const Uint8 version_is_release;

// === Configuration ===

#define B(n,t,d) t n;
#define F(n,t,d) t n;
#define I(n,t,d) t n;
#define P(n,t,d) t n;
#define S(n,t,d) t n;
typedef struct {
#include "config.inc"
} Config;
#undef B
#undef F
#undef I
#undef P
#undef S
extern Config config;

// === Display ===

extern Uint8 v_color[80*25];
extern Uint8 v_char[80*25];
extern Uint8 v_font[80*25];
extern Uint8 sv_color[80*25];
extern Uint8 sv_char[80*25];
extern Uint8 sv_font[80*25];
extern Uint8 v_status[82];
extern Uint8 v_xcur,v_ycur,v_mode,v_colormask;
extern SDL_Event event;
extern Uint8 repeating;

#define VIDEO_80COLUMNS 0x01
#define VIDEO_MONO 0x04
#define VIDEO_FLASHY 0x20
#define VIDEO_EGS 0x40
#define VIDEO_SMZX 0x80

#define VF_SYSTEM 0x01
#define VF_FRONT 0x02
#define VF_WIDE_L 0x04
#define VF_WIDE_R 0x08
#define VF_ALTERNATE 0x10

void init_display(void);
void redisplay(void);
void display_title(const char*);
void set_timer(Uint32);
Uint8 draw_text(Uint8 x,Uint8 y,const char*t,Uint8 c,int n);
int next_event(void);
void stop_key_repeat(void);

// === Fonts/palettes ===

extern const Uint8 pcfont[3584];
extern Uint8*font;

int load_font(const char*name,Uint8 z);
int load_palette(const char*name,Uint8 z);
void set_palette_vga(Uint8 k,Uint8 r,Uint8 g,Uint8 b);
void set_palette_vga_multi(Uint8 k,Uint8 n,const Uint8*r,const Uint8*g,const Uint8*b);

#define LOADFONT_BASE 0
#define LOADFONT_WIDE 1
#define LOADFONT_EGS_BASE 2
#define LOADFONT_EGS_WIDE 3
#define LOADFONT_RESET 15
#define LOADFONT_MASK 15
#define LOADFONT_CHARMAP 16

#define LOADPAL_BASE 0
#define LOADPAL_RESET 255

void load_fontpal_state(const ASN1_Value*);
void save_fontpal_state(ASN1_Encoder*);

#define FONTANIM_SHIFT 0
#define FONTANIM_CYCLE 1
#define FONTANIM_FULL 2

void configure_colors(const char*text);

// === Graphics ===

extern Uint8*backdrop_p;
extern Uint8*backdrop_z;
extern Uint8 backdrop_name[9];

const char*set_backdrop(const char*name,char usepal);

// === Joystick ===

#define JL_NORMAL 16
#define JL_TEXT_WINDOW 17
#define JL_ITEM_WINDOW 18
#define JL_CUSTOM_WINDOW 19
#define JL_LEVEL0 16
#define JL_LEVEL1 20
#define JL_LEVEL2 21
#define JL_LEVEL3 22
#define JL_LEVEL4 23

typedef struct {
  Uint16 a[24];
} JoyMapping;

typedef struct {
  Uint8*hat; // e,n,w,s
  Uint8*axis; // -,+
  Uint8*button; // *
  JoyMapping map[32];
  // Shift bits "0b1XXXXYYYYY" (X=shift state, Y=which button shifted it)
  Uint32 state,user_shift,world_shift;
  Uint8 nhat,nbutton,naxis,nmap;
} JoyStatus;

extern JoyStatus*joystat;

Sint32 configure_joystick(char mode,const char*text);

// === Sounds ===

void audio_init(void);
void audio_set_volume(Uint16 vol,Uint8 mut);
Sint32 audio_get_volume(void); // volume + (0x000000=on, 0x010000=mute, 0xFF0000=disabled)
void audio_set_sfx(const char*m);
void audio_set_music(const char*name,Uint16 song); // name=0 to cancel music, name="" for same lump
void audio_load_emulator(const char*name,const char*arg);
int world_configure_audio(const ASN1_Value*v);

extern char music_name[9];
extern Uint16 music_song;

void convert_sound_file(FILE*in,FILE*out,Uint8 k,Uint16 s,Uint8 u,Uint8 w);

// === Miscellaneous ===

#define DIR_E 0
#define DIR_N 1
#define DIR_W 2
#define DIR_S 3

const char*init_world(void);
int run_game(void);
void run_test_game(int);

Uint32 dice(Uint32 n);
Uint32 reseed(uint64_t n);

extern FILE*extern_in;
extern ASN1_Encoder*extern_out;
void unlock_front(void);

// === Editor ===

typedef struct VarPropertyList VarPropertyList;

extern Uint8 editor;

int run_editor(void);
Uint16 edit_board(Uint16);
Uint16 edit_screen(Uint16);
void edit_varprop(VarPropertyList*vp);
void combine_assembled(void);
void set_board_name(Uint16 id,const char*name);
void set_screen_name(Uint16 id,const char*name);
void write_name_list(const char*lump,Uint8**data,Uint16 count);
Uint8*text_editor(Uint8*t);

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
} Animation;

// Index into event array; 8-15 are user-defined events
#define EV_FRAME 0
#define EV_STAT 1
#define EV_PUSH 2
#define EV_TRANSPORT 3
#define EV_SENSOR 4

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
#define A_PERMANENT 0x00000800 // many things are unable to overwrite it
#define A_LIGHT 0x00001000 // suppress overlay
#define A_SPECIAL 0x00002000 // (reserved)
#define A_SENSOR 0x00004000
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
#define AP_CBRANDOM 0x0A // Counter-based random

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

// AP_CBRANDOM:
//  app[1] bit1-bit0 = Distribution select
//  app[1] bit6-bit2 = Offset of appearance_mapping
//  app[1] bit7      = Alternate distribution

// Animation:mode
#define AM_X1 0x01
#define AM_X2 0x02
#define AM_Y1 0x04
#define AM_Y2 0x08
#define AM_SLOW 0x80

extern ElementDef elem_def[256];
extern Uint8 appearance_mapping[128];
extern Animation animation[4];

extern Uint8*global_text;
extern Uint16 global_length;
extern Uint16 global_frameoffset;
extern Uint16 global_frameptr;

extern Uint16 start_mode;

// === Variable properties ===

typedef struct VarProperty {
  Uint8 type;
  Uint8 data[15];
} VarProperty;

typedef struct VarPropertyList {
  VarProperty*item;
  Uint8 count;
} VarPropertyList;

void work_varproperties(VarPropertyList*vp);

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
  // Layer: bit1-bit0=layer (1=under, 2=main, 3=overlay), bit3-bit2=face, bit4=walk, bit5=disable, bit6=user, bit7=lock
  Tile sensor;
  Uint16 frame,extra;
} StatXY;

typedef struct {
  StatXY*xy;
  Uint8*text;
  Uint16 length,frame,count;
  Uint16 misc1,misc2,misc3;
  Uint8 speed,mode,zone;
} Stat;

typedef struct {
  Uint16 width,height;
  Uint16 screen;
  Uint16 exits[4];
  Uint16 userdata;
  Uint16 flag;
  VarPropertyList varprop;
} BoardInfo;

typedef struct {
  Uint16 x,y;
} OrdZoneXY;

typedef struct {
  Uint16 ncells,flag,extra;
  OrdZoneXY xy[0];
} OrdZone;

typedef struct {
  Uint16 flag,maxy,extra;
  Uint8 data[0];
} UnordZone;

// Stat:mode
#define STAT_DYNAMIC 0x01
#define STAT_VACANT 0x02
#define STAT_INDEPENDENT 0x04
#define STAT_GLOBAL 0x08
#define STAT_SPRITES 0x10
#define STAT_ZONERESTRICT 0x20

// BoardInfo:flag
#define BF_USER0 0x0001
#define BF_USER1 0x0002
#define BF_USER2 0x0004
#define BF_USER3 0x0008
#define BF_PERSIST 0x0010  // save board state before going to another board
#define BF_NO_GLOBAL 0x0020  // suspend execution of global scripts
#define BF_OVERLAY 0x0040  // display overlay
#define BF_SAVE_ON_SENSOR 0x0100
#define BF_SAVE_NOT_SENSOR 0x0200
#define BF_MAIN_ALT_MODE 0x0400
#define BF_OVER_ALT_MODE 0x0800

// Overlay kind bits (the low nybble has user-defined meanings)
#define OVER_SOLID 0x10  // affects movement of stats in overlay
#define OVER_RESERVED 0x20  // reserved for future use (possibly wide characters)
#define OVER_BG_THRU 0x40  // show through background colour
#define OVER_VISIBLE 0x80  // overlay is visible (if not set, it is transparent)

// OrdZone:flag, UnordZone:flag
#define ZF_USER0 0x0001
#define ZF_USER1 0x0002
#define ZF_USER2 0x0004
#define ZF_USER3 0x0008
#define ZF_REVERSE 0x0010
#define ZF_AFFECT_UNDER 0x0100
#define ZF_AFFECT_MAIN 0x0200
#define ZF_AFFECT_OVER 0x0400
// The two high bits are movement kind: 0(cyclic) 1(full) 2(normal) 3(pushable)

extern Uint16 cur_board_id;
extern BoardInfo board_info;
extern Tile*b_under;
extern Tile*b_main;
extern Tile*b_over;
extern Stat*stats;
extern Uint8 maxstat;

extern OrdZone*ozone[16];
extern UnordZone*uzone[16];

extern Uint8**boardnames;
extern Uint16 maxboard;

StatXY*add_statxy(int n);
const char*select_board(Uint16 b);
//Uint8 draw_tile(Sint32 bx,Sint32 by,Uint16 at,Uint8 h);

Uint8 in_zone(Uint32 x,Uint32 y,Uint8 z);
void zone_add(Uint32 x,Uint32 y,Uint8 z,Uint8 w);
void zone_remove(Uint32 x,Uint32 y,Uint8 z);

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
#define NF_CHARACTER 'c'
#define NF_APPEARANCE '?'
#define NF_NONZERO '!'
#define NF_BOARD_NAME 'n'
#define NF_BOARD_NAME_EXT 'N'

typedef struct {
  Uint8*data; // command for first selection, command for second selection, etc; color; parameter
  Uint16 sel;
  Uint8 x,y,w,h,cur,count;
} Panel;

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
  VarPropertyList varprop;
  Uint8 npanels;
  Panel*panels;
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
#define SC_SPEC_CONTEXT_SPECIFIC 0x3F

#define SC_IND_USERDATA 0x50
#define SC_IND_CURSOR 0x51
#define SC_IND_SCROLL_Y 0x52
#define SC_IND_SCROLL_X 0x53
#define SC_IND_EXIT_E 0x58
#define SC_IND_EXIT_N 0x59
#define SC_IND_EXIT_W 0x5A
#define SC_IND_EXIT_S 0x5B
#define SC_IND_USER0 0x5C
#define SC_IND_USER1 0x5D
#define SC_IND_USER2 0x5E
#define SC_IND_USER3 0x5F

#define SC_ITEM_PLACEHOLDER 0x70 // parameter=default character
#define SC_ITEM_ELEMENT 0x71 // bit7-bit5=inventory, bit4-bit0=slot
#define SC_ITEM_SELECT_FIELD 0x72
#define SC_ITEM_SHOW_FIELD 0x73
#define SC_ITEM_FLAGS 0x78 // to 0x7F; parameter=character if flag is set

// Screen:flag
#define SF_LEFT_ALIGN_MESSAGE 0x01
#define SF_FLASHY_MESSAGE 0x02
#define SF_EXIT_BORDER 0x04
#define SF_USER_BORDER 0x08
#define SF_NO_SCROLL 0x10
#define SF_MESSAGE_EDGE 0x20
#define SF_ALT_MODE 0x40

extern NumericFormat num_format[16];
extern Screen cur_screen;
extern Uint16 cur_screen_id;
extern Sint32 scroll_x,scroll_y;

extern Uint8**screennames;
extern Uint16 maxscreen;

const char*load_screen(FILE*fp);
const char*save_screen(FILE*fp);
void update_screen(void);
void update_panels(void);

// === Screen windows ===

typedef struct {
  Uint8 command[80];
  Uint8 color[80];
  Uint8 parameter[80];
  Uint8 flag;
  Uint8 wcolor[16];
} WindowInfo;

// WindowInfo:flag
#define WF_SINGLE_ENDS 0x01
#define WF_ZERO_BASED 0x02
#define WF_HORIZ_SCROLL 0x04
#define WF_XOR_COLOR 0x08
#define WF_ALT_SCROLL 0x40
#define WF_KEY_EVENT 0x80

// Window colour index
#define WC_NORMAL_TEXT 0
#define WC_LINK_TEXT 1
#define WC_CENTER_TEXT 2
#define WC_LABEL_TEXT 3
#define WC_NORMAL_ITEM 8
#define WC_KEY_ITEM 9
#define WC_FIXED_ITEM 10
#define WC_HILIGHT_ITEM 11
#define WC_VACANT_ITEM 12
#define WC_MOVE_ITEM 14
#define WC_SELECTED_ITEM 15

const char*load_window(FILE*fp,WindowInfo*wind);

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
//   U = Coroutine U
//   V = Coroutine V

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
#define XOP_S_BOARD_ID 0x0D80 // current board number
#define XOP_S_SCROLL_X 0x0DA0 // scroll X plus (source+(value&15)-8)
#define XOP_S_SCROLL_Y 0x0DB0 // scroll Y plus (source+(value&15)-8)
#define XOP_S_IF_TRUE 0x0DC0 // (source) if true, (value) if false (-3 to +4, A to H)
#define XOP_S_IF_FALSE 0x0DD0 // (source) if false, (value) if true (-3 to +4, A to H)

#define MEM_INVENTORY 0xA0
#define MEM_ITEM_MASK 0xA1
#define MEM_GIVE_ITEM_EVENT 0xA2
#define MEM_TAKE_ITEM_EVENT 0xA3
#define MEM_NAME_ITEM_EVENT 0xA4
#define MEM_ITEM_SCREEN 0xA5
#define MEM_ITEM_EXCLUSIVE_BITS 0xA6
#define MEM_WINDOW_KEY_EVENT 0xB0
#define MEM_MUSIC_EXTRA 0xB1
#define MEM_CALL_STACK 0xB2
#define MEM_CALL_STATUS 0xB3
#define MEM_RECEIVED_DATA_EVENT 0xB4
#define MEM_FONT_ANIM 0xB5
#define MEM_OVERLAY_KEEP_BITS 0xB6
#define MEM_DEFAULT_BACKGROUND 0xB7
#define MEM_PANEL_SELECTION 0xB8
#define MEM_GLOBAL_DELAY 0xC0
#define MEM_NEW_DYNAMIC_STAT_EVENT 0xC1
#define MEM_OVERLAYMEM_ADDRESS 0xC2
#define MEM_OVERLAYMEM_SIZE 0xC3
#define MEM_JOY_LEVEL 0xC4
#define MEM_RETURN_EVENT 0xC5
#define MEM_RETURNED_PC 0xC6
#define MEM_CREATE_BOARD 0xC7
#define MEM_TRANSPORT_EVENT 0xC8
#define MEM_DEFAULT_OVERLAY 0xC9
#define MEM_GLOBAL_INSTPTR 0xCA
#define MEM_CONTROL 0xCB
#define MEM_ARG_J 0xCC
#define MEM_ARG_K 0xCD
#define MEM_COROUTINE_U_PC 0xCE
#define MEM_COROUTINE_V_PC 0xCF
#define MEM_COROUTINE_U_W_HI 0xD0
#define MEM_COROUTINE_U_W_LO 0xD1
#define MEM_COROUTINE_U_X_HI 0xD2
#define MEM_COROUTINE_U_X_LO 0xD3
#define MEM_COROUTINE_U_Y_HI 0xD4
#define MEM_COROUTINE_U_Y_LO 0xD5
#define MEM_COROUTINE_U_Z_HI 0xD6
#define MEM_COROUTINE_U_Z_LO 0xD7
#define MEM_COROUTINE_V_W_HI 0xD8
#define MEM_COROUTINE_V_W_LO 0xD9
#define MEM_COROUTINE_V_X_HI 0xDA
#define MEM_COROUTINE_V_X_LO 0xDB
#define MEM_COROUTINE_V_Y_HI 0xDC
#define MEM_COROUTINE_V_Y_LO 0xDD
#define MEM_COROUTINE_V_Z_HI 0xDE
#define MEM_COROUTINE_V_Z_LO 0xDF
#define MEM_KEY_EVENT 0xE0
#define MEM_FRAME_EVENT 0xE1
#define MEM_CUSTOM_COMMAND 0xE2
#define MEM_LIGHT 0xE3
#define MEM_WARP_TO 0xE4
#define MEM_WARP_CALL 0xE5
#define MEM_WARP_X_HI 0xE6
#define MEM_WARP_X_LO 0xE7
#define MEM_WARP_Y_HI 0xE8
#define MEM_WARP_Y_LO 0xE9
#define MEM_WARP_Z_HI 0xEA
#define MEM_WARP_Z_LO 0xEB
#define MEM_FRAME_COUNTER 0xEC
#define MEM_TEXT_SCREEN 0xED
#define MEM_SCROLL_X_RATE 0xEE
#define MEM_SCROLL_Y_RATE 0xEF

#define CONTROL_NOSCROLL 0x0001
#define CONTROL_DELAY0_SEND 0x0002
#define CONTROL_VTEXT_FOREVER 0x0004
#define CONTROL_DISABLE_SAVING 0x0008
#define CONTROL_SAVE_BOARD 0x0010
#define CONTROL_RESTORE_BOARD 0x0020
#define CONTROL_SENT 0x0040
#define CONTROL_WIN_STOP_KEY_REPEAT 0x0080
#define CONTROL_NUMBER_KEY_DIRECTION 0x0100
#define CONTROL_OVERLAY_SENSOR 0x0200

extern Uint16 memory[0x10000];
extern Sint32 regs[8];
extern Uint8 condflag;

extern Uint8**gtext;
extern Uint8*vgtext;
extern Uint16 ngtext;

// === Item/inventory ===

typedef struct {
  Uint32 flag,maxheap,name,script,desc,weight,price,ext3,appearance;
  Uint16 ext4,ext5;
  Uint8 element,class,color,parameter,special;
  // (class 255 means this item is not defined)
} ItemDef;

extern Uint8*itemnames;
extern ItemDef*itemdefs;
extern Uint16 nitemdefs;

// ItemDef:flag
#define IDF_NO_DISCARD 0x0001
#define IDF_SINGLE_HEAP 0x0002
#define IDF_HIDE_QUANTITY 0x0004
#define IDF_UNIDENTIFIED 0x0008
#define IDF_NO_RANDOMIZE 0x0010
#define IDF_HILIGHT 0x0020
#define IDF_EVENT 0x0080

// ItemDef:special
#define ISPECIAL_NONE 0x00
#define ISPECIAL_STATUS 0x10 // add number of status variable 0 to 15
#define ISPECIAL_STATUS_NONZERO 0x20 // add number of status variable 0 to 15
#define ISPECIAL_EXCLUSIVE_BITS 0x30 // add bit position number 0 to 15

typedef struct {
  Uint32 quantity,ext0;
  Uint16 item,flag,ext1,ext2;
} ItemSlot;

typedef struct {
  ItemSlot*item;
  Uint32 maxheap,strength;
  Uint16 count,flag,cursor;
} Inventory;

extern Inventory inventory[8];

// Inventory:flag
#define INV_USER0 0x0001
#define INV_USER1 0x0002
#define INV_USER2 0x0004
#define INV_USER3 0x0008
#define INV_IGNORE_MAXHEAP 0x0100
#define INV_SINGLE_HEAP 0x0200
#define INV_IGNORE_WEIGHT 0x0400
#define INV_SPECIAL 0x0800

// ItemSlot:flag
#define ISF_IN_USE 0x8000
#define ISF_FIXED 0x4000
#define ISF_HIDDEN 0x2000
#define ISF_IGNORE 0x1000
#define ISF_HILIGHT 0x0800
#define ISF_SPECIAL 0x0200
#define ISF_MARK 0x0100

const char*load_inventory(FILE*fp,Inventory*inv);
const char*save_inventory(FILE*fp,Inventory*inv);

extern Uint32 item_random_key;
const char*randomize_itemdefs(Uint8 rev);

// === Game state ===

extern Sint32 status_vars[16];

extern Uint8 textbuf[81];
extern Uint8 ntextbuf;
extern Uint8 vtextbuf[81];
extern Uint8 nvtextbuf;
extern Uint16 vtexttime;

typedef struct {
  Uint8 name[16];
} NamedFlag;

extern NamedFlag namedflag[16];

#define DYNASTRLEN 70
typedef struct {
  Uint8 text[DYNASTRLEN];
  Uint8 len,unused;
} DynaString;

extern DynaString*dynastr;
extern Uint8 ndynastr;
extern VarProperty pvarproperty;
extern ASN1_Value asn1reg[4];

// === Special option menu ===

typedef struct {
  Uint32 key;
  Uint16 value,flag;
} SpecialOption;

extern Uint16 nspecopt;
extern SpecialOption*specopt;

// SpecialOption:key
// (the relative OID excluding the first byte (always 0x08), padded with <80 00 00 00>)
#define SPECI_VACANT 0x80808080
#define SPECI_UNKNOWN 0x80808081
#define SPECI_FONTANIM 0x00800000
#define SPECI_AUDIO 0x01800000

// SpecialOption:flag
#define SPECF_LOCKED 0x0001
#define SPECF_SAVE 0x2000
#define SPECF_VARIABLE 0x4000
#define SPECF_LOCKABLE 0x8000

// Types of special option menu
#define SPECT_MENU 0
#define SPECT_OPTION 1
#define SPECT_HELP 2
#define SPECT_GOTOPAGE 3
#define SPECT_CANCEL 4
#define SPECT_SOUND 5
#define SPECT_PAGEBREAK 254
#define SPECT_INVALID 255

void special_option_menu(void);
const char*load_special_options(FILE*f);
void config_special_options(char*line);
void end_config_special_options(void);
Uint16 spec_auto_value(Uint32 key);
void special_option_debug(void);

void show_help_file(void); // defined in game.c

// === Slices ===

void unload_slices(Uint8 level);
const char*load_slices(FILE*fp,Uint8 level);
const char*load_screen_slices(Uint8 level);
void save_slices(FILE*fp,Uint8 level);

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
size_t copy_stream(FILE*in,FILE*out,size_t len);
void list_lumps(const char*pat,const char***list,int*count);

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

// === Save games ===

int ask_save_file(char issave);
void save_state(void);
void load_state(void);

// === Printer ===

int lpt_begin(void); // returns 1 to begin printing or 0 to disable printing
void lpt_end(void); // must be called to end printing
#define lpt_document() for(int lpt_document_=lpt_begin();lpt_document_;lpt_document_=(lpt_end(),0))

void lpt_linefeed(void);
void lpt_text(const Uint8*p,int w);
void lpt_title(const Uint8*p,int w);
void lpt_heading(const Uint8*p,int w);
void lpt_link(const Uint8*p,int w);
void lpt_script(const Uint8*p,int w);

// === Window ===

void draw_border(Uint8 c,Uint8 x0,Uint8 y0,Uint8 x1,Uint8 y1);
void alert_text(const char*text);
void ask_text(const char*prompt,Uint8*buf,int len);
void ask_text_restrict(const char*prompt,Uint8*buf,int len);
int ask_yn(const char*prompt,int d);
Uint8 ask_color_char(Uint8 m,Uint8 v);
void online_help(const char*major,const char*minor);

typedef struct {
  int line,cur,ncur,scroll;
  Uint8 state,ready;
} win_memo;

#define win_form(xxx) for(win_memo win_mem=win_begin_();;win_step_(&win_mem,xxx))
#define win_refresh() win_begin_()
win_memo win_begin_(void);
void win_step_(win_memo*,const char*);

#define win_numeric(aaa,bbb,ccc,ddd,eee) if(win_numeric_(&win_mem,aaa,bbb,&(ccc),sizeof(ccc),ddd,eee))
int win_numeric_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 lo,Uint32 hi);

#define win_boolean(aaa,bbb,ccc,ddd) if(win_boolean_(&win_mem,aaa,bbb,&(ccc),sizeof(ccc),ddd))
int win_boolean_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 b);

#define win_command(aaa,bbb) if(win_command_(&win_mem,aaa,bbb,0))
#define win_command_esc(aaa,bbb) if(win_command_(&win_mem,aaa,bbb,1))
int win_command_(win_memo*wm,Uint8 key,const char*label,Uint8 esc);

#define win_blank() win_heading_(&win_mem,0)
#define win_heading(aaa) win_heading_(&win_mem,aaa)
void win_heading_(win_memo*wm,const char*label);

#define win_picture(aaa) while(win_picture_(&win_mem,aaa))
int win_picture_(win_memo*wm,int h);

#define win_option(aaa,bbb,ccc,ddd) if(win_option_(&win_mem,aaa,bbb,&(ccc),sizeof(ccc),ddd))
int win_option_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,Uint32 b);

#define win_text(aaa,bbb,ccc) if(win_text_(&win_mem,aaa,bbb,ccc,sizeof(ccc),0))
#define win_text_restrict(aaa,bbb,ccc) if(win_text_(&win_mem,aaa,bbb,ccc,sizeof(ccc),1))
int win_text_(win_memo*wm,Uint8 key,const char*label,Uint8*v,size_t s,Uint8 q);

#define win_color(aaa,bbb,ccc) if(win_color_char_(&win_mem,aaa,bbb,&(ccc),sizeof(ccc),0))
#define win_char(aaa,bbb,ccc) if(win_color_char_(&win_mem,aaa,bbb,&(ccc),sizeof(ccc),1))
int win_color_char_(win_memo*wm,Uint8 key,const char*label,void*v,size_t s,int m);

#define win_list(aaa,bbb,ccc,ddd) if((ddd=win_list_(&win_mem,aaa,bbb,ccc))!=-1)
int win_list_(win_memo*wm,int n,void*u,void(*f)(Uint16,int,void*));

#define win_cursor(aaa) win_cursor_(&win_mem,aaa)
void win_cursor_(win_memo*wm,int offset);

#define win_help(aaa,bbb) win_help_(&win_mem,aaa,bbb)
void win_help_(win_memo*wm,const char*major,const char*minor);

