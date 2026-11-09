#include "challenge_menu.h"
#include "alarm_store.h"
#include "pickers.h"
#include "ui_common.h"
#include "i18n.h"

#define HEADER_H 28
#define ROW_H 50

enum { ROW_MODE = 0, ROW_LENGTH, ROW_PAUSE, ROW_COUNT };

static Window *s_window;
static MenuLayer *s_menu;
static int s_slot;
static char s_subtitle[16];
static const char *s_mode_labels[CHALLENGE_COUNT];
static const char *s_length_labels[CHALLENGE_LEN_MAX - CHALLENGE_LEN_MIN + 1];
static char s_length_text[CHALLENGE_LEN_MAX - CHALLENGE_LEN_MIN + 1][4];

// Length range of the current mode: Follow 4-10 buttons, Remember 3-5 boxes
static int prv_len_min(const Alarm *a) {
  return a->challenge == CHALLENGE_REMEMBER ? REMEMBER_LEN_MIN : CHALLENGE_LEN_MIN;
}
static int prv_len_max(const Alarm *a) {
  return a->challenge == CHALLENGE_REMEMBER ? REMEMBER_LEN_MAX : CHALLENGE_LEN_MAX;
}
static int prv_len(const Alarm *a) {
  return a->challenge == CHALLENGE_REMEMBER ? a->remember_len : a->challenge_len;
}
static const char *s_pause_labels[CHALLENGE_PAUSE_COUNT];

static Alarm prv_current(void) {
  return *store_get(s_slot);
}

static void prv_save(const Alarm *alarm) {
  store_save(s_slot, alarm);
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void prv_mode_done(int index, void *ctx) {
  Alarm a = prv_current();
  a.challenge = index;
  prv_save(&a);
}

static void prv_length_done(int index, void *ctx) {
  Alarm a = prv_current();
  if (a.challenge == CHALLENGE_REMEMBER) a.remember_len = REMEMBER_LEN_MIN + index;
  else a.challenge_len = CHALLENGE_LEN_MIN + index;
  prv_save(&a);
}

static void prv_pause_done(int index, void *ctx) {
  Alarm a = prv_current();
  a.challenge_pause = index;
  prv_save(&a);
}

static uint16_t prv_rows(MenuLayer *m, uint16_t s, void *c) { return ROW_COUNT; }
static int16_t prv_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t prv_header_h(MenuLayer *m, uint16_t s, void *c) { return HEADER_H; }

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  ui_draw_header_bar(ctx, layer_get_bounds(cell), TR("Challenge", "Weckaufgabe"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 2);
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  const Alarm *a = store_get(s_slot);
  const Theme *t = theme();
  bool hl = menu_cell_layer_is_highlighted(cell);
  GRect b = layer_get_bounds(cell);
  const char *title = "";
  const char *subtitle = "";
  switch (index->row) {
    case ROW_MODE:
      title = TR("Mode", "Modus");
      subtitle = ui_challenge_name(a->challenge);
      break;
    case ROW_LENGTH:
      title = TR("Length", "Länge");
      snprintf(s_subtitle, sizeof(s_subtitle), "%d", prv_len(a));
      subtitle = s_subtitle;
      break;
    case ROW_PAUSE:
      title = TR("Pause", "Pause");
      subtitle = ui_pause_name(a->challenge_pause);
      break;
  }
  // Length applies to Follow and Remember, the pause only to Follow
  bool greyed = (index->row == ROW_LENGTH && a->challenge == CHALLENGE_OFF) ||
                (index->row == ROW_PAUSE && a->challenge != CHALLENGE_FOLLOW);
  graphics_context_set_text_color(ctx, greyed ? (hl ? t->hl_dim : t->dim) : (hl ? t->hl_fg : t->fg));
  graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(10, -7, b.size.w - 20, 34), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, subtitle, fonts_get_system_font(FONT_KEY_GOTHIC_24),
                     GRect(10, 19, b.size.w - 20, 30), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

static void prv_select(MenuLayer *m, MenuIndex *index, void *c) {
  Alarm a = prv_current();
  if (index->row == ROW_LENGTH && a.challenge == CHALLENGE_OFF) {
    ui_show_message(TR("Choose a mode first.", "Erst einen Modus wählen."));
    return;
  }
  if (index->row == ROW_PAUSE && a.challenge != CHALLENGE_FOLLOW) {
    ui_show_message(TR("Only for the Follow mode.", "Nur für den Modus „Folgen“."));
    return;
  }
  switch (index->row) {
    case ROW_MODE:
      for (int i = 0; i < CHALLENGE_COUNT; i++) s_mode_labels[i] = ui_challenge_name(i);
      option_picker_show(TR("Mode", "Modus"), s_mode_labels, CHALLENGE_COUNT, a.challenge,
                         prv_mode_done, NULL);
      break;
    case ROW_LENGTH: {
      int min = prv_len_min(&a), count = prv_len_max(&a) - min + 1;
      for (int i = 0; i < count; i++) {
        snprintf(s_length_text[i], sizeof(s_length_text[i]), "%d", min + i);
        s_length_labels[i] = s_length_text[i];
      }
      option_picker_show(TR("Length", "Länge"), s_length_labels, count,
                         prv_len(&a) - min, prv_length_done, NULL);
      break;
    }
    case ROW_PAUSE:
      for (int i = 0; i < CHALLENGE_PAUSE_COUNT; i++) s_pause_labels[i] = ui_pause_name(i);
      option_picker_show(TR("Pause", "Pause"), s_pause_labels, CHALLENGE_PAUSE_COUNT,
                         a.challenge_pause, prv_pause_done, NULL);
      break;
  }
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
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void prv_appear(Window *window) {
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void prv_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(window);
  s_window = NULL;
}

void challenge_menu_show(int slot) {
  if (s_window || !store_get(slot) || !store_get(slot)->used) return;
  s_slot = slot;
  s_window = window_create();
  window_set_background_color(s_window, theme()->bg);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .appear = prv_appear,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
