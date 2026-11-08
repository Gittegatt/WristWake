#include "ui_common.h"
#include "scheduler.h"
#include "i18n.h"

bool ui_check_time_allowed(int slot, uint8_t hour, uint8_t minute) {
  if (!scheduler_time_conflict(slot, hour, minute)) return true;
  ui_show_message(TR("Too close to another alarm. Keep at least 2 minutes apart.",
                     "Zu nah an einem anderen Wecker. Bitte mind. 2 Minuten Abstand."));
  return false;
}

// ---------------------------------------------------------------------------
// Texts
// ---------------------------------------------------------------------------

void ui_format_time(char *buf, size_t size, uint8_t hour, uint8_t minute,
                    bool leading_zero, const char **ampm) {
  if (clock_is_24h_style()) {
    snprintf(buf, size, leading_zero ? "%02d:%02d" : "%d:%02d", hour, minute);
    if (ampm) *ampm = NULL;
  } else {
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, size, "%d:%02d", h12, minute);
    if (ampm) *ampm = hour < 12 ? "AM" : "PM";
  }
}

static const char *const DAY_SHORT_EN[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
static const char *const DAY_SHORT_DE[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
static const char *const DAY_LONG_EN[7] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                           "Friday", "Saturday", "Sunday"};
static const char *const DAY_LONG_DE[7] = {"Montag", "Dienstag", "Mittwoch", "Donnerstag",
                                           "Freitag", "Samstag", "Sonntag"};

const char *ui_day_name(int index) {
  if (index < 0 || index > 6) return "";
  return g_lang_de ? DAY_LONG_DE[index] : DAY_LONG_EN[index];
}

void ui_format_days(char *buf, size_t size, uint8_t days) {
  days &= 0x7F;
  if (days == 0) {
    snprintf(buf, size, "%s", TR("Once", "Einmalig"));
  } else if (days == 0x7F) {
    snprintf(buf, size, "%s", TR("Every day", "Täglich"));
  } else if (days == 0x1F) {
    snprintf(buf, size, "%s", TR("Mon–Fri", "Mo–Fr"));
  } else if (days == 0x60) {
    snprintf(buf, size, "%s", TR("Weekend", "Wochenende"));
  } else {
    size_t len = 0;
    buf[0] = '\0';
    for (int i = 0; i < 7 && len < size; i++) {
      if (!(days & (1 << i))) continue;
      len += snprintf(buf + len, size - len, "%s%s", len ? ", " : "",
                      g_lang_de ? DAY_SHORT_DE[i] : DAY_SHORT_EN[i]);
    }
  }
}

const char *ui_pause_name(int index) {
  switch (index) {
    case 0:  return TR("0.5 s", "0,5 s");
    case 2:  return "2 s";
    case 3:  return "3 s";
    case 4:  return "4 s";
    default: return "1 s";
  }
}

const char *ui_challenge_name(AlarmChallenge challenge) {
  switch (challenge) {
    case CHALLENGE_FOLLOW: return TR("Follow", "Folgen");
    case CHALLENGE_REMEMBER: return TR("Remember", "Merken");
    default:               return TR("Off", "Aus");
  }
}

const char *ui_volume_name(AlarmVolume volume) {
  switch (volume) {
    case ALARM_VOLUME_QUIET: return TR("Quiet", "Sehr leise");
    case ALARM_VOLUME_LOW:   return TR("Low", "Leise");
    case ALARM_VOLUME_HIGH:  return TR("High", "Laut");
    default:                 return TR("Medium", "Mittel");
  }
}

const char *ui_sound_name(AlarmSound sound) {
  switch (sound) {
    case ALARM_SOUND_REVEILLE: return "Reveille";
    case ALARM_SOUND_BEEP:     return "Beep";
    case ALARM_SOUND_CHIME:    return "Chime";
    case ALARM_SOUND_RISING:   return "Rising Beep";
    case ALARM_SOUND_KOROBEINIKI: return "Korobeiniki";
    case ALARM_SOUND_WESTMINSTER: return "Westminster";
    case ALARM_SOUND_FIELD_AGENT: return "Field Agent";
    case ALARM_SOUND_MAPLE_LEAF:  return "Maple Leaf";
    case ALARM_SOUND_RONDO:       return "Rondo";
    case ALARM_SOUND_CLASSIC_RING: return "Classic Ring";
    case ALARM_SOUND_DIGITAL:     return "Digital";
    case ALARM_SOUND_SUNRISE:     return "Rising Sunrise";
    case ALARM_SOUND_SIREN:       return "Siren";
    case ALARM_SOUND_CHIRP:       return "Chirp";
    case ALARM_SOUND_PULSE:       return "Pulse";
    case ALARM_SOUND_DAYBREAK:    return "Rising Daybreak";
    case ALARM_SOUND_BREEZE:      return "Rising Breeze";
    case ALARM_SOUND_BLOOM:       return "Rising Bloom";
    default:                   return TR("Off", "Aus");
  }
}

const char *ui_vibe_name(int pattern) {
  switch (pattern) {
    case VIBE_PULSE:     return TR("Pulse", "Puls");
    case VIBE_DOUBLE:    return TR("Double", "Doppelt");
    case VIBE_HEARTBEAT: return TR("Heartbeat", "Herzschlag");
    case VIBE_RISING:    return TR("Rising", "Steigend");
    case VIBE_SOS:       return "SOS";
    case VIBE_LONG:      return TR("Long", "Lang");
    case VIBE_GENTLE:    return TR("Gentle", "Sanft");
    case VIBE_AURORA:    return "Aurora";
    default:             return "Standard";
  }
}

const char *ui_snooze_name(uint8_t minutes) {
  switch (minutes) {
    case 0:  return TR("Off", "Aus");
    case 10: return "10 min";
    case 15: return "15 min";
    case 30: return "30 min";
    case 60: return TR("1 hour", "1 Std.");
    default: return "5 min";
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void ui_draw_day_letters(GContext *ctx, GRect rect, uint8_t days, bool highlighted, bool dimmed) {
  const Theme *t = theme();
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  if ((days & 0x7F) == 0) {
    graphics_context_set_text_color(ctx, highlighted ? (dimmed ? t->hl_dim : t->hl_fg)
                                                     : (dimmed ? t->dim : t->fg));
    graphics_draw_text(ctx, TR("Once", "Einmal"), font, rect,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  const char *letters = TR("MTWTFSS", "MDMDFSS");
  int step = rect.size.w / 7;
  char letter[2] = {0, 0};
  for (int i = 0; i < 7; i++) {
    bool active = days & (1 << i);
    GColor color;
    if (highlighted) {
      color = active ? (dimmed ? t->hl_dim : t->hl_fg) : t->hl_faint;
    } else {
      color = active ? (dimmed ? t->dim : t->fg) : t->faint;
    }
    graphics_context_set_text_color(ctx, color);
    letter[0] = letters[i];
    // Wide letters (M, W) need more room than one step: widen the box around the center
    int center = rect.origin.x + i * step + step / 2;
    graphics_draw_text(ctx, letter, font, GRect(center - 8, rect.origin.y, 16, rect.size.h),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

void ui_draw_toggle(GContext *ctx, GPoint origin, bool on, bool highlighted) {
  GRect track = GRect(origin.x, origin.y, UI_TOGGLE_W, UI_TOGGLE_H);
  int r = UI_TOGGLE_H / 2;
  const Theme *t = theme();
  GColor fg = highlighted ? t->hl_fg : t->fg;
  GColor bg = highlighted ? t->hl_bg : t->bg;
  GColor off = highlighted ? t->hl_dim : t->faint;
  if (on) {
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, track, r, GCornersAll);
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_circle(ctx, GPoint(origin.x + UI_TOGGLE_W - r, origin.y + r), r - 3);
  } else {
    graphics_context_set_stroke_color(ctx, off);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, track, r);
    graphics_context_set_fill_color(ctx, off);
    graphics_fill_circle(ctx, GPoint(origin.x + r, origin.y + r), r - 4);
    graphics_context_set_stroke_width(ctx, 1);
  }
}

static void prv_draw_check(GContext *ctx, GPoint o, int size, GColor color, int width) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  GPoint a = GPoint(o.x + size * 2 / 10, o.y + size * 5 / 10);
  GPoint b = GPoint(o.x + size * 4 / 10, o.y + size * 75 / 100);
  GPoint c = GPoint(o.x + size * 8 / 10, o.y + size * 25 / 100);
  graphics_draw_line(ctx, a, b);
  graphics_draw_line(ctx, b, c);
  graphics_context_set_stroke_width(ctx, 1);
}

void ui_draw_checkbox(GContext *ctx, GPoint origin, bool checked, bool highlighted) {
  const Theme *t = theme();
  GColor fg = highlighted ? t->hl_fg : t->fg;
  GColor bg = highlighted ? t->hl_bg : t->bg;
  GRect box = GRect(origin.x, origin.y, 16, 16);
  if (checked) {
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, box, 3, GCornersAll);
    prv_draw_check(ctx, origin, 16, bg, 2);
  } else {
    graphics_context_set_stroke_color(ctx, highlighted ? t->hl_dim : t->dim);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, box, 3);
    graphics_context_set_stroke_width(ctx, 1);
  }
}

void ui_draw_radio(GContext *ctx, GPoint center, bool selected, bool highlighted) {
  const Theme *t = theme();
  GColor fg = highlighted ? t->hl_fg : t->fg;
  graphics_context_set_stroke_color(ctx, selected ? fg : (highlighted ? t->hl_dim : t->dim));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, center, 7);
  graphics_context_set_stroke_width(ctx, 1);
  if (selected) {
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_circle(ctx, center, 4);
  }
}

void ui_style_menu(MenuLayer *menu) {
  const Theme *t = theme();
  menu_layer_set_normal_colors(menu, t->bg, t->fg);
  menu_layer_set_highlight_colors(menu, t->hl_bg, t->hl_fg);
}

GColor ui_header_fg(void) {
  return theme()->header_fg;
}

// Header in the selection colors (see theme.c), with a 2 pixel strip in the background color
// at the bottom, so it stands out from a highlighted row of the same color.
void ui_draw_header_bar(GContext *ctx, GRect bounds, const char *title, GFont font, int text_y) {
  const Theme *t = theme();
  graphics_context_set_fill_color(ctx, t->header_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, t->bg);
  graphics_fill_rect(ctx, GRect(0, bounds.size.h - 2, bounds.size.w, 2), 0, GCornerNone);
  graphics_context_set_text_color(ctx, ui_header_fg());
  graphics_draw_text(ctx, title, font, GRect(8, text_y, bounds.size.w - 16, bounds.size.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ---------------------------------------------------------------------------
// Short message
// ---------------------------------------------------------------------------

static Window *s_msg_window;
static TextLayer *s_msg_text;
static AppTimer *s_msg_timer;
static char s_msg_buf[96];

static void prv_msg_tap(const Recognizer *recognizer, RecognizerEvent event);

static void prv_msg_timeout(void *context) {
  s_msg_timer = NULL;
  if (s_msg_window) window_stack_remove(s_msg_window, true);
}

static void prv_msg_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_msg_text = text_layer_create(GRect(10, b.size.h / 2 - 50, b.size.w - 20, 100));
  text_layer_set_font(s_msg_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_msg_text, GTextAlignmentCenter);
  text_layer_set_background_color(s_msg_text, GColorClear);
  text_layer_set_text_color(s_msg_text, theme()->header_fg);
  text_layer_set_text(s_msg_text, s_msg_buf);
  layer_add_child(root, text_layer_get_layer(s_msg_text));
  window_set_touch_bridge_disabled(window, true);
  window_attach_recognizer(window, tap_recognizer_create(prv_msg_tap, NULL));
}

// Close the message by tapping
static void prv_msg_tap(const Recognizer *recognizer, RecognizerEvent event) {
  if (event == RecognizerEvent_Completed && s_msg_window) window_stack_remove(s_msg_window, true);
}

static void prv_msg_unload(Window *window) {
  if (s_msg_timer) {
    app_timer_cancel(s_msg_timer);
    s_msg_timer = NULL;
  }
  text_layer_destroy(s_msg_text);
  window_destroy(window);
  s_msg_window = NULL;
}

void ui_show_message(const char *text) {
  snprintf(s_msg_buf, sizeof(s_msg_buf), "%s", text);
  if (s_msg_window) {
    text_layer_set_text(s_msg_text, s_msg_buf);
    if (s_msg_timer) app_timer_reschedule(s_msg_timer, 2000);
    return;
  }
  s_msg_window = window_create();
  window_set_background_color(s_msg_window, theme()->header_bg);
  window_set_window_handlers(s_msg_window, (WindowHandlers) {
    .load = prv_msg_load,
    .unload = prv_msg_unload,
  });
  window_stack_push(s_msg_window, true);
  s_msg_timer = app_timer_register(2000, prv_msg_timeout, NULL);
}

// ---------------------------------------------------------------------------
// Confirmation (yes/no)
// ---------------------------------------------------------------------------

static Window *s_confirm_window;
static Layer *s_confirm_layer;
static char s_confirm_question[64];
static UiConfirmCallback s_confirm_cb;
static void *s_confirm_ctx;

static void prv_confirm_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  const Theme *t = theme();
  int x0 = theme_left_handed() ? 30 : 0;   // content next to the bar
  // question
  graphics_context_set_text_color(ctx, t->fg);
  graphics_draw_text(ctx, s_confirm_question, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(x0 + 8, 30, b.size.w - 44, 100), GTextOverflowModeWordWrap,
                     GTextAlignmentLeft, NULL);
  // hint at the bottom
  graphics_context_set_text_color(ctx, t->dim);
  graphics_draw_text(ctx, TR("BACK = cancel", "ZURÜCK = abbrechen"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(x0 + 8, b.size.h - 30, b.size.w - 46, 24), GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
  // check mark next to the SELECT button (middle of the button edge)
  GRect bar = GRect(theme_button_edge_x(b.size.w, 30), 0, 30, b.size.h);
  graphics_context_set_fill_color(ctx, t->hl_bg);
  graphics_fill_rect(ctx, bar, 0, GCornerNone);
  prv_draw_check(ctx, GPoint(bar.origin.x + 4, b.size.h / 2 - 11), 22, t->hl_fg, 3);
}

static void prv_confirm_select(ClickRecognizerRef recognizer, void *context) {
  UiConfirmCallback cb = s_confirm_cb;
  void *cb_ctx = s_confirm_ctx;
  window_stack_remove(s_confirm_window, false);
  if (cb) cb(cb_ctx);
}

static void prv_confirm_clicks(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_confirm_select);
}

// Touch: tap the check mark bar = yes, anywhere else = cancel
static void prv_confirm_tap(const Recognizer *recognizer, RecognizerEvent event) {
  if (event != RecognizerEvent_Completed) return;
  GPoint p = tap_recognizer_get_tap_point(recognizer);
  GRect b = layer_get_bounds(s_confirm_layer);
  if (theme_left_handed() ? p.x < 40 : p.x >= b.size.w - 40) {
    prv_confirm_select(NULL, NULL);
  } else {
    window_stack_remove(s_confirm_window, true);
  }
}

static void prv_confirm_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_confirm_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_confirm_layer, prv_confirm_draw);
  layer_add_child(root, s_confirm_layer);
  window_set_touch_bridge_disabled(window, true);
  window_attach_recognizer(window, tap_recognizer_create(prv_confirm_tap, NULL));
}

static void prv_confirm_unload(Window *window) {
  layer_destroy(s_confirm_layer);
  window_destroy(window);
  s_confirm_window = NULL;
}

void ui_confirm(const char *question, UiConfirmCallback on_yes, void *context) {
  if (s_confirm_window) return;
  snprintf(s_confirm_question, sizeof(s_confirm_question), "%s", question);
  s_confirm_cb = on_yes;
  s_confirm_ctx = context;
  s_confirm_window = window_create();
  window_set_background_color(s_confirm_window, theme()->bg);
  window_set_click_config_provider(s_confirm_window, prv_confirm_clicks);
  window_set_window_handlers(s_confirm_window, (WindowHandlers) {
    .load = prv_confirm_load,
    .unload = prv_confirm_unload,
  });
  window_stack_push(s_confirm_window, true);
}
