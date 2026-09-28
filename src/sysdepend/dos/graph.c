/* Memory-only graphics backend. Never switches the DOS video mode. */
#include <stdlib.h>
#include <string.h>
#include "quasi88.h"
#include "graph.h"

static T_GRAPH_SPEC spec;
static T_GRAPH_INFO info;

const T_GRAPH_SPEC *graph_init(void)
{
    memset(&spec, 0, sizeof(spec));
    spec.window_max_width = 640;
    spec.window_max_height = 480;
    return &spec;
}
const T_GRAPH_INFO *graph_setup(int width, int height, int fullscreen, double aspect)
{
    void *buffer;
    (void)fullscreen; (void)aspect;
    if (width <= 0 || width > 640 || height <= 0 || height > 480) return NULL;
    buffer = calloc((size_t)width * height, 2);
    if (!buffer) return NULL;
    free(info.buffer);
    memset(&info, 0, sizeof(info));
    info.buffer = buffer;
    info.width = width;
    info.height = height;
    info.byte_per_pixel = 2;
    info.byte_per_line = width * 2;
    info.nr_color = 65536;
    return &info;
}
void graph_exit(void) { free(info.buffer); memset(&info, 0, sizeof(info)); }
void graph_add_color(const PC88_PALETTE_T colors[], int count, unsigned long pixels[])
{
    int i;
    for (i = 0; i < count; ++i)
        pixels[i] = ((unsigned long)(colors[i].red >> 3) << 11) |
                    ((unsigned long)(colors[i].green >> 2) << 5) |
                    (colors[i].blue >> 3);
}
void graph_remove_color(int count, unsigned long pixels[]) { (void)count; (void)pixels; }
void graph_update(int count, T_GRAPH_RECT rect[]) { (void)count; (void)rect; }
void graph_set_window_title(const char *title) { (void)title; }
void graph_set_attribute(int mouse, int grab, int repeat, int *show, int *result_grab)
{
    (void)mouse; (void)grab; (void)repeat;
    *show = FALSE;
    *result_grab = FALSE;
}
