/*
  Global configuration definitions
  B = boolean
  F = float
  I = integer
  S = string
  ?(name,type,default)
  These should be listed in ASCII order by name.
*/
I(audio_buffer,Uint16,0)
I(audio_volume,Uint16,12288)
I(block_color,Uint8,0x2E)
B(brd_edit_status,Uint8,1)
B(debug,Uint8,0)
I(editor_scroll_x,Uint8,16)
I(editor_scroll_y,Uint8,6)
B(full_screen,Uint8,0)
S(help,const char*,0)
I(key_repeat_delay,int,SDL_DEFAULT_REPEAT_DELAY)
I(key_repeat_interval,int,SDL_DEFAULT_REPEAT_INTERVAL)
I(mark_color,Uint8,0x71)
I(menu_x,Uint8,1)
I(menu_y,Uint8,1)
I(message_scrollback,Uint16,100)
I(message_timer,Uint16,75)
B(overwrite_stats,Uint8,0)
S(print_command,const char*,0)
B(scr_edit_status,Uint8,1)
B(show_status,Uint8,1)
I(speed,Uint16,55)
I(speed_fast,Uint16,5)
I(template_board,Uint16,0)
S(text_editor_command,const char*,0)
