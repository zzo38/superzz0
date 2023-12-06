#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 edit.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

Uint8**screennames;
Uint16 maxscreen;

static void write_start_lump(void) {
  int i;
  FILE*fp=open_lump("START","r+");
  if(!fp) fp=open_lump("START","w");
  if(!fp) errx(1,"Cannot open START lump for writing");
  write16(fp,SUPER_ZZ_ZERO_VERSION);
  write16(fp,cur_board_id);
  fseek(fp,12,SEEK_SET);
  for(i=0;i<16;i++) write32(fp,status_vars[i]);
  fclose(fp);
}

static void write_element_lump(void) {
  int i,j,b;
  ElementDef*e;
  FILE*fp=open_lump("ELEMENT","w");
  if(!fp) errx(1,"Cannot open ELEMENT lump for writing");
  fwrite(appearance_mapping,1,128,fp);
  for(i=0;i<4;i++) {
    fputc(animation[i].mode,fp);
    fputc(animation[i].step[0],fp);
    fputc(animation[i].step[1],fp);
    fputc(animation[i].step[2],fp);
    fputc(animation[i].step[3],fp);
  }
  for(i=0;i<256;i++) {
    e=elem_def+i;
    if(e->name[0]) {
      b=strlen(e->name);
      if(e->app[0] && !(e->app[0]==AP_PARAM && !e->app[1])) b|=0x80;
      if(e->app[1]) b|=0x40;
      if(!i || e->attrib!=e[-1].attrib) b|=0x20;
      for(j=0;j<16;j++) if(e->event[j]) b|=0x10;
      fputc(b,fp);
      fwrite(e->name,1,b&15,fp);
      if(b&0x80) fputc(e->app[0],fp);
      if(b&0x40) fputc(e->app[1],fp);
      if(b&0x20) write32(fp,e->attrib);
      if(b&0x10) {
        for(b=j=0;j<16;j++) if(e->event[j]) b|=1<<j;
        write16(fp,b);
        for(j=0;j<16;j++) if(e->event[j]) write16(fp,e->event[j]);
      }
    } else {
      for(j=1;j<16 && i+j<256;j++) if(e[j].name[0]) break;
      fputc((j-1)<<4,fp);
      i+=j-1;
    }
  }
  fclose(fp);
}

static void write_numform_lump(void) {
  int i;
  FILE*fp=open_lump("NUMFORM","w");
  if(!fp) errx(1,"Cannot open NUMFORM lump for writing");
  for(i=0;i<16;i++) {
    fputc(num_format[i].code,fp);
    fputc(num_format[i].lead,fp);
    fputc(num_format[i].mark,fp);
    fputc(num_format[i].div,fp);
  }
  fclose(fp);
}

static void board_list_callback(Uint16 n,int y,void*uz) {
  FILE*fp=open_lump_by_number(n,"BRD","r");
  char buf[80];
  snprintf(buf,80,"%5d:",n);
  draw_text(1,y,buf,fp?7:8,6);
  v_color[80*y+6]=8;
  if(n<=maxboard && boardnames[n]) draw_text(7,y,boardnames[n],15,61);
  if(fp) {
    n=read16(fp);
    v_color[80*y+77]=v_color[80*y+78]=2;
    v_char[80*y+77]="\xFA\x1A\x18=\x1B\x1D==\x19=\x12"[n&5];
    v_char[80*y+78]="\xFA\x1A\x18=\x1B\x1D==\x19=\x12"[n&10];
  }
  fclose(fp);
}

static void screen_list_callback(Uint16 n,int y,void*uz) {
  char buf[80];
  snprintf(buf,80,"%5d:",n);
  draw_text(1,y,buf,7,6);
  v_color[80*y+6]=8;
  if(n<=maxscreen && screennames[n]) draw_text(7,y,screennames[n],15,61);
}

static void element_list_callback(Uint16 n,int y,void*uz) {
  ElementDef*e=elem_def+n;
  char buf[80];
  char*s=buf;
  Uint32 x;
  snprintf(buf,80,"%3d:",n);
  draw_text(1,y,buf,e->name[0]?7:8,4);
  if(!e->name[0]) return;
  draw_text(5,y,e->name,0x0B,15);
  if(e->app[0]&0x0F) {
    v_color[80*y+22]=0x08;
    v_char[80*y+22]="FPOUS123LA??????"[e->app[0]&0x0F];
  } else {
    v_color[80*y+22]=0x0F;
    v_char[80*y+22]=e->app[1];
  }
  snprintf(buf,2,"%X",e->attrib&15);
  draw_text(24,y,buf,0x20,1);
  v_color[80*y+25]=(e->attrib&A_PUSH_NS?0x0F:0x08);
  v_char[80*y+25]=(e->attrib&A_PUSH_NS?0x12:0xFA);
  v_color[80*y+26]=(e->attrib&A_PUSH_EW?0x0F:0x08);
  v_char[80*y+26]=(e->attrib&A_PUSH_EW?0x1D:0xFA);
  x=e->attrib&(A_OVER_COLOR|A_UNDER_COLOR);
  v_color[80*y+27]=(x?0x0D:0x08);
  v_char[80*y+27]="\xFAous"[x>>6];
  v_color[80*y+28]=(e->attrib&A_CRUSH?0x0C:0x08);
  v_char[80*y+28]=(e->attrib&A_CRUSH?'C':0xFA);
  v_color[80*y+29]=(e->attrib&A_FLOOR?0x09:0x08);
  v_char[80*y+29]=(e->attrib&A_FLOOR?'F':0xFA);
  v_color[80*y+30]=(e->attrib&A_UNDER_BGCOLOR?0x0D:0x08);
  v_char[80*y+30]=(e->attrib&A_UNDER_BGCOLOR?0x81:0xFA);
  v_color[80*y+31]=(e->attrib&A_LIGHT?0x0E:0x08);
  v_char[80*y+31]=(e->attrib&A_LIGHT?0x9D:0xFA);
  v_color[80*y+32]=(e->attrib&A_TRANSPORTER?0x0F:0x08);
  v_char[80*y+32]=(e->attrib&A_TRANSPORTABLE?'T':0xFA);
  v_color[80*y+33]=(e->attrib&A_TRANSPORTER?0x0F:0x08);
  v_char[80*y+33]=(e->attrib&A_TRANSPORTABLE?'t':0xFA);
  for(x=0;x<8;x++) {
    v_color[80*y+x+34]=(e->attrib&(A_MOVE_C0<<x)?0x0A:0x08);
    v_char[80*y+x+34]=(e->attrib&(A_MOVE_C0<<x)?x+'0':0xFA);
    v_color[80*y+x+42]=(e->attrib&(A_MISC_A<<x)?0x06:0x08);
    v_char[80*y+x+42]=(e->attrib&(A_MISC_A<<x)?x+'A':0xFA);
  }
  for(x=0;x<16;x++) {
    v_color[80*y+x+52]=(e->event[x]?0x02:0x08);
    v_char[80*y+x+52]=(e->event[x]?x+(x<8?'A':'S'-8):0xFA);
  }
}

void edit_element(Uint8 en) {
  ElementDef*e=elem_def+en;
  char title[80];
  Uint8 cla=e->attrib&15;
  Uint8 colo=(e->attrib&(A_OVER_COLOR|A_UNDER_COLOR))>>6;
  snprintf(title,80,"Edit element #%d ($%02X)",en,en);
  win_form(title) {
    win_text_restrict('m',"Name: ",e->name);
    win_numeric('l',"Class: ",cla,0,15);
    win_boolean('P',"Pushable \x12",e->attrib,A_PUSH_NS);
    win_boolean('u',"Pushable \x1D",e->attrib,A_PUSH_EW);
    win_boolean('r',"Crushable",e->attrib,A_CRUSH);
    win_boolean('o',"Floor",e->attrib,A_FLOOR);
    win_boolean('T',"Transporter",e->attrib,A_TRANSPORTER);
    win_boolean('n',"Transportable",e->attrib,A_TRANSPORTABLE);
    win_heading("Movement classes:");
    win_boolean('0',"Class 0",e->attrib,A_MOVE_C0);
    win_boolean('1',"Class 1",e->attrib,A_MOVE_C1);
    win_boolean('2',"Class 2",e->attrib,A_MOVE_C2);
    win_boolean('3',"Class 3",e->attrib,A_MOVE_C3);
    win_boolean('4',"Class 4",e->attrib,A_MOVE_C4);
    win_boolean('5',"Class 5",e->attrib,A_MOVE_C5);
    win_boolean('6',"Class 6",e->attrib,A_MOVE_C6);
    win_boolean('7',"Class 7",e->attrib,A_MOVE_C7);
    win_heading("User-defined:");
    win_boolean('A',"Misc. A",e->attrib,A_MISC_A);
    win_boolean('B',"Misc. B",e->attrib,A_MISC_B);
    win_boolean('C',"Misc. C",e->attrib,A_MISC_C);
    win_boolean('D',"Misc. D",e->attrib,A_MISC_D);
    win_boolean('E',"Misc. E",e->attrib,A_MISC_E);
    win_boolean('F',"Misc. F",e->attrib,A_MISC_F);
    win_boolean('G',"Misc. G",e->attrib,A_MISC_G);
    win_boolean('H',"Misc. H",e->attrib,A_MISC_H);
    win_blank();
    win_command('.',"Appearance...") {
      Uint8 m=(e->app[0]&0x20?:e->app[0]&0x1F);
      Uint8 lj=e->app[0]>>6;
      Uint8 ao=((e->app[1]>>4)&7);
      Uint8 ao1=(e->app[1]&0x7F)>>1;
      Uint8 ao2=(e->app[1]&0x7F)>>2;
      Uint8 as=e->app[1]&3;
      Uint8 bs=e->app[0]&7;
      Uint8 bi=((e->app[0]>>3)&3)+1;
      win_form(title) {
        win_boolean('B',"Background of under layer",e->attrib,A_UNDER_BGCOLOR);
        win_boolean('k',"Visible in dark",e->attrib,A_LIGHT);
        win_numeric('j',"Line joining class: ",lj,0,3);
        win_heading("Color:");
        win_option('w',"Own color",colo,0);
        win_option('v',"Over layer",colo,1);
        win_option('y',"Under layer",colo,2);
        win_option('c',"Screen data",colo,3);
        win_heading("Character mode:");
        win_option('x',"Fixed",m,AP_FIXED) win_refresh();
        win_option('P',"Parameter (character)",m,AP_PARAM) win_refresh();
        win_option('m',"Parameter (mapped)",m,0x20) win_refresh();
        win_option('A',"Animation (ignore parameter)",m,AP_ANIMATE) win_refresh();
        win_option('L',"Line joining",m,AP_LINES) win_refresh();
        win_option('1',"Misc1",m,AP_MISC1) win_refresh();
        win_option('2',"Misc2",m,AP_MISC2) win_refresh();
        win_option('3',"Misc3",m,AP_MISC3) win_refresh();
        win_option('e',"Over layer",m,AP_OVER) win_refresh();
        win_option('U',"Under layer",m,AP_UNDER) win_refresh();
        win_option('S',"Screen data",m,AP_SCREEN) win_refresh();
        win_heading("Character options:");
        switch(m) {
          case AP_FIXED:
            win_char('h',"Character: ",e->app[1]);
            break;
          case AP_PARAM: case AP_UNDER: case AP_MISC1: case AP_MISC2: case AP_MISC3:
            win_char('h',"Default character: ",e->app[1]);
            break;
          case AP_LINES:
            win_boolean('0',"Join class 0",e->app[1],0x01);
            win_boolean('o',"Join class 1",e->app[1],0x02);
            win_boolean('i',"Join class 2",e->app[1],0x04);
            win_boolean('n',"Join class 3",e->app[1],0x08);
            win_boolean('g',"Join edge",e->app[1],0x80);
            win_numeric('r',"Appearance mapping offset: 16x",ao,0,7);
            break;
          case AP_ANIMATE:
            win_numeric('n',"Animation select: ",as,0,3);
            win_numeric('r',"Appearance mapping offset: 4x",ao2,0,31);
            win_boolean('T',"Time-based",e->app[1],0x80);
            break;
          case 0x20:
            win_numeric('h',"Parameter shift: ",bs,0,7);
            win_numeric('i',"Bits of parameter: ",bi,1,4);
            win_numeric('r',"Appearance mapping offset: 2x",ao1,0,63);
            win_boolean('o',"Enable animation",e->app[1],0x80);
            win_boolean('n',"Use animation 1",e->app[1],0x01);
            break;
          default:
            win_picture(1) draw_text(1,0,"(None)",8,-1);
            break;
        }
        win_blank();
        win_command_esc(0,"Back") break;
      }
      e->app[0]=m|(lj<<6);
      switch(m) {
        case AP_LINES: e->app[1]|=ao<<4; break;
        case AP_ANIMATE: e->app[1]&=0x80; e->app[1]|=as|(ao2<<2); break;
        case 0x20: e->app[0]|=bs|((bi-1)<<3); e->app[1]&=0x81; e->app[1]|=ao1<<1; break;
      }
    }
    win_command_esc(0,"Done") break;
  }
  e->attrib=(e->attrib&~(15|A_OVER_COLOR|A_UNDER_COLOR))|cla|(colo<<6);
}

char edit_appearance_mapping(void) {
  char c=0;
  int i;
  int n=0;
  char buf[6];
  draw:
  memset(v_char,32,80*25);
  memset(v_color+80,0x07,80*24);
  memset(v_color,0x30,80);
  strcpy(v_char,"Appearance mapping");
  for(i=0;i<128;i++) {
    snprintf(buf,6,"%3d:",i);
    draw_text((i>>4)*9+2,(i&15)+2,buf,0x07,4);
  }
  draw_text(2,19,"<ESC> Done   <SPACE> Edit",7,-1);
  key:
  for(i=0;i<128;i++) {
    v_char[(i>>4)*9+(i&15)*80+161]=(n==i?0x10:0xFA);
    v_color[(i>>4)*9+(i&15)*80+161]=(n==i?0x0E:0x08);
    v_char[(i>>4)*9+(i&15)*80+166]=appearance_mapping[i];
    v_color[(i>>4)*9+(i&15)*80+166]=(n==i?0x2F:0x1F);
  }
  redisplay();
  for(;;) {
    if(!next_event()) return 0;
    if(event.type==SDL_KEYDOWN) switch(event.key.keysym.sym) {
      case SDLK_HOME: n=0; goto key;
      case SDLK_END: n=127; goto key;
      case SDLK_UP: n=(n-1)&127; goto key;
      case SDLK_DOWN: n=(n+1)&127; goto key;
      case SDLK_LEFT: n=(n-16)&127; goto key;
      case SDLK_RIGHT: n=(n+16)&127; goto key;
      case SDLK_SPACE: case SDLK_RETURN: c=1; appearance_mapping[n]=ask_color_char(1,appearance_mapping[n]); goto draw;
      case SDLK_ESCAPE: return c;
    }
  }
}

int run_editor(void) {
  int i,n,lo,hi;
  char c,b;
  Uint16 lbrd=cur_board_id;
  Uint16 lscr=0;
  v_status[0]='E';
  win_form("Editor") {
    win_numeric('t',"Starting board: ",cur_board_id,0,65535);
    win_command('B',"Boards...") {
      boards_form:
      win_form("Boards") {
        win_cursor(lbrd);
        if(boardnames) win_list(maxboard+1,0,board_list_callback,n) {
          //edit_board(n);
          win_refresh();
        }
        win_blank();
        if(boardnames) win_command('F',"Find") {
          char buf[61]="";
          ask_text("Find:",buf,60);
          b=strlen(buf);
          for(n=0;n<=maxboard;n++) {
            if(lbrd!=n && boardnames[n] && !strncmp(boardnames[n],buf,b)) {
              lbrd=n;
              goto boards_form;
            }
          }
        }
        if(maxboard!=65535) {
          win_command('A',"Add new board") {
            
          }
          win_command('C',"Copy board...") {
            char buf[61]="";
            n=lbrd;
            win_form("Copy board") {
              win_text('N',"Name: ",buf);
              win_numeric('S',"Source: ",n,0,maxboard);
              win_blank();
              win_command('x',"Execute") {
                
              }
              win_command_esc(0,"Cancel") break;
            }
          }
        }
        win_command_esc(0,"Done") break;
      }
    }
    win_command('c',"Screens...") {
      win_cursor(lbrd);
      win_form("Screens") {
        if(screennames) win_list(maxscreen+1,0,screen_list_callback,n) {
          //edit_screen(n);
          win_refresh();
        }
        win_blank();
        
        win_command_esc(0,"Done") break;
      }
    }
    win_command('v',"Status variables...") {
      win_form("Status variables") {
        int i;
        for(i=0;i<16;i++) {
          char b[3]={(i&7)+(i&8?'S':'A'),'=',0};
          win_numeric(*b,b,status_vars[i],0,999999999);
        }
        win_blank();
        win_command_esc(0,"Done") break;
      }
      write_start_lump();
    }
    win_command('E',"Elements...") {
      c=0;
      win_form("Elements") {
        win_list(256,0,element_list_callback,n) {
          c=1;
          edit_element(n);
        }
        win_blank();
        win_command('C',"Copy attributes...") {
          n=0; lo=0; hi=255; b=1;
          win_form("Copy attributes") {
            win_numeric('S',"Source: ",n,0,255);
            win_numeric('L',"Low target: ",lo,0,255);
            win_numeric('H',"High target: ",hi,0,255);
            win_boolean('E',"Exclude if already defined",b,1);
            win_blank();
            win_command('x',"Execute") {
              c=1;
              for(i=lo;i<=hi;i++) if(!b || !elem_def[i].name[0]) {
                elem_def[i].app[0]=elem_def[n].app[0];
                elem_def[i].app[1]=elem_def[n].app[1];
                elem_def[i].attrib=elem_def[n].attrib;
              }
              break;
            }
            win_command_esc(0,"Cancel") break;
          }
        }
        win_command_esc(0,"Done") break;
      }
      if(c) write_element_lump();
    }
    win_command('p',"Appearance mapping...") {
      if(edit_appearance_mapping()) write_element_lump();
      win_refresh();
    }
    win_command('i',"Animations...") {
      n=i=c=0;
      win_form("Animations") {
        win_numeric('m',"Animation edit: ",n,0,3) win_refresh();
        win_blank();
        win_numeric('S',"Step I:   ",animation[n].step[0],0,127) win_refresh(),c=1;
        win_numeric('t',"Step II:  ",animation[n].step[1],0,127) win_refresh(),c=1;
        win_numeric('e',"Step III: ",animation[n].step[2],0,127) win_refresh(),c=1;
        win_numeric('p',"Step IV:  ",animation[n].step[3],0,127) win_refresh(),c=1;
        win_boolean('X',"X1",animation[n].mode,AM_X1) win_refresh(),c=1;
        win_boolean('2',"X2",animation[n].mode,AM_X2) win_refresh(),c=1;
        win_boolean('1',"Y1",animation[n].mode,AM_Y1) win_refresh(),c=1;
        win_boolean('Y',"Y2",animation[n].mode,AM_Y2) win_refresh(),c=1;
        win_boolean('W',"SLOW",animation[n].mode,AM_SLOW) c=1;
        win_blank();
        win_numeric('v',"Preview from: ",i,0,127) win_refresh();
        win_picture(6) {
          for(lo=1;lo<12;lo++) for(hi=0;hi<6;hi++) {
            v_color[hi*80+lo]=v_color[hi*80+lo+20]=v_color[hi*80+lo+40]=v_color[hi*80+lo+60]=7;
            v_char[hi*80+lo]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3))&3]+i)&127];
            v_char[hi*80+lo+20]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3)+1)&3]+i)&127];
            v_char[hi*80+lo+40]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3)+2)&3]+i)&127];
            v_char[hi*80+lo+60]=appearance_mapping[(animation[n].step[(lo*(animation[n].mode&3)+hi*((animation[n].mode>>2)&3)+3)&3]+i)&127];
          }
        }
        win_blank();
        win_command_esc(0,"Done") break;
      }
      if(c) write_element_lump();
    }
    win_command('N',"Numeric formats...") {
      n=c=0;
      win_form("Numeric formats") {
        win_numeric('t',"Numeric format edit: ",n,0,15) win_refresh();
        win_blank();
        win_option('D',"Decimal",num_format[n].code,NF_DECIMAL);
        win_option('U',"Uppercase hex",num_format[n].code,NF_HEX_UPPER);
        win_option('w',"Lowercase hex",num_format[n].code,NF_HEX_LOWER);
        win_option('O',"Octal",num_format[n].code,NF_OCTAL);
        win_option('C',"Comma",num_format[n].code,NF_COMMA);
        win_option('R',"Roman",num_format[n].code,NF_ROMAN);
        win_option('y',"\x9Csd money",num_format[n].code,NF_LSD_MONEY);
        win_option('e',"Meter (full blocks)",num_format[n].code,NF_METER);
        win_option('h',"Meter (half blocks)",num_format[n].code,NF_METER_HALF);
        win_option('f',"Meter ext. (full blocks)",num_format[n].code,NF_METER_EXT);
        win_option('k',"Meter ext. (half blocks)",num_format[n].code,NF_METER_HALF_EXT);
        win_option('i',"Binary",num_format[n].code,NF_BINARY);
        win_option('x',"Binary ext.",num_format[n].code,NF_BINARY_EXT);
        win_option('a',"Character",num_format[n].code,NF_CHARACTER);
        win_option('z',"Nonzero",num_format[n].code,NF_NONZERO);
        win_option('B',"Board name",num_format[n].code,NF_BOARD_NAME);
        win_option('n',"Board name ext.",num_format[n].code,NF_BOARD_NAME_EXT);
        win_blank();
        win_char('L',"Lead: ",num_format[n].lead);
        win_char('M',"Mark: ",num_format[n].mark);
        win_numeric('v',"Division: ",num_format[n].div,0,255);
        win_blank();
        win_command_esc(0,"Done") break;
      }
      if(c) write_numform_lump();
    }
    win_blank();
    win_command('R',"Run") {
      
    }
    win_command('S',"Save") save_world(0);
    win_command('Q',"Quit") break;
  }
}
