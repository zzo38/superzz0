#if 0
gcc -s -O2 -c -Wno-unused-result world.c `sdl-config --cflags`
exit
#endif

#define USING_RW_DATA
#include "common.h"

#define N_FEATURES 1
static const Uint32 feature_avail[N_FEATURES]={0x00000000};
static Uint8 feature_bits[(N_FEATURES+7)/8];

const char*init_world(void) {
  // Returns 0 if OK, error message if error
  int i,j;
  Uint32 u;
  FILE*fp;
  // "FEATURE.REQ"
  if(fp=open_lump("FEATURE.REQ","r")) {
    while(u=read32(fp)) {
      for(i=0;i<N_FEATURES;i++) if(feature_avail[i]==u) break;
      if(i==N_FEATURES) return "Feature unavailable";
      feature_bits[i>>3]|=1<<(i&7);
    }
    fclose(fp);
  }
  // "FEATURE.OPT"
  if(fp=open_lump("FEATURE.OPT","r")) {
    while(u=read32(fp)) {
      for(i=0;i<N_FEATURES;i++) if(feature_avail[i]==u) break;
      if(i!=N_FEATURES) feature_bits[i>>3]|=1<<(i&7);
    }
    fclose(fp);
  }
  // "MEMORY"
  if(!editor) {
    fp=open_lump("MEMORY","r");
    if(!fp) return "Cannot open MEMORY lump";
    u=lump_size>>1;
    if(u>0x10000) u=0x10000;
    free(memory);
    memory=calloc(0x10000,sizeof(Uint16));
    if(!memory) err(1,"Allocation failed");
    for(i=0;i<u;i++) memory[i]=read16(fp);
    fclose(fp);
  }
  // "START"
  fp=open_lump("START","r");
  if(!fp) return "Cannot open START lump";
  u=read16(fp);
  if(u<1 || u>SUPER_ZZ_ZERO_VERSION) return "Wrong version";
  cur_screen.message_l=222;
  cur_board_id=read16(fp);
  read32(fp); // used later
  read32(fp); // used later
  for(i=0;i<16;i++) status_vars[i]=read32(fp);
  fclose(fp);
  // "NUMFORM"
  if(fp=open_lump("NUMFORM","r")) {
    for(i=0;i<16;i++) {
      num_format[i].code=read8(fp);
      num_format[i].lead=read8(fp);
      num_format[i].mark=read8(fp);
      num_format[i].div=read8(fp);
    }
    fclose(fp);
  } else {
    num_format[0].code=num_format[1].code='d';
    num_format[0].lead=' '; num_format[1].lead='0';
    num_format[0].div=num_format[1].div=1;
  }
  // "ELEMENT"
  memset(elem_def,0,sizeof(elem_def));
  fp=open_lump("ELEMENT","r");
  if(!fp) return "Cannot open ELEMENT lump";
  fread(appearance_mapping,1,128,fp);
  for(i=0;i<4;i++) {
    animation[i].mode=read8(fp);
    fread(animation[i].step,1,4,fp);
  }
  for(i=0;i<256;i++) {
    u=read8(fp);
    if(u&15) {
      fread(elem_def[i].name,1,u&15,fp);
      if(u&0x80) elem_def[i].app[0]=fgetc(fp);
      if(u&0x40) elem_def[i].app[1]=fgetc(fp);
      if(!(u&0xC0)) elem_def[i].app[0]=AP_PARAM;
      if(u&0x20) elem_def[i].attrib=read32(fp); else elem_def[i].attrib=i?elem_def[i-1].attrib:0;
      if(u&0x10) {
        u=read16(fp);
        for(j=0;j<16;j++) if(u&(1<<j)) elem_def[i].event[j]=read16(fp);
      }
    } else {
      i+=u>>4;
    }
  }
  fclose(fp);
  // "TEXT"
  free(vgtext),vgtext=0;
  free(gtext),gtext=0;
  if(fp=open_lump("TEXT","r")) {
    Uint8*p;
    vgtext=malloc(lump_size+1);
    if(!vgtext) err(1,"Allocation failed");
    fread(vgtext,1,lump_size,fp);
    vgtext[lump_size]=0;
    ngtext=1;
    for(i=0;i<lump_size;i++) if(!vgtext[i]) ++ngtext;
    gtext=malloc(ngtext*sizeof(Uint8*));
    if(!gtext) err(1,"Allocation failed");
    for(i=1,u=0,p=vgtext;i<ngtext;i++) {
      gtext[i]=p;
      p+=strlen(p)+1;
    }
    fclose(fp);
  } else {
    gtext=malloc(sizeof(Uint8*));
    if(!gtext) err(1,"Allocation failed");
    ngtext=1;
  }
  *gtext="";
  // "BRD.NAM"
  if(boardnames) {
    free(*boardnames);
    if(editor) for(u=1;u<=maxboard;u++) free(boardnames[u]);
  }
  free(boardnames),boardnames=0;
  if(fp=open_lump("BRD.NAM","r")) {
    Uint8*s;
    Uint8*ss;
    maxboard=read16(fp);
    boardnames=calloc(maxboard+1,sizeof(Uint8*));
    if(!boardnames) err(1,"Allocation failed");
    if(editor) {
      size_t z;
      for(j=0;j<=maxboard;j++) {
        s=0; z=0; if(getdelim((char**)&s,&z,0,fp)<=0) break;
        boardnames[j]=s;
      }
    } else {
      u=(lump_size<3?1:lump_size-2);
      s=malloc(u+1);
      if(!s) err(1,"Allocation failed");
      fread(s,1,u,fp);
      s[u]=0;
      ss=s+u;
      for(j=0;j<=maxboard && s<ss;j++) {
        i=strlen(s);
        boardnames[j]=s;
        s+=i+1;
      }
    }
    fclose(fp);
  }
  // "SCR.NAM"
  if(screennames) {
    free(*screennames);
    if(editor) for(u=1;u<=maxscreen;u++) free(screennames[u]);
  }
  free(screennames),screennames=0;
  if(editor && (fp=open_lump("SCR.NAM","r"))) {
    Uint8*s;
    size_t z;
    maxscreen=read16(fp);
    for(j=0;j<=maxboard;j++) {
      s=0; z=0; if(getdelim((char**)&s,&z,0,fp)<=0) break;
      boardnames[j]=s;
    }
    fclose(fp);
  }
  // done
  return 0;
}

static inline void fill_layer(Tile*p,Tile t,Uint32 c) {
  while(c--) *p++=t;
}

static void layer_inversion(void) {
  Uint32 tc=board_info.width*board_info.height;
  Uint32 i;
  Tile t;
  for(i=0;i<tc;i++) {
    if(b_under[i].kind && b_main[i].kind) {
      t=b_under[i];
      b_under[i]=b_main[i];
      b_main[i]=t;
    }
  }
}

const char*load_board(FILE*fp) {
  Uint8 c;
  Uint16 ef=read16(fp);
  Uint32 at,tc,n;
  Tile*pt;
  Tile*end;
  StatXY*r;
  int i,j;
  if(ef&0x7CC0) return "Unrecognized file format";
  free(b_under);
  b_under=b_main=b_over=0;
  for(i=0;i<maxstat;i++) {
    free(stats[i].text);
    free(stats[i].xy);
  }
  free(stats);
  stats=0;
  maxstat=0;
  memset(&board_info,0,sizeof(BoardInfo));
  board_info.flag=(ef&0x100?read16(fp):read8(fp));
  board_info.screen=(ef&0x200?read16(fp):read8(fp));
  for(i=0;i<4;i++) if(ef&(1<<i)) board_info.exits[i]=read16(fp);
  if(ef&0x10) {
    board_info.width=read16(fp);
    board_info.height=read16(fp);
    if(!board_info.width || !board_info.height) return "Board size is zero";
  } else {
    board_info.width=read8(fp)+1;
    board_info.height=read8(fp)+1;
  }
  if(ef&0x20) board_info.userdata=read16(fp);
  maxstat=read8(fp)?:1;
  // Board grid
  if(board_info.width*(unsigned long long)board_info.height>0x100000) return "Board size is too big";
  b_under=calloc(3*sizeof(Tile),tc=board_info.width*board_info.height);
  if(!b_under) err(1,"Allocation failed");
  b_main=b_under+tc;
  b_over=b_main+tc;
  end=b_over+tc;
  
  // Stats
  stats=calloc(maxstat,sizeof(Stat));
  if(!stats) err(1,"Allocation failed");
  for(i=0;i<maxstat;i++) {
    stats[i].misc1=read16(fp);
    stats[i].misc2=read16(fp);
    stats[i].misc3=read16(fp);
    if(stats[i].length=read16(fp)) {
      stats[i].text=malloc(stats[i].length+1);
      if(!stats[i].text) err(1,"Allocation failed");
      fread(stats[i].text,1,stats[i].length,fp);
      stats[i].text[stats[i].length]=0;
    } else {
      stats[i].text=0;
    }
    stats[i].speed=read8(fp);
    if(stats[i].count=read16(fp)) {
      r=stats[i].xy=calloc(stats[i].count,sizeof(StatXY));
      if(!r) err(1,"Allocation failed");
      for(j=0;j<stats[i].count;j++) {
        c=read8(fp);
        if(!j && (c&15)!=15) return "File format error";
        r[j].x=((c&3)==3?((board_info.width>256 || (ef&0x8000))?read16(fp):read8(fp)):r[j-1].x+(c&3)-1);
        r[j].y=(((c>>2)&3)==3?((board_info.height>256 || (ef&0x8000))?read16(fp):read8(fp)):r[j-1].y+((c>>2)&3)-1);
        r[j].instptr=(((c>>4)&3)==0?0:((c>>4)&3)==1?65535:((c>>4)&3)==2?r[j?j-1:0].instptr:read16(fp));
        r[j].layer=(c&0x40?read8(fp):j?r[j-1].layer:2);
        r[j].delay=(c&0x80?read8(fp):j?r[j-1].delay:0);
        c=r[j].layer&3;
        if(c && r[j].x<board_info.width && r[j].y<board_info.height) (c==1?b_under:c==2?b_main:b_over)[r[j].y*board_info.width+r[j].x].stat=i+1;
      }
    } else {
      stats[i].xy=0;
    }
  }
  // Stat grid
  if(ef&0x8000) {
    pt=b_under;
    while(pt<end) {
      pt++->stat=c=read8(fp);
      if(!c) {
        n=read16(fp);
        while(n-- && pt<end) pt++->stat=0;
      }
    }
  }
  return 0;
}

const char*save_board(FILE*fp,int m) {
  Uint16 ef=(m?0x8000:0);
  Uint16 w=board_info.width;
  Uint32 at;
  Uint32 tc=board_info.width*board_info.height;
  Tile*pt=b_under;
  Tile*end=pt+tc*3;
  int i,j;
  Uint8 c;
  StatXY*r;
  // Header
  if(board_info.flag&~255) ef|=0x100;
  if(board_info.screen&~255) ef|=0x200;
  if(board_info.exits[0]) ef|=1;
  if(board_info.exits[1]) ef|=2;
  if(board_info.exits[2]) ef|=4;
  if(board_info.exits[3]) ef|=8;
  if(board_info.width>256 || board_info.height>256) ef|=0x10;
  if(board_info.userdata) ef|=0x20;
  write16(fp,ef);
  if(ef&0x100) write16(fp,board_info.flag); else write8(fp,board_info.flag);
  if(ef&0x200) write16(fp,board_info.screen); else write8(fp,board_info.screen);
  if(ef&1) write16(fp,board_info.exits[0]);
  if(ef&2) write16(fp,board_info.exits[1]);
  if(ef&4) write16(fp,board_info.exits[2]);
  if(ef&8) write16(fp,board_info.exits[3]);
  if(ef&0x10) {
    write16(fp,board_info.width);
    write16(fp,board_info.height);
  } else {
    write8(fp,board_info.width-1);
    write8(fp,board_info.height-1);
  }
  if(ef&0x20) write16(fp,board_info.userdata);
  write8(fp,maxstat);
  // Board grid
  
  // Stats
  for(i=0;i<maxstat;i++) {
    write16(fp,stats[i].misc1);
    write16(fp,stats[i].misc2);
    write16(fp,stats[i].misc3);
    write16(fp,stats[i].length);
    if(stats[i].length) fwrite(stats[i].text,1,stats[i].length,fp);
    write8(fp,stats[i].speed);
    write16(fp,stats[i].count);
    for(j=0;j<stats[i].count;j++) {
      if(j) {
        if(r[j].x==r[j-1].x) c=1; else if(r[j].x==r[j-1].x-1) c=0; else if(r[j].x==r[j-1].x+1) c=2; else c=3;
        if(r[j].y==r[j-1].y) c+=4; else if(r[j].y==r[j-1].y-1) c+=0; else if(r[j].y==r[j-1].y+1) c+=8; else c+=12;
        if(r[j].layer!=r[j-1].layer) c|=0x40;
        if(r[j].delay!=r[j-1].delay) c|=0x80;
      } else {
        c=15;
        if(r[j].layer!=2) c|=0x40;
        if(r[j].delay) c|=0x80;
      }
      if(r[j].instptr==65535) c+=0x10; else if(j && r[j].instptr==r[j-1].instptr) c+=0x20; else if(r[j].instptr) c+=0x30;
      if((c&0x03)==0x03) (board_info.width>256 || (ef&0x8000))?write16(fp,r[j].x):write8(fp,r[j].x);
      if((c&0x0C)==0x0C) (board_info.height>256 || (ef&0x8000))?write16(fp,r[j].y):write8(fp,r[j].y);
      if((c&0x30)==0x30) write16(fp,r[j].instptr);
      if(c&0x40) write8(fp,r[j].layer);
      if(c&0x80) write8(fp,r[j].delay);
    }
  }
  // Stat grid
  if(ef&0x8000) {
    pt=b_under;
    while(pt<end) {
      write8(fp,c=pt++->stat);
      if(c) continue;
      for(i=0;i<65535 && pt<end && !pt->stat;i++,pt++);
      write16(fp,i);
    }
  }
  return 0;
}

const char*load_screen(FILE*fp) {
  Uint8 c;
  Uint32 at=0;
  int i,n;
  memset(&cur_screen,0,sizeof(Screen));
  if(fgetc(fp)) return "Unrecognized file format";
  cur_screen.flag=fgetc(fp);
  cur_screen.border_color=fgetc(fp);
  fread(cur_screen.border,1,4,fp);
  fread(cur_screen.soft_edge,1,4,fp);
  fread(cur_screen.hard_edge,1,4,fp);
  cur_screen.view_x=fgetc(fp);
  cur_screen.view_y=fgetc(fp);
  cur_screen.message_x=fgetc(fp);
  cur_screen.message_y=fgetc(fp);
  cur_screen.message_l=fgetc(fp);
  cur_screen.message_r=fgetc(fp);
  // Screen grid
  for(at=0;at<80*25;) {
    c=fgetc(fp);
    if(c<80) {
      c++;
      if(at+c>=80*25) return "Out of bounds access";
      memset(cur_screen.command+at,at?cur_screen.command[at-1]:0,c);
      memset(cur_screen.color+at,at?cur_screen.color[at-1]:0,c);
      memset(cur_screen.parameter+at,at?cur_screen.parameter[at-1]:0,c);
      at+=c;
    } else if(c<160) {
      c-=79;
      if(at<80 || at+c>=80*25) return "Out of bounds access";
      for(i=0;i<c;i++) {
        cur_screen.command[at+i]=cur_screen.command[at+i-80];
        cur_screen.color[at+i]=cur_screen.color[at+i-80];
        cur_screen.parameter[at+i]=cur_screen.parameter[at+i-80];
      }
      at+=c;
    } else if(c<240) {
      c-=159;
      if(at+c>=80*25) return "Out of bounds access";
      memset(cur_screen.command+at,fgetc(fp),c);
      memset(cur_screen.color+at,fgetc(fp),c);
      fread(cur_screen.parameter+at,1,c,fp);
      at+=c;
    } else {
      if(c<248 && !at) return "Out of bounds access";
      cur_screen.command[at]=(c&1)?fgetc(fp):(c&8)?0:cur_screen.command[at-1];
      cur_screen.color[at]=(c&2)?fgetc(fp):(c&8)?0:cur_screen.color[at-1];
      cur_screen.parameter[at]=(c&4)?fgetc(fp):(c&8)?0:cur_screen.parameter[at-1];
      at++;
    }
  }
  return 0;
}

const char*save_screen(FILE*fp) {
  Uint8*pk=cur_screen.command;
  Uint8*pc=cur_screen.color;
  Uint8*pp=cur_screen.parameter;
  Uint8 c;
  Uint32 at=0;
  Uint8 run0,run1,run2,run3,run4;
  int i,n;
  fputc(0,fp);
  fputc(cur_screen.flag,fp);
  fputc(cur_screen.border_color,fp);
  fwrite(cur_screen.border,1,4,fp);
  fwrite(cur_screen.soft_edge,1,4,fp);
  fwrite(cur_screen.hard_edge,1,4,fp);
  fputc(cur_screen.view_x,fp);
  fputc(cur_screen.view_y,fp);
  fputc(cur_screen.message_x,fp);
  fputc(cur_screen.message_y,fp);
  fputc(cur_screen.message_l,fp);
  fputc(cur_screen.message_r,fp);
  // Screen grid
  for(at=0;at<80*25;) {
    run0=run1=run2=run3=run4=0;
    for(n=0;n<80 && at+n<80*25;n++) {
      if(run0==n && at && pk[at+n]==pk[at-1] && pc[at+n]==pc[at-1] && pp[at+n]==pp[at-1]) ++run0;
      if(run1==n && at>=80 && pk[at+n]==pk[at+n-80] && pc[at+n]==pc[at+n-80] && pp[at+n]==pp[at+n-80]) ++run1;
      if(run2==n && pk[at+n]==pk[at] && pc[at+n]==pc[at]) {
        ++run2;
        if((at+n) && pp[at+n]==pp[at+n-1]) {
          if(++run3==4) run2-=4,run3=run4=0;
        } else {
          run3=0;
        }
      }
      if(run2==n+1) {
        if(at+n>=80 && pp[at+n]==pp[at+n-80]) {
          if(++run4==4) run2-=4,run3=run4=0;
        } else {
          run4=0;
        }
      }
    }
    if(run3>=run4) run2-=run3; else run2-=run4;
    if(run0>80 || run1>80 || run2>80) errx(1,"Unexpected internal error: at=%d run0=%d run1=%d run2=%d",at,run0,run1,run2);
    if(!run0 && !run1 && run2<2) {
      if(!pk[at] && !pc[at] && !pp[at]) c=0xF8;
      else if(!pk[at] && !pc[at]) c=0xFC;
      else if(!pk[at] && !pp[at]) c=0xFA;
      else if(!pc[at] && !pp[at]) c=0xF9;
      else if(at && pk[at]==pk[at-1] && pc[at]==pc[at-1]) c=0xF4;
      else if(at && pk[at]==pk[at-1] && pp[at]==pp[at-1]) c=0xF2;
      else if(at && pc[at]==pc[at-1] && pp[at]==pp[at-1]) c=0xF1;
      else if(!pk[at]) c=0xFE;
      else if(!pc[at]) c=0xFD;
      else if(!pp[at]) c=0xFB;
      else if(at && pk[at]==pk[at-1]) c=0xF6;
      else if(at && pc[at]==pc[at-1]) c=0xF5;
      else if(at && pp[at]==pp[at-1]) c=0xF3;
      else c=0xFF;
      fputc(c,fp);
      if(c&1) fputc(pk[at],fp);
      if(c&2) fputc(pc[at],fp);
      if(c&4) fputc(pp[at],fp);
      at++;
    } else if(run0 && run0>=run1) {
      fputc(run0-1,fp);
      at+=run0;
    } else if(run1) {
      fputc(run1+79,fp);
      at+=run1;
    } else {
      fputc(run2+159,fp);
      fputc(pk[at],fp);
      fputc(pc[at],fp);
      fwrite(pp+at,1,run2,fp);
      at+=run2;
    }
  }
  return 0;
}

