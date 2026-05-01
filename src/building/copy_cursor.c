#include "copy_cursor.h"

#include "building/building.h"
#include "building/clone.h"
#include "building/construction.h"
#include "building/image.h"
#include "building/properties.h"
#include "building/type.h"
#include "building/variant.h"
#include "city/warning.h"
#include "core/image_group.h"
#include "graphics/color.h"
#include "graphics/image.h"
#include "graphics/window.h"
#include "map/building.h"
#include "map/grid.h"
#include "map/property.h"
#include "map/terrain.h"
#include "scenario/allowed_building.h"

#define COPY_CURSOR_MAX_ITEMS MAX_SLICE_SIZE

typedef struct {
    building_type type;
    int dx;
    int dy;
    int rotation;
} copy_cursor_item;

enum {
    COPY_CURSOR_NONE,
    COPY_CURSOR_ARMED,
    COPY_CURSOR_SELECTING,
    COPY_CURSOR_PLACING
};

static struct {
    int mode;
    int start_grid_offset;
    int end_grid_offset;
    int preview_grid_offset;
    int width;
    int height;
    int rotation;
    int mirrored;
    int ignore_next_mouse_up;
    int count;
    copy_cursor_item items[COPY_CURSOR_MAX_ITEMS];
} data;

static int item_size(building_type type)
{
    if (type == BUILDING_WAREHOUSE) {
        return 3;
    }
    return building_properties_for_type(type)->size;
}

static void mark_area(int x, int y, int width, int height)
{
    for (int yy = 0; yy < height; yy++) {
        for (int xx = 0; xx < width; xx++) {
            if (!map_grid_is_inside(x + xx, y + yy, 1)) {
                continue;
            }
            int grid_offset = map_grid_offset(x + xx, y + yy);
            map_property_mark_constructing(grid_offset);
        }
    }
}

static void selected_bounds(int *x_min, int *y_min, int *x_max, int *y_max)
{
    int x1 = map_grid_offset_to_x(data.start_grid_offset);
    int y1 = map_grid_offset_to_y(data.start_grid_offset);
    int x2 = map_grid_offset_to_x(data.end_grid_offset);
    int y2 = map_grid_offset_to_y(data.end_grid_offset);
    map_grid_start_end_to_area(x1, y1, x2, y2, x_min, y_min, x_max, y_max);
}

static int is_main_building_tile(int grid_offset)
{
    int building_id = map_building_at(grid_offset);
    if (!building_id) {
        return 0;
    }
    building *b = building_main(building_get(building_id));
    return b && b->grid_offset == grid_offset;
}

static int should_copy_tile(int grid_offset)
{
    int terrain = map_terrain_get(grid_offset);
    if (terrain & TERRAIN_BUILDING) {
        building *b = building_get(map_building_at(grid_offset));
        b = building_main(b);
        if (b && building_is_house(b->type)) {
            return 1;
        }
        return is_main_building_tile(grid_offset);
    }
    return terrain & (TERRAIN_ROAD | TERRAIN_HIGHWAY | TERRAIN_AQUEDUCT | TERRAIN_WALL | TERRAIN_GARDEN | TERRAIN_RUBBLE);
}

static void add_item(building_type type, int dx, int dy, int rotation)
{
    if (!type || data.count >= COPY_CURSOR_MAX_ITEMS || !scenario_allowed_building(type)) {
        return;
    }
    data.items[data.count].type = type;
    data.items[data.count].dx = dx;
    data.items[data.count].dy = dy;
    data.items[data.count].rotation = rotation;
    data.count++;
}

static void add_tile_items(int grid_offset, int dx, int dy)
{
    int terrain = map_terrain_get(grid_offset);
    if (terrain & TERRAIN_BUILDING) {
        building *b = building_get(map_building_at(grid_offset));
        b = building_main(b);
        if (b && building_is_house(b->type)) {
            add_item(BUILDING_HOUSE_VACANT_LOT, dx, dy, 0);
            return;
        }
    }
    if ((terrain & TERRAIN_ROAD) && map_property_is_plaza_earthquake_or_overgrown_garden(grid_offset)) {
        add_item(BUILDING_ROAD, dx, dy, 0);
        add_item(BUILDING_PLAZA, dx, dy, 0);
        return;
    }
    add_item(building_clone_type_from_grid_offset(grid_offset), dx, dy,
        building_clone_rotation_from_grid_offset(grid_offset));
}

static void capture_selection(void)
{
    int x_min, y_min, x_max, y_max;
    selected_bounds(&x_min, &y_min, &x_max, &y_max);
    data.width = x_max - x_min + 1;
    data.height = y_max - y_min + 1;
    data.count = 0;
    for (int y = y_min; y <= y_max; y++) {
        for (int x = x_min; x <= x_max; x++) {
            int grid_offset = map_grid_offset(x, y);
            if (!should_copy_tile(grid_offset)) {
                continue;
            }
            add_tile_items(grid_offset, x - x_min, y - y_min);
        }
    }
}

static void transform_item(const copy_cursor_item *item, int *dx, int *dy, int *rotation)
{
    int x = item->dx;
    int y = item->dy;
    int item_rotation = item->rotation;
    if (data.mirrored) {
        x = data.width - 1 - x;
        item_rotation = (4 - item_rotation) % 4;
    }
    switch (data.rotation) {
        case 1:
            *dx = data.height - 1 - y;
            *dy = x;
            break;
        case 2:
            *dx = data.width - 1 - x;
            *dy = data.height - 1 - y;
            break;
        case 3:
            *dx = y;
            *dy = data.width - 1 - x;
            break;
        default:
            *dx = x;
            *dy = y;
            break;
    }
    *rotation = (item_rotation + data.rotation) % 4;
}

static void mark_selection_preview(void)
{
    int x_min, y_min, x_max, y_max;
    selected_bounds(&x_min, &y_min, &x_max, &y_max);
    mark_area(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
}

static void mark_placement_preview(const map_tile *tile)
{
    int cost = 0;
    data.preview_grid_offset = tile->grid_offset;
    for (int i = 0; i < data.count; i++) {
        int dx, dy, unused_rotation;
        transform_item(&data.items[i], &dx, &dy, &unused_rotation);
        mark_area(tile->x + dx, tile->y + dy, item_size(data.items[i].type), item_size(data.items[i].type));
        cost += model_get_building(data.items[i].type)->cost;
    }
    building_construction_set_cost(cost);
}

static void place_items(const map_tile *tile)
{
    int placed = 0;
    for (int i = 0; i < data.count; i++) {
        int dx, dy, rotation;
        transform_item(&data.items[i], &dx, &dy, &rotation);
        if (!map_grid_is_inside(tile->x + dx, tile->y + dy, item_size(data.items[i].type))) {
            continue;
        }
        if (building_construction_place_exact(data.items[i].type, tile->x + dx, tile->y + dy, rotation)) {
            placed++;
        }
    }
    building_construction_clear_type();
    if (placed) {
        window_invalidate();
    } else {
        city_warning_show(WARNING_CLEAR_LAND_NEEDED, NEW_WARNING_SLOT);
    }
}

void building_copy_cursor_start(int grid_offset)
{
    if (!grid_offset) {
        return;
    }
    building_construction_cancel();
    map_property_clear_constructing_and_deleted();
    data.mode = COPY_CURSOR_ARMED;
    data.start_grid_offset = grid_offset;
    data.end_grid_offset = grid_offset;
    data.count = 0;
    data.rotation = 0;
    data.mirrored = 0;
    data.ignore_next_mouse_up = 0;
}

int building_copy_cursor_is_active(void)
{
    return data.mode != COPY_CURSOR_NONE;
}

int building_copy_cursor_is_placing(void)
{
    return data.mode == COPY_CURSOR_PLACING;
}

void building_copy_cursor_rotate(void)
{
    if (data.mode == COPY_CURSOR_PLACING) {
        data.rotation = (data.rotation + 1) % 4;
    }
}

void building_copy_cursor_mirror(void)
{
    if (data.mode == COPY_CURSOR_PLACING) {
        data.mirrored ^= 1;
    }
}

void building_copy_cursor_cancel(void)
{
    data.mode = COPY_CURSOR_NONE;
    data.count = 0;
    data.preview_grid_offset = 0;
    building_construction_set_cost(0);
    map_property_clear_constructing_and_deleted();
}

void building_copy_cursor_update_preview(const map_tile *tile)
{
    map_property_clear_constructing_and_deleted();
    building_construction_set_cost(0);
    if (data.mode == COPY_CURSOR_SELECTING) {
        mark_selection_preview();
    } else if (data.mode == COPY_CURSOR_PLACING && tile->grid_offset) {
        mark_placement_preview(tile);
    }
}

void building_copy_cursor_draw_preview(int x, int y, int grid_offset, float scale)
{
    if (data.mode == COPY_CURSOR_NONE) {
        return;
    }
    if (data.mode == COPY_CURSOR_ARMED && grid_offset == data.end_grid_offset) {
        image_blend_footprint_color(x, y, COLOR_MASK_GREEN, scale);
        image_draw_isometric_footprint(image_group(GROUP_TERRAIN_FLAT_TILE), x, y, COLOR_MASK_FOOTPRINT_GHOST, scale);
        return;
    }
    if (data.mode == COPY_CURSOR_PLACING && data.preview_grid_offset) {
        int preview_x = map_grid_offset_to_x(data.preview_grid_offset);
        int preview_y = map_grid_offset_to_y(data.preview_grid_offset);
        int drew_ghost = 0;
        for (int i = 0; i < data.count; i++) {
            int dx, dy, rotation;
            transform_item(&data.items[i], &dx, &dy, &rotation);
            if (!map_grid_is_inside(preview_x + dx, preview_y + dy, 1) ||
                grid_offset != map_grid_offset(preview_x + dx, preview_y + dy)) {
                continue;
            }
            building b = {0};
            b.type = data.items[i].type;
            b.grid_offset = grid_offset;
            if (building_variant_has_variants(b.type)) {
                b.variant = rotation % building_variant_get_number_of_variants(b.type);
            } else if (building_properties_for_type(b.type)->rotation_offset) {
                b.subtype.orientation = rotation;
            }
            int image_id = building_image_get(&b);
            if (!image_id) {
                continue;
            }
            image_draw_isometric_footprint_from_draw_tile(image_id, x, y, COLOR_MASK_BUILDING_GHOST, scale);
            image_draw_isometric_top_from_draw_tile(image_id, x, y, COLOR_MASK_BUILDING_GHOST, scale);
            drew_ghost = 1;
        }
        if (drew_ghost) {
            return;
        }
    }
    if (!map_property_is_constructing(grid_offset)) {
        return;
    }
    image_blend_footprint_color(x, y, COLOR_MASK_GREEN, scale);
    image_draw_isometric_footprint(image_group(GROUP_TERRAIN_FLAT_TILE), x, y, COLOR_MASK_FOOTPRINT_GHOST, scale);
}

int building_copy_cursor_handle_mouse(const mouse *m, const map_tile *tile)
{
    if (data.mode == COPY_CURSOR_NONE) {
        return 0;
    }
    if (m->right.went_up) {
        building_copy_cursor_cancel();
        return 1;
    }
    if (!tile->grid_offset) {
        return 1;
    }
    if (data.mode == COPY_CURSOR_ARMED) {
        data.end_grid_offset = tile->grid_offset;
    }
    if (data.mode == COPY_CURSOR_ARMED) {
        if (m->left.went_down) {
            data.mode = COPY_CURSOR_SELECTING;
            data.start_grid_offset = tile->grid_offset;
            data.end_grid_offset = tile->grid_offset;
            data.ignore_next_mouse_up = 1;
            building_copy_cursor_update_preview(tile);
        }
        return 1;
    }
    if (data.mode == COPY_CURSOR_SELECTING) {
        if (tile->grid_offset) {
            data.end_grid_offset = tile->grid_offset;
        }
        if (m->left.went_up) {
            if (data.ignore_next_mouse_up) {
                data.ignore_next_mouse_up = 0;
                building_copy_cursor_update_preview(tile);
                return 1;
            }
            capture_selection();
            if (data.count) {
                data.mode = COPY_CURSOR_PLACING;
            } else {
                building_copy_cursor_cancel();
            }
        }
        building_copy_cursor_update_preview(tile);
        return 1;
    }
    if (data.mode == COPY_CURSOR_PLACING) {
        if (m->left.went_up) {
            place_items(tile);
        }
        building_copy_cursor_update_preview(tile);
        return 1;
    }
    return 0;
}
