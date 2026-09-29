#ifndef DOS_DEVICE_H_INCLUDED
#define DOS_DEVICE_H_INCLUDED
extern int dos_vga;
extern int dos_key_log;
int dos_graph_verify(void);
int dos_graph_restored(void);
unsigned long dos_key_count(void);
#endif
