#ifndef BUILDING_COPY_CURSOR_H
#define BUILDING_COPY_CURSOR_H

#include "input/mouse.h"
#include "map/point.h"

void building_copy_cursor_start(int grid_offset);
int building_copy_cursor_is_active(void);
int building_copy_cursor_is_placing(void);
void building_copy_cursor_rotate(void);
void building_copy_cursor_mirror(void);
void building_copy_cursor_cancel(void);
int building_copy_cursor_handle_mouse(const mouse *m, const map_tile *tile);
void building_copy_cursor_update_preview(const map_tile *tile);
void building_copy_cursor_draw_preview(int x, int y, int grid_offset, float scale);

#endif // BUILDING_COPY_CURSOR_H
