#include "pickers.h"
#include "ui_common.h"
#include "i18n.h"

#define HEADER_H 28
#define ROW_H 50   // 4 items per page, like in the alarm menu

static void prv_draw_header(GContext *ctx, const Layer *cell, const char *title) {
  ui_draw_header_bar(ctx, layer_get_bounds(cell), title,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 2);
}

static void prv_draw_label(GContext *ctx, const Layer *cell, const char *text, bool highlighted) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_text_color(ctx, highlighted ? theme()->hl_fg : theme()->fg);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(10, (b.size.h - 36) / 2, b.size.w - 50, 36),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ===========================================================================
// Weekdays
// ===========================================================================

static Window *s_days_window;
static MenuLayer *s_days_menu;
static uint8_t s_days;
static DaysPickerCallback s_days_done;
static void *s_days_ctx;
static char s_days_summary[48];

#define DAYS_ROW_DONE 7

static uint16_t prv_days_rows(MenuLayer *m, uint16_t s, void *c) { return 8; }
static int16_t prv_days_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t prv_days_header_h(MenuLayer *m, uint16_t s, void *c) { return HEADER_H; }

static void prv_days_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  prv_draw_header(ctx, cell, TR("Repeat on", "Wiederholen am"));
}

static void prv_days_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  bool hl = menu_cell_layer_is_highlighted(cell);
  GRect b = layer_get_bounds(cell);
  if (index->row == DAYS_ROW_DONE) {
    const Theme *t = theme();
    ui_format_days(s_days_summary, sizeof(s_days_summary), s_days);
    graphics_context_set_text_color(ctx, hl ? t->hl_fg : t->fg);
    graphics_draw_text(ctx, TR("Done", "Fertig"), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       GRect(10, -7, b.size.w - 20, 34), GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, s_days_summary, fonts_get_system_font(FONT_KEY_GOTHIC_24),
                       GRect(10, 19, b.size.w - 20, 30), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    return;
  }
  prv_draw_label(ctx, cell, ui_day_name(index->row), hl);
  ui_draw_checkbox(ctx, GPoint(b.size.w - 30, (b.size.h - 16) / 2),
                   s_days & (1 << index->row), hl);
}

static void prv_days_select(MenuLayer *m, MenuIndex *index, void *c) {
  if (index->row == DAYS_ROW_DONE) {
    DaysPickerCallback done = s_days_done;
    void *ctx = s_days_ctx;
    uint8_t days = s_days;
    window_stack_remove(s_days_window, false);
    if (done) done(days, ctx);
    return;
  }
  s_days ^= (1 << index->row);
  menu_layer_reload_data(s_days_menu);
}

static void prv_days_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_days_menu = menu_layer_create(layer_get_bounds(root));
  ui_style_menu(s_days_menu);
  static const MenuLayerCallbacks callbacks = {
    .get_num_rows = prv_days_rows,
    .get_cell_height = prv_days_cell_h,
    .get_header_height = prv_days_header_h,
    .draw_header = prv_days_header,
    .draw_row = prv_days_draw_row,
    .select_click = prv_days_select,
  };
  menu_layer_set_callbacks(s_days_menu, NULL, callbacks);
  menu_layer_set_click_config_onto_window(s_days_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_days_menu));
}

static void prv_days_unload(Window *window) {
  menu_layer_destroy(s_days_menu);
  window_destroy(window);
  s_days_window = NULL;
}

void days_picker_show(uint8_t days, DaysPickerCallback done, void *context) {
  if (s_days_window) return;
  s_days = days & 0x7F;
  s_days_done = done;
  s_days_ctx = context;
  s_days_window = window_create();
  window_set_background_color(s_days_window, theme()->bg);
  window_set_window_handlers(s_days_window, (WindowHandlers) {
    .load = prv_days_load,
    .unload = prv_days_unload,
  });
  window_stack_push(s_days_window, true);
}

// ===========================================================================
// Choose one option
// ===========================================================================

static Window *s_opt_window;
static MenuLayer *s_opt_menu;
static char s_opt_title[32];
static const char *const *s_opt_labels;
static int s_opt_count;
static int s_opt_selected;
static OptionPickerCallback s_opt_done;
static void *s_opt_ctx;

static uint16_t prv_opt_rows(MenuLayer *m, uint16_t s, void *c) { return s_opt_count; }
static int16_t prv_opt_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t prv_opt_header_h(MenuLayer *m, uint16_t s, void *c) { return HEADER_H; }

static void prv_opt_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  prv_draw_header(ctx, cell, s_opt_title);
}

static void prv_opt_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  bool hl = menu_cell_layer_is_highlighted(cell);
  GRect b = layer_get_bounds(cell);
  prv_draw_label(ctx, cell, s_opt_labels[index->row], hl);
  ui_draw_radio(ctx, GPoint(b.size.w - 22, b.size.h / 2), index->row == s_opt_selected, hl);
}

static void prv_opt_select(MenuLayer *m, MenuIndex *index, void *c) {
  OptionPickerCallback done = s_opt_done;
  void *ctx = s_opt_ctx;
  int chosen = index->row;
  window_stack_remove(s_opt_window, false);
  if (done) done(chosen, ctx);
}

static void prv_opt_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_opt_menu = menu_layer_create(layer_get_bounds(root));
  ui_style_menu(s_opt_menu);
  static const MenuLayerCallbacks callbacks = {
    .get_num_rows = prv_opt_rows,
    .get_cell_height = prv_opt_cell_h,
    .get_header_height = prv_opt_header_h,
    .draw_header = prv_opt_header,
    .draw_row = prv_opt_draw_row,
    .select_click = prv_opt_select,
  };
  menu_layer_set_callbacks(s_opt_menu, NULL, callbacks);
  menu_layer_set_click_config_onto_window(s_opt_menu, window);
  menu_layer_set_selected_index(s_opt_menu, MenuIndex(0, s_opt_selected), MenuRowAlignCenter, false);
  layer_add_child(root, menu_layer_get_layer(s_opt_menu));
}

static void prv_opt_unload(Window *window) {
  menu_layer_destroy(s_opt_menu);
  window_destroy(window);
  s_opt_window = NULL;
}

void option_picker_show(const char *title, const char *const *labels, int count, int selected,
                        OptionPickerCallback done, void *context) {
  if (s_opt_window || count <= 0) return;
  snprintf(s_opt_title, sizeof(s_opt_title), "%s", title);
  s_opt_labels = labels;
  s_opt_count = count;
  s_opt_selected = (selected >= 0 && selected < count) ? selected : 0;
  s_opt_done = done;
  s_opt_ctx = context;
  s_opt_window = window_create();
  window_set_background_color(s_opt_window, theme()->bg);
  window_set_window_handlers(s_opt_window, (WindowHandlers) {
    .load = prv_opt_load,
    .unload = prv_opt_unload,
  });
  window_stack_push(s_opt_window, true);
}
