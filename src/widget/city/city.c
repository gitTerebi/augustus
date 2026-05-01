#include "city.h"

#include "building/construction.h"
#include "building/copy_cursor.h"
#include "building/properties.h"
#include "building/rotation.h"
#include "city/finance.h"
#include "city/view.h"
#include "city/warning.h"
#include "core/calc.h"
#include "core/config.h"
#include "core/direction.h"
#include "core/image_group.h"
#include "core/lang.h"
#include "core/string.h"
#include "figure/formation.h"
#include "figure/formation_legion.h"
#include "figure/roamer_preview.h"
#include "game/cheats.h"
#include "game/settings.h"
#include "game/state.h"
#include "graphics/button.h"
#include "graphics/graphics.h"
#include "graphics/menu.h"
#include "graphics/image.h"
#include "graphics/panel.h"
#include "graphics/text.h"
#include "graphics/video.h"
#include "graphics/window.h"
#include "input/scroll.h"
#include "input/zoom.h"
#include "input/touch.h"
#include "map/building.h"
#include "map/grid.h"
#include "map/routing.h"
#include "scenario/property.h"
#include "sound/city.h"
#include "sound/speech.h"
#include "sound/effect.h"
#include "translation/translation.h"
#include "widget/city/draw.h"
#include "widget/city/overlay/overlay.h"
#include "widget/minimap.h"
#include "widget/sidebar/extra.h"
#include "window/building_info.h"
#include "window/city.h"
#include "window/pause_menu.h"

#define NO_POSITION ((unsigned int)-1)

static struct {
    map_tile current_tile;
    map_tile selected_tile;
    map_tile legion_command_tile;
    int selected_building_id;
    int new_start_grid_offset;
    int capture_input;
    int routing_grid_offset;
    int legion_command_dragging;
    int legion_command_start_x;
    int legion_command_start_y;
    int legion_command_current_x;
    int legion_command_current_y;
} data;

void set_city_clip_rectangle(void)
{
    int x, y, width, height;
    city_view_get_viewport(&x, &y, &width, &height);
    graphics_set_clip_rectangle(x, y, width, height);
}

static void display_zoom_warning(int zoom)
{
    static uint8_t zoom_string[100];
    static int warning_id;
    if (!*zoom_string) {
        uint8_t *cursor = string_copy(lang_get_string(CUSTOM_TRANSLATION, TR_ZOOM), zoom_string, 100);
        string_copy(string_from_ascii(" "), cursor, (int) (cursor - zoom_string));
    }
    int position = string_length(lang_get_string(CUSTOM_TRANSLATION, TR_ZOOM)) + 1;
    position += string_from_int(zoom_string + position, zoom, 0);
    string_copy(string_from_ascii("%"), zoom_string + position, 100 - position);
    warning_id = city_warning_show_custom(zoom_string, warning_id);
}

static void update_zoom_level(void)
{
    int zoom = city_view_get_scale();
    pixel_offset offset;
    city_view_get_camera_in_pixels(&offset.x, &offset.y);
    if (zoom_update_value(&zoom, city_view_get_max_scale(), &offset)) {
        city_view_set_scale(zoom);
        city_view_set_camera_from_pixel_position(offset.x, offset.y);
        display_zoom_warning(zoom);
        sound_city_decay_views();
    }
}

void widget_city_draw(void)
{
    update_zoom_level();
    set_city_clip_rectangle();
    city_draw(0, 0, &data.current_tile, data.selected_building_id);
    if (data.legion_command_dragging) {
        int x1 = data.legion_command_start_x;
        int y1 = data.legion_command_start_y;
        int x2 = data.legion_command_current_x;
        int y2 = data.legion_command_current_y;
        graphics_draw_line(x1 + 1, x2 + 1, y1 + 1, y2 + 1, COLOR_BLACK);
        graphics_draw_line(x1, x2, y1, y2, COLOR_FONT_YELLOW);
        int dx = x2 - x1;
        int dy = y2 - y1;
        if (dx || dy) {
            int head_x = dx > 0 ? -8 : dx < 0 ? 8 : 0;
            int head_y = dy > 0 ? -8 : dy < 0 ? 8 : 0;
            int side_x = dy > 0 ? -4 : dy < 0 ? 4 : 0;
            int side_y = dx > 0 ? 4 : dx < 0 ? -4 : 0;
            graphics_draw_line(x2 + 1, x2 + head_x + side_x + 1,
                y2 + 1, y2 + head_y + side_y + 1, COLOR_BLACK);
            graphics_draw_line(x2 + 1, x2 + head_x - side_x + 1,
                y2 + 1, y2 + head_y - side_y + 1, COLOR_BLACK);
            graphics_draw_line(x2, x2 + head_x + side_x, y2, y2 + head_y + side_y, COLOR_FONT_YELLOW);
            graphics_draw_line(x2, x2 + head_x - side_x, y2, y2 + head_y - side_y, COLOR_FONT_YELLOW);
        }
    }
    graphics_reset_clip_rectangle();
}

void widget_city_draw_for_figure(int figure_id, pixel_coordinate *coord)
{
    set_city_clip_rectangle();

    city_draw(figure_id, coord, &data.current_tile, 0);

    graphics_reset_clip_rectangle();
}

void widget_city_draw_construction_cost_and_size(void)
{
    if (scroll_in_progress()) {
        return;
    }
    int size_x, size_y;
    int cost = building_construction_cost();
    int has_size = building_construction_size(&size_x, &size_y);
    if (!cost && !has_size) {
        return;
    }
    set_city_clip_rectangle();
    int x, y;
    city_view_get_selected_tile_pixels(&x, &y);
    int inverted_scale = calc_percentage(100, city_view_get_scale());
    x = calc_adjust_with_percentage(x, inverted_scale);
    y = calc_adjust_with_percentage(y, inverted_scale);

    if (cost) {
        color_t color;
        if (cost <= city_finance_treasury()) {
            // Color blind friendly
            color = scenario_property_climate() == CLIMATE_DESERT ? COLOR_FONT_ORANGE : COLOR_FONT_ORANGE_LIGHT;
        } else {
            color = COLOR_FONT_RED;
        }
        text_draw_number(cost, '@', " ", x + 58 + 1, y + 1, FONT_NORMAL_PLAIN, COLOR_BLACK);
        text_draw_number(cost, '@', " ", x + 58, y, FONT_NORMAL_PLAIN, color);
    }
    if (has_size) {
        int width = -text_get_width(string_from_ascii("  "), FONT_SMALL_PLAIN);
        width += text_draw_number(size_x, '@', "x", x - 15 + 1, y + 25 + 1, FONT_SMALL_PLAIN, COLOR_BLACK);
        text_draw_number(size_x, '@', "x", x - 15, y + 25, FONT_SMALL_PLAIN, COLOR_FONT_YELLOW);
        text_draw_number(size_y, '@', " ", x - 15 + width + 1, y + 25 + 1, FONT_SMALL_PLAIN, COLOR_BLACK);
        text_draw_number(size_y, '@', " ", x - 15 + width, y + 25, FONT_SMALL_PLAIN, COLOR_FONT_YELLOW);
    }
    graphics_reset_clip_rectangle();
}

static void draw_pause_icon(int x_offset, int y_offset)
{
    graphics_draw_line(x_offset + 3, x_offset + 11, y_offset + 3, y_offset + 3, COLOR_BLACK);
    graphics_draw_line(x_offset + 3, x_offset + 3, y_offset + 4, y_offset + 16, COLOR_BLACK);
    graphics_fill_rect(x_offset + 4, y_offset + 4, 8, 13, COLOR_WHITE);

    x_offset += 13;

    graphics_draw_line(x_offset + 3, x_offset + 11, y_offset + 3, y_offset + 3, COLOR_BLACK);
    graphics_draw_line(x_offset + 3, x_offset + 3, y_offset + 4, y_offset + 16, COLOR_BLACK);
    graphics_fill_rect(x_offset + 4, y_offset + 4, 8, 13, COLOR_WHITE);
}

static void draw_pause_button(void)
{
    if (!mouse_get()->is_touch) {
        return;
    }
    inner_panel_draw(16, 40, 3, 2);
    button_border_draw(16, 40, 3 * BLOCK_SIZE, 2 * BLOCK_SIZE, 0);
    if (game_state_is_paused()) {
        image_draw(image_group(GROUP_MESSAGE_ICON), 26, 46, COLOR_MASK_NONE, SCALE_NONE);
    } else {
        draw_pause_icon(26, 46);
    }
}

static void draw_construction_buttons(void)
{
    if (!building_construction_type()) {
        return;
    }
    int x, y, width, height;

    city_view_get_viewport(&x, &y, &width, &height);
    int x_offset = width - 4 * BLOCK_SIZE;
    int y_offset = 40;
    if (((sidebar_extra_is_information_displayed(SIDEBAR_EXTRA_DISPLAY_GAME_SPEED) || !mouse_get()->is_touch) && width < 680)
            && game_state_is_paused()) {
        y_offset = 100;
    }
    if (mouse_get()->is_touch) {
        inner_panel_draw(x_offset, y_offset, 3, 2);
        button_border_draw(x_offset, y_offset, 3 * BLOCK_SIZE, 2 * BLOCK_SIZE, 0);
        // Use clip rectangle to remove the border of the "X" image
        graphics_set_clip_rectangle(x_offset + 5, y_offset + 5, 37, 24);
        image_draw(image_group(GROUP_OK_CANCEL_SCROLL_BUTTONS) + 4, x_offset + 4, y_offset + 4,
            COLOR_MASK_NONE, SCALE_NONE);
        graphics_reset_clip_rectangle();
    }

    if ((mouse_get()->is_touch || config_get(CONFIG_UI_ALWAYS_SHOW_ROTATION_BUTTONS)) &&
        building_construction_can_rotate()) {
        if (!sidebar_extra_is_information_displayed(SIDEBAR_EXTRA_DISPLAY_GAME_SPEED)) {
            x_offset = 4 * BLOCK_SIZE + 8;
        } else {
            x_offset = 16;
        }
        inner_panel_draw(x_offset, y_offset, 3, 2);
        button_border_draw(x_offset, y_offset, 3 * BLOCK_SIZE, 2 * BLOCK_SIZE, 0);
        graphics_set_clip_rectangle(x_offset + 8, y_offset + 6, 37, 24);
        image_draw(image_group(GROUP_SIDEBAR_BRIEFING_ROTATE_BUTTONS) + 6, x_offset + 7, y_offset + 5,
            COLOR_MASK_NONE, SCALE_NONE);
        graphics_reset_clip_rectangle();

        x_offset += 3 * BLOCK_SIZE + 8;
        inner_panel_draw(x_offset, y_offset, 3, 2);
        button_border_draw(x_offset, y_offset, 3 * BLOCK_SIZE, 2 * BLOCK_SIZE, 0);
        graphics_set_clip_rectangle(x_offset + 8, y_offset + 6, 37, 24);
        image_draw(image_group(GROUP_SIDEBAR_BRIEFING_ROTATE_BUTTONS) + 9, x_offset + 7, y_offset + 5,
            COLOR_MASK_NONE, SCALE_NONE);
        graphics_reset_clip_rectangle();
    }
}

void widget_city_draw_construction_buttons(void)
{
    if (!sidebar_extra_is_information_displayed(SIDEBAR_EXTRA_DISPLAY_GAME_SPEED)) {
        draw_pause_button();
    }
    draw_construction_buttons();
}

static int is_pause_button(int x, int y)
{
    return !sidebar_extra_is_information_displayed(SIDEBAR_EXTRA_DISPLAY_GAME_SPEED) &&
        x < 4 * BLOCK_SIZE + 4 && y >= 24 && y < 56 + 4 * BLOCK_SIZE;
}

static int is_cancel_construction_button(int x, int y)
{
    int city_x, city_y, width, height;
    city_view_get_viewport(&city_x, &city_y, &width, &height);

    int touch_width = 5 * BLOCK_SIZE;
    int touch_height = 22 + 4 * BLOCK_SIZE;
    int x_offset = width - touch_width;
    int y_offset = (sidebar_extra_is_information_displayed(SIDEBAR_EXTRA_DISPLAY_GAME_SPEED) && width < 680
            && game_state_is_paused()) ? 84 : 24;

    return x >= x_offset && x < x_offset + touch_width && y >= y_offset && y < y_offset + touch_height;
}

static int is_rotate_backward_button(int x, int y)
{
    int city_x, city_y, width, height;
    city_view_get_viewport(&city_x, &city_y, &width, &height);

    int sidebar_pause_button = sidebar_extra_is_information_displayed(SIDEBAR_EXTRA_DISPLAY_GAME_SPEED);
    int x_offset = sidebar_pause_button ? 16 : 4 * BLOCK_SIZE + 4;
    int y_offset = sidebar_pause_button && width < 680 && game_state_is_paused() ? 84 : 24;
    return x >= x_offset && x < x_offset + 3 * BLOCK_SIZE &&
        y >= y_offset && y < y_offset + 4 * BLOCK_SIZE;
}

static int is_rotate_forward_button(int x, int y)
{
    int city_x, city_y, width, height;
    city_view_get_viewport(&city_x, &city_y, &width, &height);

    int sidebar_pause_button = sidebar_extra_is_information_displayed(SIDEBAR_EXTRA_DISPLAY_GAME_SPEED);
    int x_offset = sidebar_pause_button ? 4 * BLOCK_SIZE + 4 : 7 * BLOCK_SIZE + 4;
    int y_offset = sidebar_pause_button && width < 680 && game_state_is_paused() ? 84 : 24;
    return x >= x_offset && x < x_offset + 3 * BLOCK_SIZE &&
        y >= y_offset && y < y_offset + 4 * BLOCK_SIZE;
}

// INPUT HANDLING

static void update_city_view_coords(int x, int y, map_tile *tile)
{
    view_tile view;
    if (city_view_pixels_to_view_tile(x, y, &view)) {
        tile->grid_offset = city_view_tile_to_grid_offset(&view);
        city_view_set_selected_view_tile(&view);
        tile->x = map_grid_offset_to_x(tile->grid_offset);
        tile->y = map_grid_offset_to_y(tile->grid_offset);
    } else {
        tile->grid_offset = tile->x = tile->y = 0;
    }
}

static int handle_right_click_allow_building_info(const map_tile *tile)
{
    int allow = 1;
    if (!window_is(WINDOW_CITY)) {
        allow = 0;
    }
    window_city_show();

    if (!tile->grid_offset) {
        allow = 0;
    }
    if (allow && city_has_warnings()) {
        city_warning_clear_all();
        if (config_get(CONFIG_UI_CLEAR_WARNINGS_RIGHTCLICK)) {
            allow = 0;
        } else {
            allow = 1;
        }
    }
    return allow;
}

static int handle_legion_click(const map_tile *tile)
{
    if (tile->grid_offset) {
        int formation_id = formation_legion_at_grid_offset(tile->grid_offset);
        if (formation_id > 0 && !formation_get(formation_id)->in_distant_battle) {
            window_city_military_show(formation_id);
            return 1;
        }
    }
    return 0;
}

static void build_start(const map_tile *tile)
{
    if (tile->grid_offset) { // Allow building on paused
        building_construction_start(tile->x, tile->y, tile->grid_offset);
    }
}

static void build_move(const map_tile *tile)
{
    if (!building_construction_in_progress()) {
        return;
    }
    building_construction_update(tile->x, tile->y, tile->grid_offset);
}

static void build_end(void)
{
    if (building_construction_in_progress()) {
        if (building_construction_type() != BUILDING_NONE) {
            sound_effect_play(SOUND_EFFECT_BUILD);
        }
        building_construction_place();
        widget_minimap_invalidate();
    }
}

static void scroll_map(const mouse *m)
{
    pixel_offset delta;
    if (scroll_get_delta(m, &delta, SCROLL_TYPE_CITY)) {
        city_view_scroll(delta.x, delta.y);
        sound_city_decay_views();
    }
}

static int adjust_offset_for_orientation(int grid_offset, int size)
{
    switch (city_view_orientation()) {
        case DIR_0_TOP:
            return map_grid_add_delta(grid_offset, -size + 1, -size + 1);
        case DIR_2_RIGHT:
            return map_grid_add_delta(grid_offset, 0, -size + 1);
        case DIR_6_LEFT:
            return map_grid_add_delta(grid_offset, -size + 1, 0);
        default:
            return grid_offset;
    }
}

static int has_confirmed_construction(int ghost_offset, int tile_offset, int range_size)
{
    tile_offset = adjust_offset_for_orientation(tile_offset, range_size);
    int x = map_grid_offset_to_x(tile_offset);
    int y = map_grid_offset_to_y(tile_offset);
    if (ghost_offset <= 0 || !map_grid_is_inside(x, y, range_size)) {
        return 0;
    }
    for (int dy = 0; dy < range_size; dy++) {
        for (int dx = 0; dx < range_size; dx++) {
            if (ghost_offset == tile_offset + map_grid_delta(dx, dy)) {
                return 1;
            }
        }
    }
    return 0;
}

static int input_coords_in_city(int x, int y)
{
    if (is_pause_button(x, y) || is_cancel_construction_button(x, y) ||
        is_rotate_forward_button(x, y) || is_rotate_backward_button(x, y)) {
        return 0;
    }
    int x_offset, y_offset, width, height;
    city_view_get_viewport(&x_offset, &y_offset, &width, &height);

    x -= x_offset;
    y -= y_offset;

    return (x >= 0 && x < width && y >= 0 && y < height);
}

static void handle_touch_scroll(const touch *t)
{
    if (building_construction_type()) {
        if (t->has_started) {
            int x_offset, y_offset, width, height;
            city_view_get_viewport(&x_offset, &y_offset, &width, &height);
            scroll_set_custom_margins(x_offset, y_offset, width, height);
        }
        if (t->has_ended) {
            scroll_restore_margins();
        }
        return;
    }
    scroll_restore_margins();

    if (!data.capture_input) {
        return;
    }
    int was_click = touch_was_click(touch_get_latest());
    if (t->has_started || was_click) {
        scroll_drag_start(1);
        return;
    }
    if (!touch_not_click(t)) {
        return;
    }
    if (t->has_ended) {
        scroll_drag_end();
    }
}

static void handle_touch_zoom(const touch *first, const touch *last)
{
    if (touch_not_click(first)) {
        zoom_update_touch(first, last, city_view_get_scale());
    }
    if (first->has_ended || last->has_ended) {
        zoom_end_touch();
    }
}

static void handle_last_touch(void)
{
    const touch *last = touch_get_latest();
    if (!last->in_use) {
        return;
    }
    if (touch_was_click(last)) {
        building_construction_cancel();
        window_request_refresh();
    } else if (touch_not_click(last)) {
        handle_touch_zoom(touch_get_earliest(), last);
    }
}

static int handle_play_pause_button(const touch *t)
{
    if (is_pause_button(t->current_point.x, t->current_point.y)) {
        game_state_toggle_paused();
        return 1;
    }
    return 0;
}

static int handle_construction_buttons(int x, int y, int is_touch)
{
    if (!building_construction_type()) {
        return 0;
    }
    if (is_touch && is_cancel_construction_button(x, y)) {
        building_construction_cancel();
        window_request_refresh();
        return 1;
    }

    if (building_construction_can_rotate()) {
        if (is_rotate_backward_button(x, y)) {
            building_rotation_rotate_backward();
            return 1;
        }
        if (is_rotate_forward_button(x, y)) {
            building_rotation_rotate_forward();
            return 1;
        }
    }
    return 0;
}

static void handle_first_touch(map_tile *tile)
{
    const touch *first = touch_get_earliest();
    building_type type = building_construction_type();

    if (touch_was_click(first)) {
        if (handle_play_pause_button(first) ||
            handle_construction_buttons(first->current_point.x, first->current_point.y, 1) ||
            handle_legion_click(tile)) {
            return;
        }
        if (type == BUILDING_NONE && handle_right_click_allow_building_info(tile)) {
            scroll_drag_end();
            data.capture_input = 0;
            window_building_info_show(tile->grid_offset);
            return;
        }
    }

    handle_touch_scroll(first);

    if (!input_coords_in_city(first->current_point.x, first->current_point.y) || type == BUILDING_NONE) {
        return;
    }

    if (building_construction_is_updatable()) {
        if (!building_construction_in_progress()) {
            if (first->has_started) {
                build_start(tile);
                data.new_start_grid_offset = 0;
            }
        } else {
            if (first->has_started) {
                if (data.selected_tile.grid_offset != tile->grid_offset) {
                    data.new_start_grid_offset = tile->grid_offset;
                }
            }
            if (touch_not_click(first) && data.new_start_grid_offset) {
                data.new_start_grid_offset = 0;
                data.selected_tile.grid_offset = 0;
                building_construction_cancel();
                build_start(tile);
            }
            build_move(tile);
            if (data.selected_tile.grid_offset != tile->grid_offset) {
                data.selected_tile.grid_offset = 0;
            }
            if (first->has_ended) {
                if (data.selected_tile.grid_offset == tile->grid_offset) {
                    build_end();
                    widget_city_clear_current_tile();
                    data.new_start_grid_offset = 0;
                } else {
                    data.selected_tile.grid_offset = tile->grid_offset;
                }
            }
        }
        return;
    }

    int size = building_properties_for_type(type)->size;
    if (type == BUILDING_WAREHOUSE) {
        size = 3;
    }

    if (touch_was_click(first) && first->has_ended && data.capture_input &&
        has_confirmed_construction(data.selected_tile.grid_offset, tile->grid_offset, size)) {
        build_start(&data.selected_tile);
        build_move(&data.selected_tile);
        build_end();
        widget_city_clear_current_tile();
    } else if (first->has_ended) {
        data.selected_tile = *tile;
    }
}

static void handle_touch(void)
{
    const touch *first = touch_get_earliest();
    if (!first->in_use) {
        scroll_restore_margins();
        return;
    }

    if (touch_was_click(first) && handle_construction_buttons(first->current_point.x, first->current_point.y, 1)) {
        return;
    }

    map_tile *tile = &data.current_tile;
    if (!building_construction_in_progress() || input_coords_in_city(first->current_point.x, first->current_point.y)) {
        update_city_view_coords(first->current_point.x, first->current_point.y, tile);
    }

    if (first->has_started && input_coords_in_city(first->current_point.x, first->current_point.y)) {
        data.capture_input = 1;
        scroll_restore_margins();
    }

    handle_last_touch();
    handle_first_touch(tile);

    if (first->has_ended) {
        data.capture_input = 0;
    }

    building_construction_reset_draw_as_constructing();
}

int widget_city_has_input(void)
{
    return data.capture_input;
}

static void handle_mouse(const mouse *m)
{
    map_tile *tile = &data.current_tile;
    update_city_view_coords(m->x, m->y, tile);
    building_construction_reset_draw_as_constructing();
    if (building_copy_cursor_handle_mouse(m, tile)) {
        return;
    }
    if (m->left.went_down) {
        if (handle_legion_click(tile)) {
            return;
        }
        if (config_get(CONFIG_UI_ALWAYS_SHOW_ROTATION_BUTTONS) && handle_construction_buttons(m->x, m->y, 0)) {
            return;
        }
        if (building_construction_type()) {
            if (!building_construction_in_progress()) {
                build_start(tile);
            }
            build_move(tile);
        }
    } else if (building_construction_type() && (m->left.is_down || building_construction_in_progress())) {
        build_move(tile);
    }
    if (m->left.went_up) {
        if (building_construction_type()) {
            build_end();
        } else {
            int grid_offset = tile->grid_offset;
            int building_id = map_building_at(grid_offset);
            if (building_id) {
                building *b = building_main(building_get(building_id));
                grid_offset = b->grid_offset;
            }
            if (data.routing_grid_offset != grid_offset) {
                data.routing_grid_offset = grid_offset;
                widget_city_setup_routing_preview();
            }
        }
    }
    if (m->right.went_down && input_coords_in_city(m->x, m->y)) {
        scroll_drag_start(0);
    }
    if (m->right.went_up) {
        if (scroll_drag_end()) {
            return;
        }
        if (building_construction_type()) {
            building_construction_cancel();
            window_request_refresh();
            return;
        }
        if (handle_right_click_allow_building_info(tile)) {
            int building_id = building_main(building_get(map_building_at(tile->grid_offset)))->id; //building inception!
            data.selected_building_id = building_id ? building_id : NO_POSITION; //no position if selected 0
            window_building_info_show(tile->grid_offset);
            return;
        }
    }
}

void widget_city_handle_input(const mouse *m, const hotkeys *h)
{
    scroll_map(m);

    if (m->is_touch) {
        handle_touch();
    } else {
        handle_mouse(m);
        zoom_map(m, h, city_view_get_scale());
    }

    if (h->escape_pressed) {
        if (building_copy_cursor_is_active()) {
            building_copy_cursor_cancel();
            window_request_refresh();
        } else if (building_construction_type()) {
            building_construction_cancel();
            window_request_refresh();
        } else {
            video_stop();
            window_pause_menu_show();
        }
    }
}

static void military_map_click(int legion_formation_id, const map_tile *tile)
{
    if (!tile->grid_offset) {
        return;
    }
    formation *m = formation_get(legion_formation_id);
    if (m->in_distant_battle || m->cursed_by_mars) {
        return;
    }
    int other_formation_id = formation_legion_at_building(tile->grid_offset);
    if (other_formation_id && other_formation_id == legion_formation_id) {
        formation_legion_return_home(m);
    } else {
        formation_legion_move_to(m, tile);
        if (config_get(CONFIG_UI_MOVE_LEGION_SOUND_SWAP)) {
            sound_speech_play_file("wavs/marching.wav"); //replacement marching sound
        } else {
            sound_speech_play_file("wavs/cohort5.wav");
        }

    }
    window_city_show();
}

static int tile_was_used(int grid_offset, const int *used_offsets, int used_count)
{
    for (int i = 0; i < used_count; i++) {
        if (used_offsets[i] == grid_offset) {
            return 1;
        }
    }
    return 0;
}

static int formation_layout_for_direction(const formation *m, int direction)
{
    if (m->layout == FORMATION_COLUMN || m->layout == FORMATION_TORTOISE || m->layout == FORMATION_MOP_UP) {
        return m->layout;
    }
    if (direction == DIR_6_LEFT || direction == DIR_2_RIGHT) {
        return m->layout == FORMATION_SINGLE_LINE_1 || m->layout == FORMATION_SINGLE_LINE_2 ?
            FORMATION_SINGLE_LINE_2 : FORMATION_DOUBLE_LINE_2;
    }
    return m->layout == FORMATION_SINGLE_LINE_1 || m->layout == FORMATION_SINGLE_LINE_2 ?
        FORMATION_SINGLE_LINE_1 : FORMATION_DOUBLE_LINE_1;
}

static int command_cardinal_direction(int direction)
{
    switch (direction) {
        case DIR_1_TOP_RIGHT:
            return DIR_0_TOP;
        case DIR_3_BOTTOM_RIGHT:
            return DIR_2_RIGHT;
        case DIR_5_BOTTOM_LEFT:
            return DIR_4_BOTTOM;
        case DIR_7_TOP_LEFT:
            return DIR_6_LEFT;
        default:
            return direction;
    }
}

static int legion_command_role(const formation *m)
{
    switch (m->figure_type) {
        case FIGURE_FORT_MOUNTED:
            return 0;
        case FIGURE_FORT_LEGIONARY:
        case FIGURE_FORT_INFANTRY:
            return 1;
        case FIGURE_FORT_ARCHER:
        case FIGURE_FORT_JAVELIN:
            return 2;
        default:
            return 1;
    }
}

static map_point command_offset_for_role(int direction, int role, int index)
{
    static const int side_offsets[] = {0, -4, 4, -8, 8, -12, 12};
    int side = side_offsets[index % (sizeof(side_offsets) / sizeof(side_offsets[0]))];
    map_point offset = {0, 0};
    int row = (role - 1) * 2;
    switch (direction) {
        case DIR_0_TOP:
            offset.x = side;
            offset.y = row;
            break;
        case DIR_4_BOTTOM:
            offset.x = side;
            offset.y = -row;
            break;
        case DIR_2_RIGHT:
            offset.x = -row;
            offset.y = side;
            break;
        case DIR_6_LEFT:
            offset.x = row;
            offset.y = side;
            break;
        default:
            offset.x = side;
            offset.y = 0;
            break;
    }
    return offset;
}

static int get_legion_command_target(const formation *m, const map_tile *center, int direction,
    int role, int preferred_offset, const int *used_offsets, int used_count, map_tile *target)
{
    static const map_point square_offsets[] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 0}, {0, -1}, {-1, -1}, {1, -1}, {-1, 1}
    };
    const int num_square_offsets = sizeof(square_offsets) / sizeof(square_offsets[0]);
    map_routing_calculate_distances(m->x_home, m->y_home);
    for (int attempt = 0; attempt < num_square_offsets; attempt++) {
        const map_point offset = direction >= DIR_0_TOP && direction < DIR_8_NONE ?
            command_offset_for_role(direction, role, preferred_offset + attempt) :
            square_offsets[(preferred_offset + attempt) % num_square_offsets];
        int x = center->x + offset.x;
        int y = center->y + offset.y;
        if (!map_grid_is_inside(x, y, 1)) {
            continue;
        }
        int grid_offset = map_grid_offset(x, y);
        if (tile_was_used(grid_offset, used_offsets, used_count) || map_routing_distance(grid_offset) <= 0) {
            continue;
        }
        target->x = x;
        target->y = y;
        target->grid_offset = grid_offset;
        return 1;
    }
    if (direction >= DIR_0_TOP && direction < DIR_8_NONE) {
        for (int attempt = 0; attempt < num_square_offsets; attempt++) {
            const map_point offset = square_offsets[(preferred_offset + attempt) % num_square_offsets];
            int x = center->x + offset.x;
            int y = center->y + offset.y;
            if (!map_grid_is_inside(x, y, 1)) {
                continue;
            }
            int grid_offset = map_grid_offset(x, y);
            if (tile_was_used(grid_offset, used_offsets, used_count) || map_routing_distance(grid_offset) <= 0) {
                continue;
            }
            target->x = x;
            target->y = y;
            target->grid_offset = grid_offset;
            return 1;
        }
    }
    return 0;
}

static void military_map_click_all_legions(const map_tile *tile, int direction)
{
    if (!tile->grid_offset) {
        return;
    }
    int used_offsets[MAX_LEGIONS] = { 0 };
    int cardinal_direction = command_cardinal_direction(direction);
    int used_count = 0;
    int ordered = 0;
    int ordered_in_role[3] = {0};
    for (int role = 0; role < 3; role++) {
        for (int i = 1; i < formation_count(); i++) {
            formation *m = formation_get(i);
            if (!m || !m->in_use || !m->is_legion || m->is_herd || m->in_distant_battle ||
                m->cursed_by_mars || m->num_figures <= 0 || legion_command_role(m) != role) {
                continue;
            }
            map_tile target;
            if (!get_legion_command_target(m, tile, cardinal_direction, role,
                ordered_in_role[role], used_offsets, used_count, &target)) {
                continue;
            }
            if (cardinal_direction >= DIR_0_TOP && cardinal_direction < DIR_8_NONE) {
                m->layout = formation_layout_for_direction(m, cardinal_direction);
            }
            formation_legion_move_to(m, &target);
            if (cardinal_direction >= DIR_0_TOP && cardinal_direction < DIR_8_NONE) {
                m->direction = cardinal_direction;
            }
            used_offsets[used_count++] = target.grid_offset;
            ordered_in_role[role]++;
            ordered++;
        }
    }
    if (ordered > 0) {
        if (config_get(CONFIG_UI_MOVE_LEGION_SOUND_SWAP)) {
            sound_speech_play_file("wavs/marching.wav");
        } else {
            sound_speech_play_file("wavs/cohort5.wav");
        }
    }
    window_city_show();
}

static void handle_military_input(const mouse *m, const hotkeys *h, int legion_formation_id, int all_legions)
{
    map_tile *tile = &data.current_tile;
    update_city_view_coords(m->x, m->y, tile);
    if (!city_view_is_sidebar_collapsed() && widget_minimap_handle_mouse(m)) {
        return;
    }
    scroll_map(m);
    if (m->is_touch) {
        const touch *t = touch_get_earliest();
        if (!t->in_use) {
            return;
        }
        if (touch_was_click(t) && handle_play_pause_button(t)) {
            return;
        }
        if (t->has_started) {
            data.capture_input = 1;
        }
        const touch *last = touch_get_latest();
        if (last->in_use) {
            handle_touch_zoom(t, last);
        }
        handle_touch_scroll(t);
        if (t->has_ended) {
            data.capture_input = 0;
        }
    } else {
        zoom_map(m, h, city_view_get_scale());
    }

    if (all_legions && !m->is_touch && m->left.went_down) {
        data.legion_command_tile = *tile;
        data.legion_command_dragging = 1;
        data.capture_input = 1;
        data.legion_command_start_x = m->x;
        data.legion_command_start_y = m->y;
        data.legion_command_current_x = m->x;
        data.legion_command_current_y = m->y;
        return;
    }
    if (all_legions && data.legion_command_dragging) {
        data.legion_command_current_x = m->x;
        data.legion_command_current_y = m->y;
    }

    if (all_legions && !m->is_touch && m->left.went_up && data.legion_command_dragging) {
        int direction = DIR_8_NONE;
        if (data.legion_command_tile.grid_offset && tile->grid_offset &&
            data.legion_command_tile.grid_offset != tile->grid_offset) {
            direction = calc_general_direction(data.legion_command_tile.x, data.legion_command_tile.y, tile->x, tile->y);
        }
        military_map_click_all_legions(&data.legion_command_tile, direction);
        data.legion_command_dragging = 0;
        data.capture_input = 0;
    } else if ((!m->is_touch && m->left.went_down)
        || (m->is_touch && m->left.went_up && touch_was_click(touch_get_earliest()))) {
        if (all_legions) {
            military_map_click_all_legions(tile, DIR_8_NONE);
        } else {
            military_map_click(legion_formation_id, tile);
        }
    }

    if (m->right.went_down && input_coords_in_city(m->x, m->y)) {
        scroll_drag_start(0);
    }
    if ((m->right.went_up && !scroll_drag_end()) || h->escape_pressed) {
        data.capture_input = 0;
        data.legion_command_dragging = 0;
        city_warning_clear_all();
        window_city_show();
    }
}

void widget_city_handle_input_military(const mouse *m, const hotkeys *h, int legion_formation_id)
{
    handle_military_input(m, h, legion_formation_id, 0);
}

void widget_city_handle_input_all_legions(const mouse *m, const hotkeys *h)
{
    handle_military_input(m, h, 0, 1);
}

int widget_city_current_grid_offset(void)
{
    return data.current_tile.grid_offset;
}

void widget_city_get_tooltip(tooltip_context *c)
{
    if (setting_tooltips() == TOOLTIPS_NONE) {
        return;
    }
    if (!window_is(WINDOW_CITY)) {
        return;
    }
    if (data.current_tile.grid_offset == 0) {
        return;
    }
    int grid_offset = data.current_tile.grid_offset;
    int building_id = map_building_at(grid_offset);
    int overlay = game_state_overlay();
    // cheat tooltips
    if (overlay == OVERLAY_NONE && game_cheat_tooltip_enabled()) {
        c->type = TOOLTIP_TILES;
        c->high_priority = 1;
        return;
    }
    // regular tooltips
    if (overlay == OVERLAY_NONE && building_id && building_get(building_id)->type == BUILDING_SENATE) {
        c->type = TOOLTIP_SENATE;
        c->high_priority = 1;
        return;
    }
    // overlay tooltips
    if (overlay != OVERLAY_NONE) {
        c->text_group = 66;
        c->text_id = city_overlay_get_tooltip_text(c, grid_offset);
        if (c->text_id || c->translation_key) {
            c->type = TOOLTIP_OVERLAY;
            c->high_priority = 1;
        }
    }
}

void widget_city_clear_current_tile(void)
{
    data.selected_tile.x = -1;
    data.selected_tile.y = -1;
    data.selected_tile.grid_offset = 0;
    data.current_tile.grid_offset = 0;
    data.routing_grid_offset = 0;
    data.selected_building_id = NO_POSITION;

}

void widget_city_clear_routing_grid_offset(void)
{
    data.routing_grid_offset = 0;
    data.selected_building_id = NO_POSITION;
}

void widget_city_setup_routing_preview(void)
{
    if (!config_get(CONFIG_UI_SHOW_ROAMING_PATH)) {
        figure_roamer_preview_reset_building_types();
        return;
    }

    int building_id = map_building_at(data.routing_grid_offset);

    if (building_id) {
        data.selected_building_id = building_id;
        building *b = building_main(building_get(building_id));
        figure_roamer_preview_reset(b->type);
        figure_roamer_preview_create(b->type, b->x, b->y);
    } else {
        data.selected_building_id = NO_POSITION;
        figure_roamer_preview_reset(building_construction_type());
    }
}
