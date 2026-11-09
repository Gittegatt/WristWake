#include "time_picker.h"
#include "ui_common.h"
#include "i18n.h"

#define KEY_TIME_INPUT 17   // storage key: TIME_INPUT_* flags (other keys see theme.c)

static Window *s_window;
static int s_flags = -1;   // TIME_INPUT_* flags, -1 = not loaded yet
static Layer *s_layer;
static char s_title[32];
static uint8_t s_hour, s_minute;
static int s_field;  // 0 = hour, 1 = minute
static TimePickerCallback s_done;
static void *s_done_ctx;

static void prv_draw_arrow(GContext *ctx, GPoint tip, bool up, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int d = up ? 1 : -1;
  GPathInfo info = {
    .num_points = 3,
    .points = (GPoint[]) {{tip.x, tip.y}, {tip.x - 8, tip.y + 8 * d}, {tip.x + 8, tip.y + 8 * d}},
  };
  GPath *path = gpath_create(&info);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

#define BOX_W 72
#define BOX_H 58
#define BOX_GAP 16
#define PAN_STEP_PX 14   // this many pixels of swiping = 1 step

static int s_pan_accum;

// Scroll animation of a number box: the new value rolls in, the old one rolls out
#define ANIM_FRAMES 6
#define ANIM_FRAME_MS 20
static Layer *s_box_layers[2];
static int s_anim_field = -1;   // box being animated, -1 = none
static int s_anim_dir;          // +1 = numbers move down (new value comes from the top)
static int s_anim_step;
static uint8_t s_prev_value;    // value shown before the change
static AppTimer *s_anim_timer;

static void prv_box_rects(GRect b, GRect *boxes) {
  int y = 70;
  int x0 = (b.size.w - (2 * BOX_W + BOX_GAP)) / 2;
  boxes[0] = GRect(x0, y, BOX_W, BOX_H);
  boxes[1] = GRect(x0 + BOX_W + BOX_GAP, y, BOX_W, BOX_H);
}

// Large "OK" button across almost the full width, easy to tap
static GRect prv_ok_rect(GRect b) {
  return GRect(10, b.size.h - 50, b.size.w - 20, 44);
}

// Text of a box: hour in the selected clock format, or minute
static void prv_value_text(int field, uint8_t value, char *buf, size_t size) {
  if (field == 1) {
    snprintf(buf, size, "%02d", value);
  } else if (clock_is_24h_style()) {
    snprintf(buf, size, "%02d", value);
  } else {
    int h12 = value % 12;
    snprintf(buf, size, "%d", h12 == 0 ? 12 : h12);
  }
}

// One number box (its own layer, so the rolling numbers are clipped to the box)
static void prv_box_draw(Layer *layer, GContext *ctx) {
  int i = *(int *)layer_get_data(layer);
  GRect r = layer_get_bounds(layer);
  const Theme *t = theme();
  bool sel = (i == s_field);
  if (sel) {
    graphics_context_set_fill_color(ctx, t->hl_bg);
    graphics_fill_rect(ctx, r, 6, GCornersAll);
  } else {
    graphics_context_set_stroke_color(ctx, t->faint);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, grect_inset(r, GEdgeInsets(1)), 6);
    graphics_context_set_stroke_width(ctx, 1);
  }
  graphics_context_set_text_color(ctx, sel ? t->hl_fg : t->fg);
  GFont big = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  char buf[4];
  prv_value_text(i, i == 0 ? s_hour : s_minute, buf, sizeof(buf));
  if (s_anim_field == i && s_anim_step < ANIM_FRAMES) {
    int moved = BOX_H * s_anim_step / ANIM_FRAMES;   // distance travelled so far
    char old[4];
    prv_value_text(i, s_prev_value, old, sizeof(old));
    graphics_draw_text(ctx, old, big, GRect(0, 2 + s_anim_dir * moved, BOX_W, BOX_H),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, buf, big, GRect(0, 2 - s_anim_dir * (BOX_H - moved), BOX_W, BOX_H),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  } else {
    graphics_draw_text(ctx, buf, big, GRect(0, 2, BOX_W, BOX_H),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void prv_redraw(void) {
  layer_mark_dirty(s_layer);
  for (int i = 0; i < 2; i++) {
    if (s_box_layers[i]) layer_mark_dirty(s_box_layers[i]);
  }
}

static void prv_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  const Theme *t = theme();

  // title
  graphics_context_set_text_color(ctx, t->fg);
  graphics_draw_text(ctx, s_title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 4, b.size.w, 30), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  // "OK" button at the bottom (for tapping; with buttons, SELECT confirms)
  GRect ok = prv_ok_rect(b);
  graphics_context_set_fill_color(ctx, t->bar_bg);
  graphics_fill_rect(ctx, ok, 8, GCornersAll);
  graphics_context_set_text_color(ctx, t->bar_fg);
  graphics_draw_text(ctx, "OK", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(ok.origin.x, ok.origin.y + 3, ok.size.w, ok.size.h),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  const char *ampm = clock_is_24h_style() ? NULL : (s_hour < 12 ? "AM" : "PM");

  GRect boxes[2];
  prv_box_rects(b, boxes);
  const int box_w = BOX_W, box_h = BOX_H, gap = BOX_GAP;
  int y = boxes[0].origin.y;
  int x0 = boxes[0].origin.x;
  // arrows above and below the selected box (the boxes themselves are child layers)
  GRect sel = boxes[s_field];
  prv_draw_arrow(ctx, GPoint(sel.origin.x + box_w / 2, y - 16), true, t->fg);
  prv_draw_arrow(ctx, GPoint(sel.origin.x + box_w / 2, y + box_h + 16), false, t->fg);
  // colon
  graphics_context_set_fill_color(ctx, t->fg);
  int cx = x0 + box_w + gap / 2;
  graphics_fill_rect(ctx, GRect(cx - 3, y + 16, 6, 6), 1, GCornersAll);
  graphics_fill_rect(ctx, GRect(cx - 3, y + 36, 6, 6), 1, GCornersAll);

  if (ampm) {
    graphics_context_set_text_color(ctx, t->dim);
    graphics_draw_text(ctx, ampm, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(boxes[0].origin.x, y + box_h + 20, box_w, 22),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void prv_anim_frame(void *data) {
  s_anim_timer = NULL;
  s_anim_step++;
  if (s_anim_step < ANIM_FRAMES) {
    s_anim_timer = app_timer_register(ANIM_FRAME_MS, prv_anim_frame, NULL);
  } else {
    s_anim_field = -1;
  }
  prv_redraw();
}

uint8_t time_picker_flags(void) {
  if (s_flags < 0) {
    s_flags = persist_exists(KEY_TIME_INPUT) ? (persist_read_int(KEY_TIME_INPUT) & 0x03) : 0;
  }
  return (uint8_t)s_flags;
}

void time_picker_set_flags(uint8_t flags) {
  s_flags = flags & 0x03;
  persist_write_int(KEY_TIME_INPUT, s_flags);
}

// delta: change of the value; motion: direction the numbers roll (+1 down, -1 up)
static void prv_change_moving(int delta, int motion) {
  if (time_picker_flags() & TIME_INPUT_NO_ANIM) {
    s_anim_field = -1;
  } else {
    s_prev_value = (s_field == 0) ? s_hour : s_minute;
    s_anim_field = s_field;
    s_anim_dir = motion;
    s_anim_step = 0;
    if (!s_anim_timer) s_anim_timer = app_timer_register(ANIM_FRAME_MS, prv_anim_frame, NULL);
  }
  if (s_field == 0) {
    s_hour = (s_hour + 24 + delta) % 24;
  } else {
    s_minute = (s_minute + 60 + delta) % 60;
  }
  prv_redraw();
}

// Buttons: up = more, the new value comes from the top
static void prv_change(int delta) {
  prv_change_moving(delta, delta > 0 ? 1 : -1);
}

static void prv_up(ClickRecognizerRef r, void *c) { prv_change(1); }
static void prv_down(ClickRecognizerRef r, void *c) { prv_change(-1); }

static void prv_confirm(void) {
  // Confirm: keep values, close window, then call back
  TimePickerCallback done = s_done;
  void *ctx = s_done_ctx;
  uint8_t hour = s_hour, minute = s_minute;
  window_stack_remove(s_window, false);
  if (done) done(hour, minute, ctx);
}

// Touch: tapping a box selects hour/minute, "OK" confirms
static void prv_tap(const Recognizer *recognizer, RecognizerEvent event) {
  if (event != RecognizerEvent_Completed) return;
  GPoint p = tap_recognizer_get_tap_point(recognizer);
  GRect b = layer_get_bounds(s_layer);
  GRect boxes[2];
  prv_box_rects(b, boxes);
  GRect ok = prv_ok_rect(b);
  if (grect_contains_point(&boxes[0], &p)) {
    s_field = 0;
  } else if (grect_contains_point(&boxes[1], &p)) {
    s_field = 1;
  } else if (grect_contains_point(&ok, &p)) {
    prv_confirm();
    return;
  }
  prv_redraw();
}

// Touch: swipe down = more, swipe up = less (like a scroll wheel); reversed on request.
// The numbers always roll in the direction of the finger.
static void prv_pan(const Recognizer *recognizer, RecognizerEvent event) {
  if (event == RecognizerEvent_Started) s_pan_accum = 0;
  if (event != RecognizerEvent_Updated && event != RecognizerEvent_Completed) return;
  int sign = (time_picker_flags() & TIME_INPUT_REVERSE) ? -1 : 1;
  s_pan_accum += pan_recognizer_get_delta_since_prev(recognizer).y;
  while (s_pan_accum >= PAN_STEP_PX) {
    prv_change_moving(sign, 1);
    s_pan_accum -= PAN_STEP_PX;
  }
  while (s_pan_accum <= -PAN_STEP_PX) {
    prv_change_moving(-sign, -1);
    s_pan_accum += PAN_STEP_PX;
  }
}

static void prv_select(ClickRecognizerRef r, void *c) {
  if (s_field == 0) {
    s_field = 1;
    prv_redraw();
    return;
  }
  prv_confirm();
}

static void prv_back(ClickRecognizerRef r, void *c) {
  if (s_field == 1) {
    s_field = 0;
    prv_redraw();
    return;
  }
  window_stack_remove(s_window, true);  // cancel
}

static void prv_clicks(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 120, prv_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 120, prv_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back);
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, prv_draw);
  layer_add_child(root, s_layer);
  GRect boxes[2];
  prv_box_rects(layer_get_bounds(root), boxes);
  for (int i = 0; i < 2; i++) {
    s_box_layers[i] = layer_create_with_data(boxes[i], sizeof(int));
    *(int *)layer_get_data(s_box_layers[i]) = i;
    layer_set_update_proc(s_box_layers[i], prv_box_draw);
    layer_add_child(s_layer, s_box_layers[i]);
  }
  // Own touch handling instead of the automatic button emulation
  window_set_touch_bridge_disabled(window, true);
  window_attach_recognizer(window, tap_recognizer_create(prv_tap, NULL));
  window_attach_recognizer(window, pan_recognizer_create(prv_pan, NULL, PanAxis_Vertical));
}

static void prv_unload(Window *window) {
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  s_anim_field = -1;
  for (int i = 0; i < 2; i++) {
    layer_destroy(s_box_layers[i]);
    s_box_layers[i] = NULL;
  }
  layer_destroy(s_layer);
  window_destroy(window);
  s_window = NULL;
}

void time_picker_show(const char *title, uint8_t hour, uint8_t minute,
                      TimePickerCallback done, void *context) {
  if (s_window) return;
  snprintf(s_title, sizeof(s_title), "%s", title);
  s_hour = hour % 24;
  s_minute = minute % 60;
  s_field = 0;
  s_done = done;
  s_done_ctx = context;
  s_window = window_create();
  window_set_background_color(s_window, theme()->bg);
  window_set_click_config_provider(s_window, prv_clicks);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
