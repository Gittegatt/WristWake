#include "symbol_menu.h"
#include "alarm_store.h"
#include "symbols.h"
#include "ui_common.h"
#include "i18n.h"

#define HEADER_H 28
#define ROW_H 46

static Window *s_window;
static MenuLayer *s_menu;
static int s_slot;
static uint8_t s_order[SYMBOL_COUNT];   // display order: "None" first, then by color

static void prv_sort(void) {
  for (int i = 0; i < SYMBOL_COUNT; i++) s_order[i] = symbol_at(i);   // "None" first, then by color
}

static const Alarm *prv_alarm(void) {
  return store_get(s_slot);
}

static uint16_t prv_rows(MenuLayer *m, uint16_t s, void *c) { return SYMBOL_COUNT; }
static int16_t prv_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t prv_header_h(MenuLayer *m, uint16_t s, void *c) { return HEADER_H; }

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  ui_draw_header_bar(ctx, layer_get_bounds(cell), TR("Symbol", "Symbol"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 2);
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  int symbol = s_order[index->row];
  const Theme *t = theme();
  bool hl = menu_cell_layer_is_highlighted(cell);
  GRect b = layer_get_bounds(cell);
  int text_x = 10;
  if (symbol != SYMBOL_NONE) {
    symbol_draw(ctx, symbol, GPoint(8, (b.size.h - SYMBOL_LARGE) / 2), true, hl ? t->hl_bg : t->bg,
                hl ? t->hl_fg : t->fg);
    text_x = 8 + SYMBOL_LARGE + 8;
  }
  graphics_context_set_text_color(ctx, hl ? t->hl_fg : t->fg);
  graphics_draw_text(ctx, symbol_name(symbol), fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(text_x, (b.size.h - 32) / 2, b.size.w - text_x - 34, 32),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  ui_draw_radio(ctx, GPoint(b.size.w - 18, b.size.h / 2), symbol == prv_alarm()->symbol, hl);
}

// Choosing saves the symbol and returns to the alarm menu
static void prv_select(MenuLayer *m, MenuIndex *index, void *c) {
  Alarm a = *prv_alarm();
  a.symbol = s_order[index->row];
  store_save(s_slot, &a);
  window_stack_remove(s_window, true);
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
  };
  menu_layer_set_callbacks(s_menu, NULL, callbacks);
  menu_layer_set_click_config_onto_window(s_menu, window);
  int row = 0;
  for (int i = 0; i < SYMBOL_COUNT; i++) {
    if (s_order[i] == prv_alarm()->symbol) row = i;
  }
  menu_layer_set_selected_index(s_menu, MenuIndex(0, row), MenuRowAlignCenter, false);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void prv_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(window);
  s_window = NULL;
}

void symbol_menu_show(int slot) {
  const Alarm *a = store_get(slot);
  if (s_window || !a || !a->used) return;
  s_slot = slot;
  prv_sort();
  s_window = window_create();
  window_set_background_color(s_window, theme()->bg);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
