#include "alarm_list.h"
#include "symbols.h"
#include "alarm_store.h"
#include "alarm_menu.h"
#include "time_picker.h"
#include "ui_common.h"
#include "i18n.h"

#define HEADER_H 28      // same as the alarm menu
#define ALARM_ROW_H 66   // 3 alarms per page: (228 - 28) / 3
#define NEW_ROW_H 46
#define DAYS_W 74        // width of the weekday letters
#define ZZ_W 22          // "zZ" (snooze on) top right after the time

static Window *s_window;
static Layer *s_header;
static MenuLayer *s_menu;

static int s_order[ALARM_SLOTS];  // slot numbers, sorted by time
static int s_count;
static int s_focus_slot;          // select this alarm when returning (0 = first)

static void prv_build_order(void) {
  s_count = 0;
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    if (store_get(slot)->used) s_order[s_count++] = slot;
  }
  // sort by time (same time: by slot)
  for (int i = 1; i < s_count; i++) {
    int slot = s_order[i];
    const Alarm *a = store_get(slot);
    int key = a->hour * 60 + a->minute;
    int j = i - 1;
    while (j >= 0) {
      const Alarm *b = store_get(s_order[j]);
      if (b->hour * 60 + b->minute <= key) break;
      s_order[j + 1] = s_order[j];
      j--;
    }
    s_order[j + 1] = slot;
  }
}

static void prv_select_row(int row) {
  menu_layer_set_selected_index(s_menu, MenuIndex(0, row), MenuRowAlignCenter, false);
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

static void prv_header_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  // "WristWake" centered (the version number is shown at the bottom of the settings page)
  // Same height and font as the headers of the alarm menu and pickers
  ui_draw_header_bar(ctx, b, "", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), -3);
  graphics_context_set_text_color(ctx, ui_header_fg());
  graphics_draw_text(ctx, "WristWake", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, -3, b.size.w, b.size.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

static uint16_t prv_rows(MenuLayer *m, uint16_t s, void *c) { return s_count + 1; }

static int16_t prv_cell_h(MenuLayer *m, MenuIndex *index, void *c) {
  return index->row < s_count ? ALARM_ROW_H : NEW_ROW_H;
}

static void prv_draw_alarm(GContext *ctx, const Layer *cell, const Alarm *a) {
  GRect b = layer_get_bounds(cell);
  bool hl = menu_cell_layer_is_highlighted(cell);
  bool off = !a->enabled;
  const Theme *t = theme();

  GColor main_color = hl ? (off ? t->hl_dim : t->hl_fg) : (off ? t->dim : t->fg);

  // Line 1: large time on the left (room for "23:59"), ON/OFF toggle on the right
  char time_buf[8];
  const char *ampm;
  ui_format_time(time_buf, sizeof(time_buf), a->hour, a->minute, false, &ampm);
  GFont time_font = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
  GRect time_rect = GRect(8, 0, b.size.w - UI_TOGGLE_W - 24, 42);
  graphics_context_set_text_color(ctx, main_color);
  graphics_draw_text(ctx, time_buf, time_font, time_rect, GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
  GSize size = graphics_text_layout_get_content_size(time_buf, time_font, time_rect,
                                                     GTextOverflowModeFill, GTextAlignmentLeft);
  int after_x = time_rect.origin.x + size.w + 4;   // AM/PM right of the time
  GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (ampm) {
    graphics_draw_text(ctx, ampm, small_font, GRect(after_x, 14, 30, 22), GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
  }
  // "zZ" top right after the time when snoozing is possible (snooze on, no challenge); the same
  // place for every alarm: right of the widest possible time "00:00"
  if (a->snooze > 0 && a->challenge == CHALLENGE_OFF) {
    int zz_x = time_rect.origin.x + graphics_text_layout_get_content_size(
        "00:00", time_font, time_rect, GTextOverflowModeFill, GTextAlignmentLeft).w + 3;
    graphics_draw_text(ctx, "zZ", small_font, GRect(zz_x, -3, ZZ_W, 22), GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
  }
  ui_draw_toggle(ctx, GPoint(b.size.w - UI_TOGGLE_W - 8, 14), a->enabled, hl);

  // Line 2: (symbol and) name on the left, weekdays on the right
  int name_x = 8;
  if (a->symbol && symbols_show(SYMBOL_SHOW_LIST)) {
    symbol_draw(ctx, a->symbol, GPoint(8, 44), false, hl ? t->hl_bg : t->bg, hl ? t->hl_fg : t->fg);
    name_x += SYMBOL_SMALL + 4;
  }
  graphics_context_set_text_color(ctx, main_color);
  graphics_draw_text(ctx, a->name[0] ? a->name : TR("Alarm", "Wecker"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(name_x, 40, b.size.w - DAYS_W - 10 - name_x, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  ui_draw_day_letters(ctx, GRect(b.size.w - DAYS_W - 6, 43, DAYS_W, 18), a->days, hl, off);
}

static void prv_draw_new(GContext *ctx, const Layer *cell) {
  GRect b = layer_get_bounds(cell);
  bool hl = menu_cell_layer_is_highlighted(cell);
  bool full = store_find_free_slot() == 0;
  const Theme *t = theme();
  graphics_context_set_fill_color(ctx, hl ? t->hl_bg : t->bar_bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, hl ? t->hl_fg : t->bar_fg);
  if (full) {
    graphics_draw_text(ctx, TR("All 8 in use", "Alle 8 belegt"),
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(0, (b.size.h - 24) / 2, b.size.w, 24), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
  } else {
    graphics_draw_text(ctx, TR("+ NEW", "+ NEU"), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       GRect(0, (b.size.h - 36) / 2, b.size.w, 36), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
  }
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  if (index->row < s_count) {
    prv_draw_alarm(ctx, cell, store_get(s_order[index->row]));
  } else {
    prv_draw_new(ctx, cell);
  }
}

static void prv_new_time_done(uint8_t hour, uint8_t minute, void *ctx) {
  int slot = store_find_free_slot();
  if (slot == 0) return;
  if (!ui_check_time_allowed(slot, hour, minute)) return;
  Alarm a;
  store_alarm_defaults(&a);
  a.hour = hour;
  a.minute = minute;
  if (store_save(slot, &a)) {
    s_focus_slot = slot;
    alarm_menu_show(slot);
  }
}

static void prv_select(MenuLayer *m, MenuIndex *index, void *c) {
  if (index->row < s_count) {
    s_focus_slot = s_order[index->row];
    alarm_menu_show(s_focus_slot);
    return;
  }
  if (store_find_free_slot() == 0) {
    ui_show_message(TR("All 8 alarms are in use.", "Alle 8 Wecker sind belegt."));
    return;
  }
  time_picker_show(TR("New Alarm", "Neuer Wecker"), 7, 0, prv_new_time_done, NULL);
}

static void prv_long_select(MenuLayer *m, MenuIndex *index, void *c) {
  if (index->row >= s_count) return;  // "+ NEW": nothing
  int slot = s_order[index->row];
  Alarm a = *store_get(slot);
  if (!a.enabled && !ui_check_time_allowed(slot, a.hour, a.minute)) return;
  a.enabled = !a.enabled;
  store_save(slot, &a);
  vibes_short_pulse();  // short feedback when toggling
  menu_layer_reload_data(s_menu);
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_header = layer_create(GRect(0, 0, b.size.w, HEADER_H));
  layer_set_update_proc(s_header, prv_header_draw);
  layer_add_child(root, s_header);

  s_menu = menu_layer_create(GRect(0, HEADER_H, b.size.w, b.size.h - HEADER_H));
  ui_style_menu(s_menu);
  menu_layer_pad_bottom_enable(s_menu, false);
  static const MenuLayerCallbacks callbacks = {
    .get_num_rows = prv_rows,
    .get_cell_height = prv_cell_h,
    .draw_row = prv_draw_row,
    .select_click = prv_select,
    .select_long_click = prv_long_select,
  };
  menu_layer_set_callbacks(s_menu, NULL, callbacks);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void prv_appear(Window *window) {
  prv_build_order();
  menu_layer_reload_data(s_menu);
  // Selection: last edited alarm, otherwise the first item
  int row = 0;
  for (int i = 0; i < s_count; i++) {
    if (s_order[i] == s_focus_slot) row = i;
  }
  prv_select_row(row);
}

static void prv_unload(Window *window) {
  menu_layer_destroy(s_menu);
  layer_destroy(s_header);
  window_destroy(window);
  s_window = NULL;
}

void alarm_list_show(void) {
  s_focus_slot = 0;
  s_window = window_create();
  window_set_background_color(s_window, theme()->bg);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .appear = prv_appear,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
