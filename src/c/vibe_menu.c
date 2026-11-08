#include "vibe_menu.h"
#include "alarm_store.h"
#include "alert.h"
#include "ui_common.h"
#include "i18n.h"

#define HEADER_H 28
#define ROW_H 50
#define ROW_TOGGLE 0   // row 0 = on/off, rows 1-9 = patterns

static Window *s_window;
static MenuLayer *s_menu;
static int s_slot;
static bool s_ready;   // only play previews once the window is open
static uint8_t s_order[VIBE_PATTERN_COUNT];  // display order: alphabetical by name

// Case-insensitive comparison (names are plain ASCII)
static int prv_compare_names(const char *a, const char *b) {
  for (;; a++, b++) {
    char ca = (*a >= 'A' && *a <= 'Z') ? *a - 'A' + 'a' : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? *b - 'A' + 'a' : *b;
    if (ca != cb || !ca) return ca - cb;
  }
}

// Sort the patterns alphabetically by their name in the watch language
static void prv_build_order(void) {
  for (int i = 0; i < VIBE_PATTERN_COUNT; i++) {
    uint8_t p = i;
    int j = i - 1;
    while (j >= 0 && prv_compare_names(ui_vibe_name(s_order[j]), ui_vibe_name(p)) > 0) {
      s_order[j + 1] = s_order[j];
      j--;
    }
    s_order[j + 1] = p;
  }
}

static int prv_row_of_pattern(int pattern) {
  for (int i = 0; i < VIBE_PATTERN_COUNT; i++) {
    if (s_order[i] == pattern) return 1 + i;
  }
  return 1;
}

static const Alarm *prv_alarm(void) {
  return store_get(s_slot);
}

static uint16_t prv_rows(MenuLayer *m, uint16_t s, void *c) { return 1 + VIBE_PATTERN_COUNT; }
static int16_t prv_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t prv_header_h(MenuLayer *m, uint16_t s, void *c) { return HEADER_H; }

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  ui_draw_header_bar(ctx, layer_get_bounds(cell), TR("Vibration", "Vibration"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 2);
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  const Alarm *a = prv_alarm();
  const Theme *t = theme();
  bool hl = menu_cell_layer_is_highlighted(cell);
  GRect b = layer_get_bounds(cell);
  graphics_context_set_text_color(ctx, hl ? t->hl_fg : t->fg);

  if (index->row == ROW_TOGGLE) {
    graphics_draw_text(ctx, a->vibrate ? TR("On", "An") : TR("Off", "Aus"),
                       fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       GRect(10, (b.size.h - 36) / 2, b.size.w - 60, 36),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    ui_draw_toggle(ctx, GPoint(b.size.w - UI_TOGGLE_W - 10, (b.size.h - UI_TOGGLE_H) / 2),
                   a->vibrate, hl);
    return;
  }
  int pattern = s_order[index->row - 1];
  // With vibration off, the patterns are greyed out
  if (!a->vibrate) graphics_context_set_text_color(ctx, hl ? t->hl_dim : t->dim);
  graphics_draw_text(ctx, ui_vibe_name(pattern), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(10, (b.size.h - 36) / 2, b.size.w - 50, 36),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  ui_draw_radio(ctx, GPoint(b.size.w - 22, b.size.h / 2), pattern == a->vibe_pattern, hl);
}

// Looping preview of the highlighted pattern; no preview on the on/off row
static void prv_preview_row(int row) {
  if (row == ROW_TOGGLE) {
    alert_stop();
  } else {
    alert_preview_vibe(s_order[row - 1]);
  }
}

static void prv_selection_changed(MenuLayer *m, MenuIndex new_index, MenuIndex old_index, void *c) {
  if (s_ready) prv_preview_row(new_index.row);
}

static void prv_select(MenuLayer *m, MenuIndex *index, void *c) {
  Alarm a = *prv_alarm();
  if (index->row == ROW_TOGGLE) {
    if (a.vibrate && a.sound == ALARM_SOUND_OFF) {
      ui_show_message(TR("Sound or vibration must stay on.", "Ton oder Vibration muss an bleiben."));
      return;
    }
    a.vibrate = !a.vibrate;
    store_save(s_slot, &a);
    menu_layer_reload_data(s_menu);
    return;
  }
  // Choosing a pattern turns vibration on and returns to the alarm menu
  a.vibe_pattern = s_order[index->row - 1];
  a.vibrate = 1;
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
  // Select the current pattern (or on/off if vibration is off)
  const Alarm *a = prv_alarm();
  int row = a->vibrate ? prv_row_of_pattern(a->vibe_pattern) : ROW_TOGGLE;
  menu_layer_set_selected_index(s_menu, MenuIndex(0, row), MenuRowAlignCenter, false);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  s_ready = true;
  prv_preview_row(row);  // preview of the current pattern right away
}

static void prv_unload(Window *window) {
  s_ready = false;
  alert_stop();  // stop the preview
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(window);
  s_window = NULL;
}

void vibe_menu_show(int slot) {
  if (s_window || !store_get(slot) || !store_get(slot)->used) return;
  s_slot = slot;
  s_ready = false;
  prv_build_order();
  s_window = window_create();
  window_set_background_color(s_window, theme()->bg);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
