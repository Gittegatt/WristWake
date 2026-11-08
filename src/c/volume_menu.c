#include "volume_menu.h"
#include "alarm_store.h"
#include "alert.h"
#include "ui_common.h"
#include "i18n.h"

#define HEADER_H 28
#define ROW_H 50

static Window *s_window;
static MenuLayer *s_menu;
static int s_slot;
static bool s_ready;   // only play previews once the window is open

static const Alarm *prv_alarm(void) {
  return store_get(s_slot);
}

static uint16_t prv_rows(MenuLayer *m, uint16_t s, void *c) { return ALARM_VOLUME_COUNT; }
static int16_t prv_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t prv_header_h(MenuLayer *m, uint16_t s, void *c) { return HEADER_H; }

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  ui_draw_header_bar(ctx, layer_get_bounds(cell), TR("Volume", "Lautstärke"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 2);
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  const Theme *t = theme();
  bool hl = menu_cell_layer_is_highlighted(cell);
  GRect b = layer_get_bounds(cell);
  graphics_context_set_text_color(ctx, hl ? t->hl_fg : t->fg);
  graphics_draw_text(ctx, ui_volume_name(index->row), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(10, (b.size.h - 36) / 2, b.size.w - 50, 36),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  ui_draw_radio(ctx, GPoint(b.size.w - 22, b.size.h / 2), index->row == prv_alarm()->volume, hl);
}

// Looping preview of the alarm's sound at the highlighted volume
static void prv_preview_row(int row) {
  AlertParams p = {.vibrate = false, .sound = prv_alarm()->sound, .volume = row, .preview = true};
  alert_start(&p);
}

static void prv_selection_changed(MenuLayer *m, MenuIndex new_index, MenuIndex old_index, void *c) {
  if (s_ready) prv_preview_row(new_index.row);
}

// Choosing a volume saves it and returns to the alarm menu
static void prv_select(MenuLayer *m, MenuIndex *index, void *c) {
  Alarm a = *prv_alarm();
  a.volume = index->row;
  store_save(s_slot, &a);
  window_stack_remove(s_window, true);  // stops the preview (prv_unload)
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_menu = menu_layer_create(layer_get_bounds(root));
  ui_style_menu(s_menu);
  static const MenuLayerCallbacks callbacks = {
    .get_num_rows = prv_rows,
    .get_cell_height = prv_cell_h,
    .get_header_height = prv_header_h,
    .draw_header = prv_draw_header,
    .draw_row = prv_draw_row,
    .select_click = prv_select,
    .selection_changed = prv_selection_changed,
  };
  menu_layer_set_callbacks(s_menu, NULL, callbacks);
  menu_layer_set_click_config_onto_window(s_menu, window);
  int row = prv_alarm()->volume;
  menu_layer_set_selected_index(s_menu, MenuIndex(0, row), MenuRowAlignCenter, false);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  s_ready = true;
  prv_preview_row(row);  // preview of the current volume right away
}

static void prv_unload(Window *window) {
  s_ready = false;
  alert_stop();  // stop the preview
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(window);
  s_window = NULL;
}

void volume_menu_show(int slot) {
  const Alarm *a = store_get(slot);
  if (s_window || !a || !a->used || a->sound == ALARM_SOUND_OFF) return;
  s_slot = slot;
  s_ready = false;
  s_window = window_create();
  window_set_background_color(s_window, theme()->bg);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
