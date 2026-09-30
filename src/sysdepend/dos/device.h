#ifndef DOS_DEVICE_H_INCLUDED
#define DOS_DEVICE_H_INCLUDED
extern int dos_vga;
extern int dos_key_log;
extern int dos_mouse_log;
void dos_mouse_video_update_begin(void);
void dos_mouse_video_update_end(void);
int dos_graph_verify(void);
int dos_graph_restored(void);
void dos_graph_palette_report(void);
unsigned long dos_key_count(void);
#endif
