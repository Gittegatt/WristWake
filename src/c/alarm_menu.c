#include "alarm_menu.h"
#include "alarm_store.h"
#include "ui_common.h"
#include "time_picker.h"
#include "pickers.h"
#include "vibe_menu.h"
#include "volume_menu.h"
#include "challenge_menu.h"
#include "i18n.h"

// Day presets; "Custom" opens the checkbox list
enum { DAYS_ONCE = 0, DAYS_EVERY, DAYS_WEEKDAYS, DAYS_WEEKENDS, DAYS_CUSTOM, DAYS_OPTION_COUNT };
static const uint8_t DAYS_PRESET_MASK[DAYS_CUSTOM] = {0x00, 0x7F, 0x1F, 0x60};
static const char *s_days_labels[DAYS_OPTION_COUNT];

enum {
  ROW_ENABLE = 0,
  ROW_DELETE,
  ROW_TIME,
  ROW_DAYS,
  ROW_NAME,
  ROW_SOUND,
  ROW_VOLUME,
  ROW_VIBRATION,
  ROW_SNOOZE,
  ROW_CHALLENGE, // solve a button pattern before the alarm can be stopped
  ROW_PROTECT,   // protection against accidental deletion
  ROW_COUNT
};

#define HEADER_H 28
#define ROW_H 50   // 4 menu items per page: 28 + 4 * 50 = 228

static Window *s_window;
static MenuLayer *s_menu;
static int s_slot;
static DictationSession *s_dictation;
static char s_header[16 + ALARM_NAME_BUF];
static char s_subtitle[48];

static const char *s_sound_labels[ALARM_SOUND_COUNT];
static AlarmSound s_sound_order[ALARM_SOUND_COUNT];   // display order: "Off" first, then A-Z
static const char *s_snooze_labels[SNOOZE_OPTION_COUNT];

// Case-insensitive comparison (sound names are plain ASCII)
static int prv_name_cmp(const char *a, const char *b) {
  for (;; a++, b++) {
    char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    if (ca != cb || !ca) return ca - cb;
  }
}

// Fills s_sound_order / s_sound_labels and returns the row of the given sound
static int prv_sort_sounds(AlarmSound current) {
  int n = 0;
  s_sound_order[n++] = ALARM_SOUND_OFF;
  for (int s = 1; s < ALARM_SOUND_COUNT; s++) {
    int i = n++;
    while (i > 1 && prv_name_cmp(ui_sound_name(s), ui_sound_name(s_sound_order[i - 1])) < 0) {
      s_sound_order[i] = s_sound_order[i - 1];
      i--;
    }
    s_sound_order[i] = s;
  }
  int row = 0;
  for (int i = 0; i < ALARM_SOUND_COUNT; i++) {
    s_sound_labels[i] = ui_sound_name(s_sound_order[i]);
    if (s_sound_order[i] == current) row = i;
  }
  return row;
}

static Alarm prv_current(void) {
  return *store_get(s_slot);
}

static void prv_save(const Alarm *alarm) {
  store_save(s_slot, alarm);
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void prv_silent_hint(void) {
  ui_show_message(TR("Sound or vibration must stay on.", "Ton oder Vibration muss an bleiben."));
}

// ---------------------------------------------------------------------------
// Callbacks from sub-windows
// ---------------------------------------------------------------------------

static void prv_time_done(uint8_t hour, uint8_t minute, void *ctx) {
  Alarm a = prv_current();
  if (a.enabled && !ui_check_time_allowed(s_slot, hour, minute)) return;
  a.hour = hour;
  a.minute = minute;
  prv_save(&a);
}

static void prv_days_done(uint8_t days, void *ctx) {
  Alarm a = prv_current();
  a.days = days;
  prv_save(&a);
}

static void prv_days_preset_done(int index, void *ctx) {
  if (index == DAYS_CUSTOM) {
    days_picker_show(prv_current().days, prv_days_done, NULL);
    return;
  }
  prv_days_done(DAYS_PRESET_MASK[index], NULL);
}

static void prv_show_days_presets(void) {
  s_days_labels[DAYS_ONCE] = TR("Just Once", "Nur einmal");
  s_days_labels[DAYS_EVERY] = TR("Every Day", "Jeden Tag");
  s_days_labels[DAYS_WEEKDAYS] = TR("Weekdays", "Werktags");
  s_days_labels[DAYS_WEEKENDS] = TR("Weekends", "Wochenende");
  s_days_labels[DAYS_CUSTOM] = TR("Custom", "Eigene");
  uint8_t days = prv_current().days & 0x7F;
  int selected = DAYS_CUSTOM;
  for (int i = 0; i < DAYS_CUSTOM; i++) {
    if (DAYS_PRESET_MASK[i] == days) selected = i;
  }
  option_picker_show(TR("Days", "Tage"), s_days_labels, DAYS_OPTION_COUNT, selected,
                     prv_days_preset_done, NULL);
}

static void prv_sound_done(int index, void *ctx) {
  Alarm a = prv_current();
  AlarmSound sound = s_sound_order[index];
  if (sound == ALARM_SOUND_OFF && !a.vibrate) {
    prv_silent_hint();
    return;
  }
  a.sound = sound;
  prv_save(&a);
}

static void prv_snooze_done(int index, void *ctx) {
  Alarm a = prv_current();
  a.snooze = SNOOZE_OPTIONS[index];
  prv_save(&a);
}

static void prv_delete_yes(void *ctx) {
  store_delete(s_slot);
  window_stack_remove(s_window, true);
}

// Take the name from dictation: at most 24 characters, trailing punctuation removed.
static void prv_apply_name(const char *text) {
  Alarm a = prv_current();
  store_copy_name(a.name, text);
  prv_save(&a);
}

static void prv_dictation_result(DictationSession *session, DictationSessionStatus status,
                                 char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess && transcription) {
    prv_apply_name(transcription);
  } else if (status == DictationSessionStatusFailureConnectivityError) {
    ui_show_message(TR("No connection to the phone.", "Keine Verbindung zum Handy."));
  } else if (status == DictationSessionStatusFailureDisabled) {
    ui_show_message(TR("Dictation is turned off.", "Diktat ist ausgeschaltet."));
  }
}

static void prv_start_dictation(void) {
  if (!s_dictation) {
    s_dictation = dictation_session_create(256, prv_dictation_result, NULL);
  }
  if (!s_dictation) {
    ui_show_message(TR("Dictation not available. Is the phone connected?",
                       "Diktat nicht verfügbar. Ist das Handy verbunden?"));
    return;
  }
  dictation_session_start(s_dictation);
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

static uint16_t prv_rows(MenuLayer *m, uint16_t s, void *c) { return ROW_COUNT; }
static int16_t prv_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return ROW_H; }
static int16_t prv_header_h(MenuLayer *m, uint16_t s, void *c) { return HEADER_H; }

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  const Alarm *a = store_get(s_slot);
  GRect b = layer_get_bounds(cell);
  char time_buf[8];
  const char *ampm;
  ui_format_time(time_buf, sizeof(time_buf), a->hour, a->minute, false, &ampm);
  snprintf(s_header, sizeof(s_header), "%s%s%s  %s", time_buf, ampm ? " " : "", ampm ? ampm : "",
           a->name[0] ? a->name : "");
  ui_draw_header_bar(ctx, b, s_header, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), -3);
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *c) {
  const Alarm *a = store_get(s_slot);
  bool hl = menu_cell_layer_is_highlighted(cell);
  GRect b = layer_get_bounds(cell);
  const char *title = "";
  const char *subtitle = NULL;
  int toggle = -1;  // -1 = no toggle, 0/1 = toggle state

  switch (index->row) {
    case ROW_ENABLE:
      title = a->enabled ? TR("Disable", "Ausschalten") : TR("Enable", "Einschalten");
      toggle = a->enabled;
      break;
    case ROW_DELETE:
      title = TR("Delete", "Löschen");
      if (a->protect) subtitle = TR("Protected", "Geschützt");
      break;
    case ROW_TIME: {
      title = TR("Change Time", "Uhrzeit ändern");
      const char *ampm;
      char t[8];
      ui_format_time(t, sizeof(t), a->hour, a->minute, true, &ampm);
      snprintf(s_subtitle, sizeof(s_subtitle), "%s%s%s", t, ampm ? " " : "", ampm ? ampm : "");
      subtitle = s_subtitle;
      break;
    }
    case ROW_DAYS:
      title = TR("Change Days", "Tage ändern");
      ui_format_days(s_subtitle, sizeof(s_subtitle), a->days);
      subtitle = s_subtitle;
      break;
    case ROW_NAME:
      title = TR("Change Name", "Name ändern");
      subtitle = a->name[0] ? a->name : TR("(none)", "(kein Name)");
      break;
    case ROW_SOUND:
      title = TR("Sound", "Ton");
      subtitle = ui_sound_name(a->sound);
      break;
    case ROW_VOLUME:
      title = TR("Volume", "Lautstärke");
      subtitle = a->sound == ALARM_SOUND_OFF ? TR("Off", "Aus") : ui_volume_name(a->volume);
      break;
    case ROW_VIBRATION:
      title = TR("Vibration", "Vibration");
      subtitle = a->vibrate ? ui_vibe_name(a->vibe_pattern) : TR("Off", "Aus");
      break;
    case ROW_SNOOZE:
      title = TR("Snooze Delay", "Schlummerdauer");
      subtitle = ui_snooze_name(a->snooze);
      break;
    case ROW_CHALLENGE:
      title = TR("Challenge", "Weckaufgabe");
      if (a->challenge == CHALLENGE_FOLLOW) {
        snprintf(s_subtitle, sizeof(s_subtitle), "%s, %d, %s", ui_challenge_name(a->challenge),
                 a->challenge_len, ui_pause_name(a->challenge_pause));
        subtitle = s_subtitle;
      } else {
        subtitle = ui_challenge_name(a->challenge);
      }
      break;
    case ROW_PROTECT:
      title = TR("Protection", "Schutz");
      toggle = a->protect;
      break;
  }
  // Protected alarm: show "Delete" greyed out
  bool greyed = (index->row == ROW_DELETE && a->protect) ||
                (index->row == ROW_VOLUME && a->sound == ALARM_SOUND_OFF);

  int text_w = b.size.w - 16 - (toggle >= 0 ? UI_TOGGLE_W + 10 : 0);
  const Theme *t = theme();
  graphics_context_set_text_color(ctx, greyed ? (hl ? t->hl_dim : t->dim) : (hl ? t->hl_fg : t->fg));
  // All menu items in the same font size; details below slightly smaller and not bold,
  // in the same (high-contrast) color as the title. Only greyed-out rows are dimmed.
  if (subtitle) {
    graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       GRect(10, -7, text_w, 34), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, subtitle, fonts_get_system_font(FONT_KEY_GOTHIC_24),
                       GRect(10, 19, text_w, 30), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
  } else {
    graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       GRect(10, (b.size.h - 36) / 2, text_w, 36),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
  if (toggle >= 0) {
    ui_draw_toggle(ctx, GPoint(b.size.w - UI_TOGGLE_W - 10, (b.size.h - UI_TOGGLE_H) / 2),
                   toggle, hl);
  }
}

static void prv_select(MenuLayer *m, MenuIndex *index, void *c) {
  Alarm a = prv_current();
  switch (index->row) {
    case ROW_ENABLE:
      if (!a.enabled && !ui_check_time_allowed(s_slot, a.hour, a.minute)) break;
      a.enabled = !a.enabled;
      prv_save(&a);
      break;
    case ROW_DELETE:
      if (a.protect) {
        ui_show_message(TR("This alarm is protected. Turn off Protection first.",
                           "Dieser Wecker ist geschützt. Schalte zuerst den Schutz aus."));
        break;
      }
      ui_confirm(TR("Delete this alarm?", "Diesen Wecker löschen?"), prv_delete_yes, NULL);
      break;
    case ROW_TIME:
      time_picker_show(TR("Change Time", "Uhrzeit ändern"), a.hour, a.minute, prv_time_done, NULL);
      break;
    case ROW_DAYS:
      prv_show_days_presets();
      break;
    case ROW_NAME:
      prv_start_dictation();
      break;
    case ROW_SOUND:
      option_picker_show(TR("Sound", "Ton"), s_sound_labels, ALARM_SOUND_COUNT,
                         prv_sort_sounds(a.sound), prv_sound_done, NULL);
      break;
    case ROW_VOLUME:
      if (a.sound == ALARM_SOUND_OFF) {
        ui_show_message(TR("Choose a sound first.", "Zuerst einen Ton wählen."));
      } else {
        volume_menu_show(s_slot);  // levels with a preview of the sound
      }
      break;
    case ROW_VIBRATION:
      vibe_menu_show(s_slot);  // on/off and pattern
      break;
    case ROW_SNOOZE: {
      int selected = 0;
      for (int i = 0; i < SNOOZE_OPTION_COUNT; i++) {
        s_snooze_labels[i] = ui_snooze_name(SNOOZE_OPTIONS[i]);
        if (SNOOZE_OPTIONS[i] == a.snooze) selected = i;
      }
      option_picker_show(TR("Snooze Delay", "Schlummerdauer"), s_snooze_labels,
                         SNOOZE_OPTION_COUNT, selected, prv_snooze_done, NULL);
      break;
    }
    case ROW_CHALLENGE:
      challenge_menu_show(s_slot);  // mode, length and pause
      break;
    case ROW_PROTECT:
      a.protect = !a.protect;
      prv_save(&a);
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
  if (s_dictation) {
    dictation_session_destroy(s_dictation);
    s_dictation = NULL;
  }
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  window_destroy(window);
  s_window = NULL;
}

void alarm_menu_show(int slot) {
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
