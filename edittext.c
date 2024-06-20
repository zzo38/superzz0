#if 0
gcc -s -O2 -c -Wno-unused-result -std=gnu99 edittext.c `sdl-config --cflags`
exit
#endif

#include "common.h"

Uint8*text_editor(Uint8*t) {
  win_refresh();
  if(config.text_editor_command) {
    char tem[]="/tmp/XXXXXX";
    int fd=mkstemp(tem);
    off_t o;
    if(fd==-1) {
      warn("Cannot open temporary file");
      alert_text("Cannot open temporary file");
      return t;
    }
    if(t) write(fd,t,strlen(t));
    setenv("textfile",tem,1);
    system(config.text_editor_command);
    unsetenv("textfile");
    unlink(tem);
    free(t);
    o=lseek(fd,0,SEEK_END);
    if(o==-1) err(1,"Cannot seek temporary file");
    if(o>0xFFFF) o=0xFFFF;
    if(!o) {
      close(fd);
      return 0;
    }
    t=malloc(o+1);
    if(!t) err(1,"Allocation failed");
    lseek(fd,0,SEEK_SET);
    read(fd,t,o);
    close(fd);
    t[o]=0;
    return t;
  }
  //TODO
  warnx("Not implemented");
  alert_text("Text editor is not implemented");
  return t;
}

