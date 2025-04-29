#if 0
gcc $CFLAGS -c -Wno-unused-result -std=gnu99 printer.c `sdl-config --cflags`
exit
#endif

#include "common.h"

static const Uint8 asciichars[256]={
// 0   1   2   3   4   5   6   7    8   9   A   B   C   D   E   F
  ' ',' ',' ',' ',' ',' ',' ',' ', ' ',' ',' ',' ',' ',' ',' ',' ', // 0
  '>','<',' ','!',' ',' ','_',' ', ' ',' ',' ',' ',' ',' ','^',' ', // 1
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,' ',

  'C','u','e','a','a','a','a','c', 'e','e','e','i','i','i','A','A', // 8
  'E','e','A','o','o','o','u','u', 'y','O','U','c','L','Y','P','f', // 9
  'a','i','o','u','n','N','a','o', '?','~','~','{','}','!','<','>', // A
  '#','#','#','|','+','+','+','+', '+','+','|','+','+','+','+','+', // B
  '+','+','+','+','-','+','+','+', '+','+','+','+','+','=','+','+', // C
  '+','+','+','+','+','+','+','+', '+','+','+','#','#','#','#','#', // D
  ' ',' ',' ',' ',' ',' ',' ',' ', ' ',' ',' ',' ',' ',' ',' ',' ', // E
  '=','+','>','<','|','|','/','=', ' ','.',' ',' ',' ',' ',' ',' ', // F
// 0   1   2   3   4   5   6   7    8   9   A   B   C   D   E   F
};

static FILE*printer=0;
#define PRINT_STRING(a) fwrite(a,1,sizeof(a)-1,printer)

static struct {
  Uint8 type;
  union {
    struct {
      Uint8 crlf;
      Uint8 endff;
      Uint8 charset;
    } plain;
    struct {
      Uint8 flag;
    } epson;
  };
  Uint16 lines,offset;
  Uint8 title,blanks,width,cont,center;
} parameters;

#define EPSON_AUTOINIT 0x01
#define EPSON_BELL 0x02
#define EPSON_COLOR 0x04
#define EPSON_PC 0x08
#define EPSON_CRONLY 0x10

static Uint16 curlines;

static int set_printer_type(void) {
  const char*a=config.printer_type+1;
  switch(parameters.type=*config.printer_type) {
    case '0': // Plain
      parameters.plain.crlf=3;
      parameters.plain.endff=0;
      parameters.plain.charset=0;
      parameters.width=255;
      while(*a) switch(*a++) {
        case '0': parameters.plain.charset=0; break;
        case '1': parameters.plain.charset=1; break;
        case '2': parameters.plain.charset=2; break;
        case 'C': parameters.plain.crlf=1; break;
        case 'F': parameters.plain.endff=1; break;
        case 'L': parameters.plain.crlf=2; break;
        case 'X': parameters.plain.crlf=3; break;
        default: warnx("Improper printer type option"); return 1;
      }
      break;
    case '1': // Epson
      parameters.epson.flag=0;
      parameters.width=80;
      while(*a) switch(*a++) {
        case '@': parameters.epson.flag|=EPSON_AUTOINIT; break;
        case 'B': parameters.epson.flag|=EPSON_BELL; break;
        case 'C': parameters.epson.flag|=EPSON_COLOR; break;
        case 'N': parameters.epson.flag|=EPSON_CRONLY; break;
        case 'P': parameters.epson.flag|=EPSON_PC; break;
        default: warnx("Improper printer type option"); return 1;
      }
      break;
    default:
      warnx("Unknown printer type: %c",*config.printer_type);
      return 1;
  }
  if(config.printer_option) {
    a=config.printer_option;
    while(*a) switch(*a++) {
      case ',': break;
      case 'B': parameters.blanks=strtol(a,(char**)&a,10); break;
      case 'C': parameters.center=strtol(a,(char**)&a,10); break;
      case 'L': parameters.lines=strtol(a,(char**)&a,10); break;
      case 'S': parameters.offset=strtol(a,(char**)&a,10); break;
      case 'T': parameters.title=strtol(a,(char**)&a,10); break;
      case 'W': parameters.width=strtol(a,(char**)&a,10); break;
      case 'Z': parameters.cont=strtol(a,(char**)&a,10); break;
      default: warnx("Improper printer option");
    }
  }
  return 0;
}

int lpt_begin(void) {
  FILE*fp;
  char buf[1024];
  int n;
  if(!config.printer_type || !*config.printer_type || !config.printer_command) {
    alert_text("Printer setup is not configured");
    return 0;
  }
  if(!parameters.type && set_printer_type()) {
    alert_text("Printer configuration error");
    return parameters.type=0;
  }
  printer=popen(config.printer_command,"w");
  if(!printer) {
    alert_text("Printer error");
    warn("Cannot open printer");
    return 0;
  }
  if(config.printer_begin) {
    if(fp=fopen(config.printer_begin,"r")) {
      while((n=fread(buf,1,1024,fp))>0) fwrite(buf,1,n,printer);
      fclose(fp);
    } else {
      warn("Cancelled print job; cannot open printer_begin file");
      pclose(printer);
      alert_text("Printer error");
      return 0;
    }
    if(parameters.cont) config.printer_begin=0;
  }
  switch(parameters.type) {
    case '0': // Plain
      buf[0]=(parameters.plain.crlf&1?'\r':'\n');
      buf[1]=(parameters.plain.crlf==3?'\n':0);
      buf[2]=0;
      break;
    case '1': // Epson
      if(parameters.epson.flag&EPSON_AUTOINIT) {
        if(parameters.cont) parameters.epson.flag&=~EPSON_AUTOINIT;
        PRINT_STRING(
          "\e@"  // Reset printer
          "\eR\x00"  // US-ASCII character set
          "\e6"  // Treat 128 to 159 as printable characters
        );
        if(parameters.epson.flag&EPSON_PC) PRINT_STRING(
          "\e(t\x03\x00\x01\x01\x00"  // Assign PC character set to table 1
          "\et1"  // Select character table 1
        );
      }
      if(parameters.epson.flag&EPSON_COLOR) PRINT_STRING("\er\x00");
      if(parameters.epson.flag&EPSON_BELL) fputc(0x07,printer);
      buf[0]='\r'; buf[1]=(parameters.epson.flag&EPSON_CRONLY?0:'\n'); buf[2]=0;
      break;
  }
  if(parameters.cont!=255) {
    curlines=parameters.offset;
    if(parameters.cont) parameters.cont=255;
  }
  for(n=0;n<parameters.blanks || (parameters.lines && curlines<parameters.lines);n++) fputs(buf,printer);
  if(parameters.lines && curlines>=parameters.lines) {
    fputc('\f',printer);
    curlines=0;
  }
  return 1;
}

void lpt_end(void) {
  FILE*fp;
  char buf[1024];
  int n;
  if(!printer) return;
  switch(parameters.type) {
    case '0': // Plain
      if(parameters.plain.endff && (curlines || !parameters.lines)) {
        fputc('\f',printer);
        curlines=0;
      }
      break;
    case '1': // Epson
      if(parameters.epson.flag&EPSON_BELL) fputc(0x07,printer);
      if(parameters.epson.flag&EPSON_COLOR) PRINT_STRING("\er\x00");
      break;
  }
  if(config.printer_end) {
    if(fp=fopen(config.printer_end,"r")) {
      while((n=fread(buf,1,1024,fp))>0) fwrite(buf,1,n,printer);
      fclose(fp);
    } else {
      warn("Cannot open printer_end file");
    }
  }
  pclose(printer);
}

void lpt_linefeed(void) {
  switch(parameters.type) {
    case '0': // Plain
      if(parameters.plain.crlf&1) fputc('\r',printer);
      if(parameters.plain.crlf&2) fputc('\n',printer);
      break;
    case '1': // Epson
      fputc('\r',printer);
      if(!(parameters.epson.flag&EPSON_CRONLY)) fputc('\n',printer);
      break;
  }
  if(++curlines==parameters.lines && parameters.lines) {
    fputc('\f',printer);
    curlines=0;
  }
}

static void lpt_string(const Uint8*p,int w) {
  int n;
  switch(parameters.type) {
    case '0': // Plain
      if(parameters.plain.charset==0) {
        for(n=0;n<w;n++) fputc(asciichars[p[n]]?:p[n],printer);
      } else if(parameters.plain.charset==1) {
        for(n=0;n<w;n++) fputc(p[n]>=0x20?p[n]:asciichars[p[n]],printer);
      } else {
        for(n=0;n<w;n++) fputc(p[n]==0 || p[n]==8 || p[n]=='\r' || p[n]=='\n' || p[n]=='\f' ? 32 : p[n],printer);
      }
      break;
    case '1': // Epson
      if(parameters.epson.flag&EPSON_PC) {
        PRINT_STRING("\e(^");
        fputc(w,printer);
        fputc(0,printer);
        fwrite(p,1,w,printer);
      } else {
        for(n=0;n<w;n++) fputc(asciichars[p[n]]?:p[n],printer);
      }
      break;
  }
}

void lpt_text(const Uint8*p,int w) {
  if(w>parameters.width) w=parameters.width;
  lpt_string(p,w);
  lpt_linefeed();
}

void lpt_title(const Uint8*p,int w) {
  if(!parameters.title) return;
  if(w>parameters.width) w=parameters.width;
  if(parameters.title==2) w/=2;
  switch(parameters.type) {
    case '0': // Plain
      lpt_string(p,w);
      break;
    case '1': // Epson
      if(parameters.title==2) fputc(0x0E,printer); else PRINT_STRING("\eE");
      if(parameters.epson.flag&EPSON_COLOR) {
        PRINT_STRING("\er\x05");
        lpt_string(p,w);
        PRINT_STRING("\er\x00");
      } else {
        PRINT_STRING("\e-1");
        lpt_string(p,w);
        PRINT_STRING("\e-0");
      }
      if(parameters.title==2) fputc(0x14,printer); else PRINT_STRING("\eF");
      break;
  }
  lpt_linefeed();
}

static inline void lpt_indent(int w) {
  while(w--) fputc(' ',printer);
}

void lpt_heading(const Uint8*p,int w) {
  if(w>parameters.width) w=parameters.width;
  if(w<parameters.width && parameters.center) lpt_indent((parameters.width-w)/2);
  switch(parameters.type) {
    case '1': // Epson
      if(parameters.epson.flag&EPSON_COLOR) PRINT_STRING("\er\x03");
      lpt_string(p,w);
      if(parameters.epson.flag&EPSON_COLOR) PRINT_STRING("\er\x00");
      break;
    default:
      lpt_string(p,w);
  }
  lpt_linefeed();
}

void lpt_link(const Uint8*p,int w) {
  if(w>parameters.width-2) w=parameters.width-2;
  switch(parameters.type) {
    case '0': // Plain
      if(parameters.plain.charset==2) fputc(0x10,printer); else fputc('>',printer);
      fputc(' ',printer);
      break;
    case '1': // Epson
      if(parameters.epson.flag&EPSON_COLOR) PRINT_STRING("\er\x01");
      PRINT_STRING("\eE");
      if(parameters.epson.flag&EPSON_PC) PRINT_STRING("\e(^\x02\x00> "); else PRINT_STRING("> ");
      if(parameters.epson.flag&EPSON_COLOR) PRINT_STRING("\er\x00");
      PRINT_STRING("\eF");
      break;
  }
  lpt_string(p,w);
  lpt_linefeed();
}

void lpt_script(const Uint8*p,int w) {
  const Uint8*q;
  if(!w || !p || !*p) {
    lpt_linefeed();
    return;
  }
  switch(*p) {
    case '$': lpt_heading(p+1,w-1); break;
    case ':': if(q=memchr(p+1,';',w-1)) lpt_heading(q+1,w-(q-p)-1); else lpt_linefeed(); break;
    case '!': if(q=memchr(p+1,';',w-1)) lpt_link(q+1,w-(q-p)-1); else lpt_linefeed(); break;
    default: lpt_text(p,w);
  }
}

