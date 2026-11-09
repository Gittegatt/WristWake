#include "ring.h"
#include "alarm_store.h"
#include "scheduler.h"
#include "alert.h"
#include "comms.h"
#include "ui_common.h"
#include "i18n.h"

#define AUTO_SNOOZE_MS (60 * 1000)
#define MAX_RING_MS (30 * 60 * 1000)   // snooze off: stop after 30 minutes (battery)
#define HOLD_MS 1500         // hold the bottom button this long = stop
#define TAP_MS 300           // released sooner = snooze
#define HOLD_FRAME_MS 33     // about 30 frames per second
#define HOLD_REWIND_SPEED 4  // rewind after early release: 4x as fast
#define CLOSE_DELAY_MS 3000  // some time for the phone to send the state to Home Assistant
#define ACTION_BAR_W 30

typedef enum { RING_ACTIVE, RING_CHALLENGE, RING_DONE } RingState;

// Challenge: a random sequence of buttons that must be pressed before the alarm stops
#define CHALLENGE_REMEMBER_LEN 4   // Remember always has 4 boxes (they fit on the screen)
#define CHALLENGE_IDLE_MS (30 * 1000)   // no input: back to the ringing screen
enum { CH_UP = 0, CH_SELECT, CH_DOWN };

static Window *s_window;
static Layer *s_layer;
static uint8_t s_mask;
static RingState s_state;
static AppTimer *s_timer;
static char s_names[2 * ALARM_NAME_BUF + 8];
static char s_result[48];

// Holding the bottom button: progress 0-1000, the bar fills from the bottom
static int s_hold_progress;
static bool s_holding;
static uint32_t s_hold_start_ms;
static AppTimer *s_hold_timer;

// Test alarm (from the settings page): rings with the given values, changes nothing
static bool s_test;
static AlertParams s_test_params;
static bool s_test_can_snooze;
static RingChallenge s_test_challenge;

static uint8_t s_challenge[CHALLENGE_LEN_MAX];
static int s_challenge_len;          // number of buttons of the running challenge
static int s_challenge_pause_ms;     // Follow: pause before the next button appears
static bool s_challenge_waiting;     // Follow: in the pause (no button shown, input ignored)
static AppTimer *s_pause_timer;
static int s_challenge_pos;          // number of correctly pressed buttons
static int s_challenge_mode;         // AlarmChallenge of the running challenge
static AppTimer *s_challenge_timer;

static bool prv_in_mask(int slot) {
  return s_mask & (1 << (slot - 1));
}

static int prv_first_slot(void) {
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    if (prv_in_mask(slot)) return slot;
  }
  return 0;
}

// Snoozing is possible only if no ringing alarm has snooze off or a challenge. Without
// snoozing, only holding the bottom button stops the alarm; everything else keeps it ringing.
static bool prv_can_snooze(void) {
  if (s_test) return s_test_can_snooze && s_test_challenge.mode == CHALLENGE_OFF;
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    const Alarm *a = store_get(slot);
    if (prv_in_mask(slot) && a->used && (a->snooze == 0 || a->challenge != CHALLENGE_OFF)) {
      return false;
    }
  }
  return true;
}

// Challenge of the ringing alarms (the hardest one wins: Remember before Follow)
static RingChallenge prv_needs_challenge(void) {
  if (s_test) return s_test_challenge;
  RingChallenge ch = {CHALLENGE_OFF, DEFAULT_CHALLENGE_LEN, 1000};
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    const Alarm *a = store_get(slot);
    if (prv_in_mask(slot) && a->used && a->challenge > ch.mode) {
      ch.mode = a->challenge;
      ch.length = a->challenge_len;
      ch.pause_ms = CHALLENGE_PAUSES_MS[a->challenge_pause];
    }
  }
  return ch;
}

bool ring_is_active(void) {
  return s_window != NULL;
}

static void prv_build_names(void) {
  if (s_test) {
    snprintf(s_names, sizeof(s_names), "Test");
    return;
  }
  s_names[0] = '\0';
  size_t len = 0;
  int shown = 0;
  for (int slot = 1; slot <= ALARM_SLOTS && shown < 2; slot++) {
    if (!prv_in_mask(slot)) continue;
    const Alarm *a = store_get(slot);
    if (!a->used || !a->name[0]) continue;
    len += snprintf(s_names + len, sizeof(s_names) - len, "%s%s", len ? "\n" : "", a->name);
    shown++;
  }
  if (!shown) snprintf(s_names, sizeof(s_names), "%s", TR("Alarm", "Wecker"));
}

// Sound/vibration from the settings of the ringing alarms
static void prv_start_alert(void) {
  if (s_test) {
    AlertParams t = s_test_params;
    if (!t.vibrate && speaker_is_muted()) t.vibrate = true;   // muted: vibrate instead
    alert_start(&t);
    return;
  }
  AlertParams p = {.vibe_pattern = DEFAULT_VIBE_PATTERN, .vibrate = false,
                   .sound = ALARM_SOUND_OFF, .volume = DEFAULT_VOLUME};
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    if (!prv_in_mask(slot)) continue;
    const Alarm *a = store_get(slot);
    if (!a->used) continue;
    if (a->vibrate && !p.vibrate) {
      p.vibrate = true;
      p.vibe_pattern = a->vibe_pattern;
    }
    if (a->sound != ALARM_SOUND_OFF && p.sound == ALARM_SOUND_OFF) {
      p.sound = a->sound;
      p.volume = a->volume;
    }
  }
  // Never silent: without a sound, or with the speaker muted (always or during Quiet Time),
  // the watch vibrates even if vibration is off for the alarm
  if (!p.vibrate && (p.sound == ALARM_SOUND_OFF || speaker_is_muted())) p.vibrate = true;
  alert_start(&p);
}

static void prv_close(void *context) {
  s_timer = NULL;
  if (s_window) window_stack_remove(s_window, true);
}

static void prv_finish(const char *text) {
  alert_stop();
  if (s_challenge_timer) {
    app_timer_cancel(s_challenge_timer);
    s_challenge_timer = NULL;
  }
  if (s_pause_timer) {
    app_timer_cancel(s_pause_timer);
    s_pause_timer = NULL;
  }
  if (s_timer) app_timer_cancel(s_timer);
  s_state = RING_DONE;
  snprintf(s_result, sizeof(s_result), "%s", text);
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(CLOSE_DELAY_MS, prv_close, NULL);
}

static void prv_snooze(void) {
  if (s_state != RING_ACTIVE || !prv_can_snooze()) return;
  if (s_test) {  // test alarm: store nothing, report nothing
    prv_finish(TR("Test ended", "Test beendet"));
    return;
  }
  time_t now = time(NULL);
  time_t minute_start = now - (now % 60);
  time_t earliest = 0;
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    if (!prv_in_mask(slot) || !store_get(slot)->used) continue;
    store_set_snooze_until(slot, minute_start + store_get(slot)->snooze * 60);
  }
  scheduler_update();
  // If the snooze time is right next to another alarm, shift it by 1 minute
  for (int tries = 0; tries < 3 && (scheduler_failed_mask() & s_mask); tries++) {
    for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
      if (prv_in_mask(slot) && (scheduler_failed_mask() & (1 << (slot - 1)))) {
        store_set_snooze_until(slot, store_snooze_until(slot) + 60);
      }
    }
    scheduler_update();
  }
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    time_t t = prv_in_mask(slot) ? store_snooze_until(slot) : 0;
    if (t && (!earliest || t < earliest)) earliest = t;
  }
  char buf[48];
  if (earliest) {
    struct tm *lt = localtime(&earliest);
    char t[8];
    const char *ampm;
    ui_format_time(t, sizeof(t), lt->tm_hour, lt->tm_min, true, &ampm);
    snprintf(buf, sizeof(buf), "%s\n%s%s%s", TR("Snoozed until", "Schlummern bis"), t,
             ampm ? " " : "", ampm ? ampm : "");
  } else {
    snprintf(buf, sizeof(buf), "%s", TR("Snoozed", "Schlummern"));
  }
  comms_set_status(ALARM_STATUS_SNOOZED, prv_first_slot());
  prv_finish(buf);
}

static void prv_dismiss(void) {
  if (s_state != RING_ACTIVE && s_state != RING_CHALLENGE) return;
  if (s_test) {
    prv_finish(TR("Test ended", "Test beendet"));
    return;
  }
  store_batch_begin();
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    if (!prv_in_mask(slot)) continue;
    store_set_snooze_until(slot, 0);
    Alarm a = *store_get(slot);
    if (a.used && a.days == 0 && a.enabled) {
      a.enabled = 0;  // a one-time alarm turns itself off after ringing
      store_save(slot, &a);
    }
  }
  store_batch_end();
  scheduler_update();
  comms_set_status(ALARM_STATUS_DISMISSED, prv_first_slot());
  prv_finish(TR("Alarm stopped", "Wecker beendet"));
}

static void prv_auto_snooze(void *context) {
  s_timer = NULL;
  prv_snooze();
}

// Snooze off: no auto snooze; the alarm keeps ringing and stops itself after MAX_RING_MS
static void prv_max_ring(void *context) {
  s_timer = NULL;
  prv_dismiss();
}

static void prv_start_timer(void) {
  if (prv_can_snooze()) {
    s_timer = app_timer_register(AUTO_SNOOZE_MS, prv_auto_snooze, NULL);
  } else {
    s_timer = app_timer_register(MAX_RING_MS, prv_max_ring, NULL);
  }
}

// ---------------------------------------------------------------------------
// Display and buttons
// ---------------------------------------------------------------------------

// Symbol of a challenge button: "+" (top button), circle (middle), "-" (bottom button)
static void prv_draw_symbol(GContext *ctx, GPoint c, int button, int size, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int w = size / 3 > 3 ? size / 3 : 3;   // bar thickness
  if (button == CH_SELECT) {
    graphics_fill_circle(ctx, c, size * 2 / 3);
    return;
  }
  graphics_fill_rect(ctx, GRect(c.x - size, c.y - w / 2, 2 * size + 1, w), 0, GCornerNone);
  if (button == CH_UP) {
    graphics_fill_rect(ctx, GRect(c.x - w / 2, c.y - size, w, 2 * size + 1), 0, GCornerNone);
  }
}

static void prv_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  const Theme *t = theme();
  graphics_context_set_fill_color(ctx, t->bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (s_state == RING_CHALLENGE) {
    graphics_context_set_text_color(ctx, t->fg);
    bool remember = (s_challenge_mode == CHALLENGE_REMEMBER);
    graphics_draw_text(ctx, remember ? TR("Guess the buttons:", "Tasten erraten:") : TR("Press:", "Drücke:"),
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(8, 14, b.size.w - 16, 30), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    if (remember) {
      // Remember: one box per step; "?" until guessed, the current box is filled
      int slot_w = (b.size.w - 16) / s_challenge_len;
      for (int i = 0; i < s_challenge_len; i++) {
        GPoint c = GPoint(8 + slot_w * i + slot_w / 2, 104);
        if (i == s_challenge_pos) {
          graphics_context_set_fill_color(ctx, t->hl_bg);   // current box in the selection color
          graphics_fill_rect(ctx, GRect(c.x - 20, c.y - 24, 40, 48), 8, GCornersAll);
        }
        if (i < s_challenge_pos) {
          prv_draw_symbol(ctx, c, s_challenge[i], 12, t->fg);
        } else {
          graphics_context_set_text_color(ctx, i == s_challenge_pos ? t->hl_fg : t->dim);
          graphics_draw_text(ctx, "?", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                             GRect(c.x - 20, c.y - 21, 40, 36), GTextOverflowModeFill,
                             GTextAlignmentCenter, NULL);
        }
      }
      return;
    }
    // Follow: only the button that is required now is shown, large in the middle
    GPoint c = GPoint(b.size.w / 2, 100);
    graphics_context_set_fill_color(ctx, t->hl_bg);
    graphics_fill_rect(ctx, GRect(c.x - 40, c.y - 40, 80, 80), 10, GCornersAll);
    if (!s_challenge_waiting) prv_draw_symbol(ctx, c, s_challenge[s_challenge_pos], 24, t->hl_fg);
    // Progress: one small dot per step, filled when done (reveals no direction)
    int dot_gap = 18;
    int dots_x = b.size.w / 2 - (s_challenge_len - 1) * dot_gap / 2;
    for (int i = 0; i < s_challenge_len; i++) {
      GPoint dp = GPoint(dots_x + i * dot_gap, 162);
      if (i < s_challenge_pos) {
        graphics_context_set_fill_color(ctx, t->fg);
        graphics_fill_circle(ctx, dp, 5);
      } else {
        graphics_context_set_stroke_color(ctx, t->dim);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_circle(ctx, dp, 5);
        graphics_context_set_stroke_width(ctx, 1);
      }
    }
    return;
  }

  if (s_state == RING_DONE) {
    graphics_context_set_text_color(ctx, t->fg);
    graphics_draw_text(ctx, s_result, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       GRect(8, b.size.h / 2 - 40, b.size.w - 16, 80), GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    return;
  }

  // Narrow bar at the button edge (like the built-in alarm app), on the left in left-handed mode
  GRect bar = GRect(theme_button_edge_x(b.size.w, ACTION_BAR_W), 0, ACTION_BAR_W, b.size.h);
  int cx0 = theme_left_handed() ? ACTION_BAR_W : 0;   // start of the content next to the bar
  graphics_context_set_fill_color(ctx, t->hl_bg);
  graphics_fill_rect(ctx, bar, 0, GCornerNone);
  // While the bottom button is held, the bar fills from the bottom in the inverse color
  int fill_h = b.size.h * s_hold_progress / 1000;
  int fill_top = b.size.h - fill_h;
  if (fill_h > 0) {
    graphics_context_set_fill_color(ctx, t->bg);
    graphics_fill_rect(ctx, GRect(bar.origin.x, fill_top, ACTION_BAR_W, fill_h), 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, t->hl_bg);
    graphics_draw_rect(ctx, GRect(bar.origin.x, fill_top, ACTION_BAR_W, fill_h));
  }
  int cx = bar.origin.x + ACTION_BAR_W / 2;
  bool can_snooze = prv_can_snooze();
  // "zZ" between the top and the middle button (not shown when snooze is off)
  if (can_snooze) {
    int zz_y = b.size.h * 36 / 100 - 16;
    graphics_context_set_text_color(ctx, zz_y + 14 >= fill_top ? t->fg : t->hl_fg);
    graphics_draw_text(ctx, "zZ", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(bar.origin.x, zz_y, ACTION_BAR_W, 28),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  // check mark next to the bottom button
  int cy = b.size.h * 78 / 100;
  graphics_context_set_stroke_color(ctx, cy >= fill_top ? t->fg : t->hl_fg);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(cx - 8, cy), GPoint(cx - 2, cy + 7));
  graphics_draw_line(ctx, GPoint(cx - 2, cy + 7), GPoint(cx + 9, cy - 7));
  graphics_context_set_stroke_width(ctx, 1);

  int content_w = b.size.w - ACTION_BAR_W;

  // current time, large
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  char time_buf[8];
  const char *ampm;
  ui_format_time(time_buf, sizeof(time_buf), lt->tm_hour, lt->tm_min, false, &ampm);
  graphics_context_set_text_color(ctx, t->fg);
  graphics_draw_text(ctx, time_buf, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
                     GRect(cx0, 26, content_w, 50), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  if (ampm) {
    graphics_draw_text(ctx, ampm, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(cx0, 76, content_w, 22), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }

  // name(s)
  graphics_draw_text(ctx, s_names, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(cx0 + 6, 98, content_w - 12, 64), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  // hints at the bottom
  graphics_context_set_text_color(ctx, t->dim);
  graphics_draw_text(ctx, can_snooze ? TR("Any button: snooze\nHold bottom button: stop",
                                          "Taste: Schlummern\nUntere Taste halten: Aus")
                                     : TR("Hold bottom button: stop", "Untere Taste halten: Aus"),
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(cx0 + 4, b.size.h - 40, content_w - 8, 36), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

static void prv_challenge_idle(void *context);

static void prv_challenge_new(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  srand((unsigned int)(s * 1000 + ms));
  for (int i = 0; i < s_challenge_len; i++) s_challenge[i] = rand() % 3;
  s_challenge_pos = 0;
  s_challenge_waiting = false;
}

static void prv_challenge_touch_timer(void) {
  if (s_challenge_timer) app_timer_cancel(s_challenge_timer);
  s_challenge_timer = app_timer_register(CHALLENGE_IDLE_MS, prv_challenge_idle, NULL);
}

static void prv_challenge_start(const RingChallenge *ch) {
  s_challenge_mode = ch->mode;
  s_challenge_len = ch->mode == CHALLENGE_REMEMBER ? CHALLENGE_REMEMBER_LEN : ch->length;
  if (s_challenge_len < CHALLENGE_LEN_MIN || s_challenge_len > CHALLENGE_LEN_MAX) {
    s_challenge_len = DEFAULT_CHALLENGE_LEN;
  }
  s_challenge_pause_ms = ch->pause_ms;
  s_state = RING_CHALLENGE;
  prv_challenge_new();
  prv_challenge_touch_timer();
  layer_mark_dirty(s_layer);
}

static void prv_challenge_idle(void *context) {
  s_challenge_timer = NULL;   // the challenge stays until it is solved
}

// Follow: end of the pause, show the next button
static void prv_pause_over(void *context) {
  s_pause_timer = NULL;
  s_challenge_waiting = false;
  if (s_layer) layer_mark_dirty(s_layer);
}

// A button press while the challenge is shown: right = next step.
// Wrong: Follow gets a new sequence; Remember starts over at the first box (same sequence).
static void prv_challenge_input(int button) {
  if (s_challenge_waiting) return;   // Follow pause: presses do not count
  if (button == s_challenge[s_challenge_pos]) {
    s_challenge_pos++;
    if (s_challenge_pos >= s_challenge_len) {
      prv_dismiss();
      return;
    }
    if (s_challenge_mode == CHALLENGE_FOLLOW && s_challenge_pause_ms > 0) {
      s_challenge_waiting = true;
      if (s_pause_timer) app_timer_cancel(s_pause_timer);
      s_pause_timer = app_timer_register(s_challenge_pause_ms, prv_pause_over, NULL);
    }
  } else {
    vibes_short_pulse();
    if (s_challenge_mode == CHALLENGE_REMEMBER) {
      s_challenge_pos = 0;
    } else {
      if (s_pause_timer) {
        app_timer_cancel(s_pause_timer);
        s_pause_timer = NULL;
      }
      prv_challenge_new();
    }
  }
  prv_challenge_touch_timer();
  layer_mark_dirty(s_layer);
}

static void prv_up_click(ClickRecognizerRef r, void *c) {
  if (s_state == RING_CHALLENGE) prv_challenge_input(CH_UP);
  else prv_snooze();
}

static void prv_select_click(ClickRecognizerRef r, void *c) {
  if (s_state == RING_CHALLENGE) prv_challenge_input(CH_SELECT);
  else prv_snooze();
}

static void prv_back_click(ClickRecognizerRef r, void *c) {
  if (s_state == RING_CHALLENGE) return;   // only solving the challenge ends it
  prv_snooze();
}

static uint32_t prv_now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (uint32_t)s * 1000 + ms;
}

// One animation step: forward while holding, quickly back after release
static void prv_hold_frame(void *context) {
  s_hold_timer = NULL;
  if (s_state != RING_ACTIVE) return;
  if (s_holding) {
    uint32_t elapsed = prv_now_ms() - s_hold_start_ms;
    s_hold_progress = elapsed >= HOLD_MS ? 1000 : (int)(elapsed * 1000 / HOLD_MS);
    if (s_hold_progress >= 1000) {
      s_holding = false;
      layer_mark_dirty(s_layer);
      RingChallenge challenge = prv_needs_challenge();
      if (challenge.mode != CHALLENGE_OFF) {
        prv_challenge_start(&challenge);
      } else {
        prv_dismiss();
      }
      return;
    }
  } else {
    s_hold_progress -= HOLD_REWIND_SPEED * 1000 * HOLD_FRAME_MS / HOLD_MS;
    if (s_hold_progress <= 0) {
      s_hold_progress = 0;
      layer_mark_dirty(s_layer);
      return;
    }
  }
  layer_mark_dirty(s_layer);
  s_hold_timer = app_timer_register(HOLD_FRAME_MS, prv_hold_frame, NULL);
}

static void prv_down_pressed(ClickRecognizerRef r, void *c) {
  if (s_state == RING_CHALLENGE) {
    prv_challenge_input(CH_DOWN);
    return;
  }
  if (s_state != RING_ACTIVE) return;
  s_holding = true;
  s_hold_start_ms = prv_now_ms() - (uint32_t)s_hold_progress * HOLD_MS / 1000;
  if (!s_hold_timer) s_hold_timer = app_timer_register(HOLD_FRAME_MS, prv_hold_frame, NULL);
}

static void prv_down_released(ClickRecognizerRef r, void *c) {
  if (!s_holding) return;
  s_holding = false;
  // very short press = snooze; released early = keep ringing
  if (prv_now_ms() - s_hold_start_ms < TAP_MS) {
    s_hold_progress = 0;
    prv_snooze();
    return;
  }
  if (!s_hold_timer) s_hold_timer = app_timer_register(HOLD_FRAME_MS, prv_hold_frame, NULL);
}

static void prv_clicks(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click);
  // bottom button: short = snooze, hold 1.5 s = stop (bar fills up)
  window_raw_click_subscribe(BUTTON_ID_DOWN, prv_down_pressed, prv_down_released, NULL);
}

// tapping the screen = snooze
static void prv_tap(const Recognizer *recognizer, RecognizerEvent event) {
  if (event == RecognizerEvent_Completed) prv_snooze();
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, prv_draw);
  layer_add_child(root, s_layer);
  window_set_touch_bridge_disabled(window, true);
  window_attach_recognizer(window, tap_recognizer_create(prv_tap, NULL));
}

static void prv_unload(Window *window) {
  alert_stop();
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (s_hold_timer) {
    app_timer_cancel(s_hold_timer);
    s_hold_timer = NULL;
  }
  if (s_challenge_timer) {
    app_timer_cancel(s_challenge_timer);
    s_challenge_timer = NULL;
  }
  if (s_pause_timer) {
    app_timer_cancel(s_pause_timer);
    s_pause_timer = NULL;
  }
  s_holding = false;
  s_hold_progress = 0;
  light_enable(false);
  layer_destroy(s_layer);
  window_destroy(window);
  s_window = NULL;
  s_mask = 0;
  s_test = false;
}

static void prv_open_window(void) {
  s_state = RING_ACTIVE;
  s_hold_progress = 0;
  s_holding = false;
  prv_build_names();
  s_window = window_create();
  window_set_background_color(s_window, theme()->bg);
  window_set_click_config_provider(s_window, prv_clicks);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
  light_enable(true);
  prv_start_alert();
}

void ring_show_test(const AlertParams *params, bool can_snooze, const RingChallenge *challenge) {
  if (s_window && !s_test && s_state != RING_DONE) return;  // a real alarm has priority
  if (s_window) window_stack_remove(s_window, false);
  s_test_params = *params;
  s_test_can_snooze = can_snooze;
  s_test_challenge = *challenge;
  s_mask = 0;
  s_test = true;
  prv_open_window();
  {
    RingChallenge ch = prv_needs_challenge();   // with a challenge: show it right away
    if (ch.mode != CHALLENGE_OFF) prv_challenge_start(&ch);
  }
  prv_start_timer();
  APP_LOG(APP_LOG_LEVEL_INFO, "Test alarm");
}

void ring_show(uint8_t slot_mask) {
  slot_mask &= SCHED_SLOT_MASK;
  if (!slot_mask) return;
  if (s_window && s_state != RING_DONE && !s_test) {
    s_mask |= slot_mask;  // another alarm while ringing
    prv_build_names();
    layer_mark_dirty(s_layer);
    return;
  }
  if (s_window) window_stack_remove(s_window, false);  // also end a running test

  s_mask = slot_mask;
  s_test = false;
  prv_open_window();
  {
    RingChallenge ch = prv_needs_challenge();   // with a challenge: show it right away
    if (ch.mode != CHALLENGE_OFF) prv_challenge_start(&ch);
  }
  comms_set_status(ALARM_STATUS_RINGING, prv_first_slot());
  prv_start_timer();
  APP_LOG(APP_LOG_LEVEL_INFO, "Alarm ringing: slots 0x%02x", s_mask);
}
