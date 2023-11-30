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
  fp=open_lump("MEMORY","r");
  if(!fp) return "Cannot open MEMORY lump";
  u=lump_size>>1;
  if(u>0x10000) u=0x10000;
  free(memory);
  memory=calloc(0x10000,sizeof(Uint16));
  if(!memory) err(1,"Allocation failed");
  for(i=0;i<u;i++) memory[i]=read16(fp);
  fclose(fp);
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

static const char*load_stat_text(Uint8*te,Uint16 len,FILE*fp) {
  int c,d;
  int at=0;
  while(at<len) {
    c=read8(fp);
    if(c==0) {
      te[at++]='\n';
    } else if(c>=0x20) {
      te[at++]=c;
    } else if(c==0x01) {
      te[at++]=read8(fp);
    } else if(c==0x02) {
      te[at++]=read8(fp);
      te[at++]=read8(fp);
    } else {
      if(at+c>len) return "Too long back-reference";
      d=read8(fp)+1;
      if(at-d<-7) return "Too far back-reference";
      while(at-d<0 && c--) te[at]="\n#END\n:"[at-d+7],at++;
      while(c--) te[at]=te[at-d],at++;
    }
  }
  return 0;
}

static void save_stat_text(const Uint8*te,Uint16 len,FILE*fp) {
  int c,d;
  const Uint8*p;
  int at=0;
  // TODO: Improve this, please.
  while(at<len) {
    c=te[at];
    if(at>=len-3) goto nomatch;
    if(at>2 && (p=memmem(at<256?te:te+at-256,at<256?at+2:257,te+at,3))) {
      for(d=3;d<31 && at+d<len && te[at+d]==p[d];d++);
      write8(fp,d);
      write8(fp,p-te);
      at+=d;
      continue;
    }
    nomatch:
    if(c<0x20 && c!='\n') {
      if(at<len-1 && te[at+1]<0x20) {
        write8(fp,2);
        write8(fp,c);
        write8(fp,te[++at]);
      } else {
        write8(fp,1);
        write8(fp,c);
      }
    } else {
      write8(fp,c=='\n'?0:c);
    }
    at++;
  }
}

const char*load_board(FILE*fp) {
  Uint16 ef=read16(fp);
  int c,i,n,nn;
  Uint32 at,tc;
  Tile*pt;
  Tile*end;
  Tile mru[10];
  Uint8 nmru=0;
  Tile t={0,0,0,0};
  Uint8 mk,mc,mp;
  StatXY*r;
  memset(mru,0,sizeof(mru));
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
  b_under=calloc(3*sizeof(Tile),tc=board_info.width*board_info.height);
  if(!b_under) err(1,"Allocation failed");
  b_main=b_under+tc;
  b_over=b_main+tc;
  end=b_over+tc;
  pt=b_main;
  n=nn=0;
  for(;;) {
    c=fgetc(fp);
    if(c<40) {
      switch(c) {
        case 0 ... 19: nn=(c+1)<<8; break;
        case 20 ... 29: t=mru[c-20]; mk=mc=mp=6; goto dotile;
        case 30: layer_inversion(); goto endgrid;
        case 31: pt=b_under; break;
        case 32: pt=b_over; break;
        case 33: goto endgrid;
        case 34: t.kind=fgetc(fp); t.color=fgetc(fp); t.param=fgetc(fp); t.stat=0; fill_layer(b_over,t,tc); break;
        case 38: t.param=t.stat=0; t.kind=fgetc(fp); t.color=fgetc(fp); n=1; mk=mc=mp=3; goto dotile1;
        case 39: t.stat=0; t.kind=fgetc(fp); t.color=fgetc(fp); t.param=fgetc(fp); n=1; mk=mc=mp=3; goto dotile1;
      }
      continue;
    } else {
      c-=40;
      mk=(c/36)%6; mc=(c/6)%6; mp=c%6;
    }
    dotile:
    n=read8(fp)+1+nn;
    nn=0;
#define YY(bb) ({ Tile tt=pt[-board_info.width]; tt.kind==pt[-1].kind && tt.color==pt[-1].color && tt.param==pt[-1].param; })
#define ZZ(aa,bb) t.bb=(aa==0?0:aa==1?(pt==b_under?0:pt[-1].bb):aa==3?read8(fp):aa==5?(pt<b_under+board_info.width?0:pt[-board_info.width-YY(bb)].bb):t.bb)
    ZZ(mk,kind); ZZ(mc,color); ZZ(mp,param);
#undef YY
#undef ZZ
    dotile1:
    if((mk==3 || mc==3 || mp==3) && mk!=4 && mc!=4 && mp!=4) {
      mru[nmru++]=t;
      if(nmru==10) nmru=0;
    }
    while(n--) {
      if(pt==end) return "Write past end of board grid";
#define ZZ(aa,bb) if(aa==2 && pt>=b_under-board_info.width) t.bb=pt[-board_info.width].bb; else if(aa==4) t.bb=read8(fp);
      ZZ(mk,kind); ZZ(mc,color); ZZ(mp,param);
#undef ZZ
      *pt++=t;
    }
  }
  endgrid:
  stats=calloc(maxstat,sizeof(Stat));
  if(!stats) err(1,"Allocation failed");
  for(i=0;i<maxstat;i++) {
    c=read8(fp);
    if(c&0x01) stats[i].misc1=read16(fp);
    if(c&0x02) stats[i].misc2=read16(fp);
    if(c&0x04) stats[i].misc3=read16(fp);
    n=stats[i].speed=(c>>4)&3;
    if(n==3) stats[i].speed=read8(fp);
    if(c&0x08) {
      stats[i].text=malloc((stats[i].length=read16(fp))+1);
      if(!stats[i].text) err(1,"Allocation failed");
      stats[i].text[stats[i].length]=0;
      if(load_stat_text(stats[i].text,stats[i].length,fp)) return "Error loading stat text";
    }
    // XY records
    while(c=read8(fp)) {
      r=add_statxy(i);
      if(c&3) {
        r->layer=c&0xC3;
        r->x=(board_info.width<256?read8(fp):read16(fp));
        r->y=(board_info.height<256?read8(fp):read16(fp));
        if(c&0x04) r->delay=read8(fp); else r->delay=0;
        c>>=4; c&=3;
        if(c==0) r->instptr=0;
        else if(c==1) r->instptr=65535;
        else if(c==2) r->instptr=read8(fp);
        else if(c==3) r->instptr=read16(fp);
      } else if(c&4) {
        c>>=3;
        if(r==stats[i].xy) return "Copy of nonexistent stat XY record";
        *r=r[-1];
        if(c==18 && board_info.width<256 && board_info.height<256) {
          c=read8(fp);
          n=(c&15)-8; r->x+=n+(c&8?-1:2);
          n=(c>>4)-8; r->y+=n+(c&8?-1:2);
        } else {
          if(c%5==0) r->x--; else if(c%5==2) r->x++; else if(c%5==3) r->x+=read8(fp)-127; else if(c%5==4) r->x=(board_info.width<256?read8(fp):read16(fp));
          if(c/5==0) r->y--; else if(c/5==2) r->y++; else if(c/5==3) r->y+=read8(fp)-127; else if(c/5==4) r->y=(board_info.height<256?read8(fp):read16(fp));
        }
      } else {
        c>>=3;
        if(r==stats[i].xy) return "Copy of nonexistent stat XY record";
        *r=r[-1];
        pt=((r->layer&3)==1?b_under:(r->layer&3)==2?b_main:b_over);
        while(c--) {
          at=r->y*board_info.width+r->x;
          t=pt[at];
          while(at<tc) {
            at++;
            if(pt[at].kind==t.kind && pt[at].color==t.color && pt[at].param==t.param && !pt[at].stat) break;
          }
          if(at==tc) return "Stat XY cannot find matching tile in same layer";
          r->x=at%board_info.width;
          r->y=at/board_info.width;
          if(c) {
            pt[at].stat=i+1;
            r=add_statxy(i),*r=r[-1];
          }
        }
      }
      if(r->x>=board_info.width || r->y>=board_info.height) return "Stat coordinates out of range";
      ((r->layer&3)==1?b_under:(r->layer&3)==2?b_main:b_over)[r->y*board_info.width+r->x].stat=i+1;
    }
  }
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

static int find_hetero(const Tile*pt,Uint32 at,int m,int a,int w,Uint32 tc) {
  int n=0;
  while(n<m) {
    if(at+n+2<tc && pt[at+n].values[a]==pt[at+n+1].values[a] && pt[at+n].values[a]==pt[at+n+2].values[a]) break;
    if(at+n>=w && pt[at+n].values[a]==pt[at+n-w].values[a]) {
      if(at+n+2<tc && pt[at+n+1].values[a]==pt[at+n+1-w].values[a] && pt[at+n+2].values[a]==pt[at+n+2-w].values[a]) break;
    }
    n++;
  }
  if(n==2 && pt[at].values[a]==pt[at+1].values[a]) return 0;
  if(n==2 && at>=w && pt[at].values[a]==pt[at-w].values[a] && pt[at+1].values[a]==pt[at+1-w].values[a]) return 0;
  return n;
}

const char*save_board(FILE*fp,int m) {
  Uint16 ef=0;
  Uint16 w=board_info.width;
  int i,j,k,n,xk,yk;
  Uint32 at;
  Uint32 tc=board_info.width*board_info.height;
  Tile*pt;
  Tile*end=b_under+3*tc;
  Tile mru[10];
  Uint8 nmru=0;
  Uint8 hmru=0;
  Tile t={0,0,0,0};
  Uint8 mk,mc,mp;
  int nk,nka,nc,nca,np,npa;
  Uint16 xf=0;
  StatXY*r;
  Stat*s;
  // Header
  if(m) ef|=0x8000;
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
  pt=b_main; at=0;
  for(;;) {
    next:
    if(at==tc) {
      at=0;
      if(pt==b_over) {
        write8(fp,33);
        break;
      }
      if(pt==b_main) {
        t=*(pt=b_under);
        if(!t.kind && !t.color && !t.param) {
          for(i=1;i<tc;i++) if(pt[i].kind || pt[i].color || pt[i].param) {
            write8(fp,31);
            goto next;
          }
        }
      }
      t=*(pt=b_over);
      for(i=1;i<tc;i++) if(pt[i].kind!=t.kind || pt[i].color!=t.color || pt[i].param!=t.param) {
        write8(fp,32);
        goto next;
      }
      if(t.kind || t.color || t.param) {
        write8(fp,34);
        write8(fp,t.kind);
        write8(fp,t.color);
        write8(fp,t.param);
      }
      write8(fp,33);
      break;
    }
    // Figure out homogeneous/copy-above run
    n=1; nk=nc=np=1; nka=nca=npa=0;
    if(at>=w && pt[at].kind==pt[at-w].kind) nka=1;
    if(at>=w && pt[at].color==pt[at-w].color) nca=1;
    if(at>=w && pt[at].param==pt[at-w].param) npa=1;
    while(at+n<tc && n<5375 && (nk==n || nc==n || np==n || nka==n || nca==n || npa==n)) {
      if(nk==n && pt[at].kind==pt[at+n].kind) nk++;
      if(nc==n && pt[at].color==pt[at+n].color) nc++;
      if(np==n && pt[at].param==pt[at+n].param) np++;
      if(nka==n && pt[at+n-w].kind==pt[at+n].kind) nka++;
      if(nca==n && pt[at+n-w].color==pt[at+n].color) nca++;
      if(npa==n && pt[at+n-w].param==pt[at+n].param) npa++;
      n++;
    }
    if(nk<=nka) nk=nka,mk=2; else if(!pt[at].kind) mk=0; else if(at && pt[at].kind==pt[at-1].kind) mk=1; else mk=3;
    if(mk==3 && at>=w && pt[at-w].kind==pt[at].kind) mk=5;
    if(nc<=nca) nc=nca,mc=2; else if(!pt[at].color) mc=0; else if(at && pt[at].color==pt[at-1].color) mc=1; else mc=3;
    if(mc==3 && at>=w && pt[at-w].color==pt[at].color) mc=5;
    if(np<=npa) np=npa,mp=2; else if(!pt[at].param) mp=0; else if(at && pt[at].param==pt[at-1].param) mp=1; else mp=3;
    if(mp==3 && at>=w && pt[at-w].param==pt[at].param) mp=5;
    if(at+n==tc && nk==n && nc==n && np==n && !mk && !mc && !mp) {
      at=tc;
      goto next;
    }
    // Find MRU
    if(hmru && ((nk==1 && nc==1 && np==1) || (mk!=2 && mc!=2 && mp!=2)) && (mk==3 || mc==3 || mp==3)) {
      for(i=0;i<hmru;i++) if(mru[i].kind==pt[at].kind && mru[i].color==pt[at].color && mru[i].param==pt[at].param) {
        if(nk<n) n=nk;
        if(nc<n) n=nc;
        if(np<n) n=np;
        if(n>256) write8(fp,(n-1)>>8);
        write8(fp,i+20);
        write8(fp,n-1);
        at+=n;
        goto next;
      }
    }
    // Figure out heterogeneous run
    nka=nca=npa=0; n=5375;
    if(nk>2 && nk<n) n=nk;
    if(nc>2 && nc<n) n=nc;
    if(np>2 && np<n) n=np;
    if(n>tc-at) n=tc-at;
    if(nk<3 && (nka=find_hetero(pt,at,n,0,w,tc))>1) mk=4,n=nk=nka;
    if(nc<3 && (nca=find_hetero(pt,at,n,1,w,tc))>1) mc=4,n=nc=nca;
    if(np<3 && (npa=find_hetero(pt,at,n,2,w,tc))>1) mp=4,n=np=npa;
    if(nk<n) n=nk;
    if(nc<n) n=nc;
    if(np<n) n=np;
    // Encode
    if(n==1 && mk==3 && mc==3) {
      write8(fp,pt[at].param?39:38);
      write8(fp,pt[at].kind);
      write8(fp,pt[at].color);
      if(pt[at].param) write8(fp,pt[at].param);
      mp=3;
    } else {
      if(n>256) write8(fp,(n-1)>>8);
      write8(fp,40+(mk*36+mc*6+mp));
      write8(fp,n-1);
      if(mk==3) write8(fp,pt[at].kind);
      if(mc==3) write8(fp,pt[at].color);
      if(mp==3) write8(fp,pt[at].param);
      if(mk==4 || mc==4 || mp==4) {
        for(i=0;i<n;i++) {
          if(mk==4) write8(fp,pt[at+i].kind);
          if(mc==4) write8(fp,pt[at+i].color);
          if(mp==4) write8(fp,pt[at+i].param);
        }
      }
    }
    if((mk==3 || mc==3 || mp==3) && mk!=4 && mc!=4 && mp!=4) {
      mru[nmru++]=pt[at];
      if(nmru==10) nmru=0;
      if(hmru<10) hmru=nmru;
    }
    at+=n;
  }
  // Stats
  for(i=0;i<maxstat;i++) {
    s=stats+i;
    write8(fp,j=(s->misc1?1:0)|(s->misc2?2:0)|(s->misc3?4:0)|(s->length?8:0)|(s->speed<3?s->speed<<4:0x30));
    if(j&1) write16(fp,s->misc1);
    if(j&2) write16(fp,s->misc2);
    if(j&4) write16(fp,s->misc3);
    if(s->speed>=3) write8(fp,s->speed);
    if(j&8) {
      write16(fp,s->length);
      save_stat_text(s->text,s->length,fp);
    }
    for(j=0;j<s->count;j++) {
      r=s->xy+j;
      if(j && r[-1].layer==r->layer && r[-1].delay==r->delay && r[-1].instptr==r->instptr) {
        // TODO: run of matching tiles of stats
        if(board_info.width<256 && board_info.height<256) k=10; else k=127;
        if(abs(r->x-r[-1].x-1)<2) xk=r->x+1-r[-1].x; else if(abs(r->x-r[-1].x)<127) xk=3; else xk=4;
        if(abs(r->y-r[-1].y-1)<2) yk=r->y+1-r[-1].y; else if(abs(r->y-r[-1].y)<127) yk=3; else yk=4;
        k=yk*5+xk;
        write8(fp,(k<<3)+4);
        if(k==18 && board_info.width<256 && board_info.height<256) {
          write8(fp,(r->x+(r->x<r[-1].x?9:6)-r[-1].x)|((r->y+(r->y<r[-1].y?9:6)-r[-1].y)<<4));
        } else {
          if(xk==3) write8(fp,r->x+127-r[-1].x); else if(xk==4 && board_info.width<256) write8(fp,r->x); else if(xk==4) write16(fp,r->x);
          if(yk==3) write8(fp,r->y+127-r[-1].y); else if(yk==4 && board_info.width<256) write8(fp,r->y); else if(yk==4) write16(fp,r->y);
        }
      } else {
        k=r->layer&0xC3;
        if(r->delay) k|=0x04;
        if(r->instptr==65535) k|=0x10; else if(r->instptr>0 && r->instptr<256) k|=0x20; else if(r->instptr) k|=0x30;
        write8(fp,k);
        if(board_info.width<256) write8(fp,r->x); else write16(fp,r->x);
        if(board_info.height<256) write8(fp,r->y); else write16(fp,r->y);
        if(k&0x04) write8(fp,r->delay);
        if(k&0x20) {
          if(k&0x10) write16(fp,r->instptr); else write8(fp,r->instptr);
        }
      }
    }
    write8(fp,0);
  }
  // Stat numbers in grid
  if(ef&0x8000) {
    pt=b_under;
    while(pt<end) {
      write8(fp,m=pt++->stat);
      if(!m) {
        while(pt<end && !pt->stat) pt++,m++;
        write16(fp,m);
      }
    }
  }
  return 0;
}

const char*load_screen(FILE*fp) {
  Tile mru[10];
  Tile t;
  Uint8 nmru=0;
  Uint8 mk,mc,mp;
  Uint32 at=0;
  int c,i,n,nn;
  memset(&cur_screen,0,sizeof(Screen));
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
  n=nn=0;
  while(at<80*25) {
    c=fgetc(fp);
    if(c==EOF) break;
    if(c<40) {
      switch(c) {
        case 0 ... 19: nn=(c+1)<<8; break;
        case 20 ... 29: t=mru[c-20]; mk=mc=mp=6; goto dotile;
        case 30 ... 33:
          i=(c<32?1:80);
          if(at<i) return "Invalid copy code";
          n=read8(fp)+1+nn; nn=0;
          while(n-- && at<80*25) {
            cur_screen.command[at]=cur_screen.command[at-i];
            cur_screen.color[at]=cur_screen.color[at-i];
            cur_screen.parameter[at]=cur_screen.parameter[at-i]+(c&1?1:-1);
            at++;
          }
          break;
        case 34: t.color=fgetc(fp); t.kind=0x03; mk=mc=3; mp=4; goto dotile;
        case 39: t.kind=fgetc(fp); t.color=fgetc(fp); t.param=fgetc(fp); n=1; mk=mc=mp=3; goto dotile1;
      }
      continue;
    } else {
      c-=40;
      mk=(c/36)%6; mc=(c/6)%6; mp=c%6;
    }
    dotile:
    n=read8(fp)+1+nn;
    nn=0;
#define YY(cc) ({ cur_screen.command[at-1]==cur_screen.command[at-80] && cur_screen.color[at-1]==cur_screen.color[at-80] && cur_screen.parameter[at-1]==cur_screen.parameter[at-80]; })
#define ZZ(aa,bb,cc) t.bb=(aa==0?0:aa==1?(at?cc[at-1]:0):aa==3?read8(fp):aa==5?(at<80?0:cc[at-80-YY(cc)]):t.bb)
    ZZ(mk,kind,cur_screen.command); ZZ(mc,color,cur_screen.color); ZZ(mc,param,cur_screen.parameter);
#undef YY
#undef ZZ
    dotile1:
    if((mk==3 || mc==3 || mp==3) && mk!=4 && mc!=4 && mp!=4) {
      mru[nmru++]=t;
      if(nmru==10) nmru=0;
    }
    while(n-- && at<80*25) {
#define ZZ(aa,bb,cc) if(aa==2 && at>=80) t.bb=cc[at-80]; else if(aa==4) t.bb=read8(fp); cc[at]=t.bb;
      ZZ(mk,kind,cur_screen.command); ZZ(mc,color,cur_screen.color); ZZ(mc,param,cur_screen.parameter);
#undef ZZ
      at++;
    }
  }
  return 0;
}

