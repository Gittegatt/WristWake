#include "time_picker.h"
#include "ui_common.h"
#include "i18n.h"

static Window *s_window;
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

static void prv_box_rects(GRect b, GRect *boxes) {
  int y = 70;
  int x0 = (b.size.w - (2 * BOX_W + BOX_GAP)) / 2;
  boxes[0] = GRect(x0, y, BOX_W, BOX_H);
  boxes[1] = GRect(x0 + BOX_W + BOX_GAP, y, BOX_W, BOX_H);
}

static GRect prv_ok_rect(GRect b) {
  return GRect(b.size.w / 2 - 50, b.size.h - 42, 100, 34);
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
  graphics_draw_text(ctx, "OK", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(ok.origin.x, ok.origin.y + 1, ok.size.w, ok.size.h),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // hour in the selected clock format
  char hour_buf[4], min_buf[4];
  const char *ampm = NULL;
  if (clock_is_24h_style()) {
    snprintf(hour_buf, sizeof(hour_buf), "%02d", s_hour);
  } else {
    int h12 = s_hour % 12;
    snprintf(hour_buf, sizeof(hour_buf), "%d", h12 == 0 ? 12 : h12);
    ampm = s_hour < 12 ? "AM" : "PM";
  }
  snprintf(min_buf, sizeof(min_buf), "%02d", s_minute);

  GRect boxes[2];
  prv_box_rects(b, boxes);
  const int box_w = BOX_W, box_h = BOX_H, gap = BOX_GAP;
  int y = boxes[0].origin.y;
  int x0 = boxes[0].origin.x;
  const char *texts[2] = {hour_buf, min_buf};
  GFont big = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);

  for (int i = 0; i < 2; i++) {
    bool sel = (i == s_field);
    if (sel) {
      graphics_context_set_fill_color(ctx, t->hl_bg);
      graphics_fill_rect(ctx, boxes[i], 6, GCornersAll);
      prv_draw_arrow(ctx, GPoint(boxes[i].origin.x + box_w / 2, y - 16), true, t->fg);
      prv_draw_arrow(ctx, GPoint(boxes[i].origin.x + box_w / 2, y + box_h + 16), false, t->fg);
    } else {
      graphics_context_set_stroke_color(ctx, t->faint);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_round_rect(ctx, boxes[i], 6);
      graphics_context_set_stroke_width(ctx, 1);
    }
    graphics_context_set_text_color(ctx, sel ? t->hl_fg : t->fg);
    graphics_draw_text(ctx, texts[i], big, GRect(boxes[i].origin.x, y + 2, box_w, box_h),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
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

static void prv_change(int delta) {
  if (s_field == 0) {
    s_hour = (s_hour + 24 + delta) % 24;
  } else {
    s_minute = (s_minute + 60 + delta) % 60;
  }
  layer_mark_dirty(s_layer);
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
  layer_mark_dirty(s_layer);
}

// Touch: swipe down = more, swipe up = less (like a scroll wheel)
static void prv_pan(const Recognizer *recognizer, RecognizerEvent event) {
  if (event == RecognizerEvent_Started) s_pan_accum = 0;
  if (event != RecognizerEvent_Updated && event != RecognizerEvent_Completed) return;
  s_pan_accum += pan_recognizer_get_delta_since_prev(recognizer).y;
  while (s_pan_accum >= PAN_STEP_PX) {
    prv_change(1);
    s_pan_accum -= PAN_STEP_PX;
  }
  while (s_pan_accum <= -PAN_STEP_PX) {
    prv_change(-1);
    s_pan_accum += PAN_STEP_PX;
  }
}

static void prv_select(ClickRecognizerRef r, void *c) {
  if (s_field == 0) {
    s_field = 1;
    layer_mark_dirty(s_layer);
    return;
  }
  prv_confirm();
}

static void prv_back(ClickRecognizerRef r, void *c) {
  if (s_field == 1) {
    s_field = 0;
    layer_mark_dirty(s_layer);
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
  // Own touch handling instead of the automatic button emulation
  window_set_touch_bridge_disabled(window, true);
  window_attach_recognizer(window, tap_recognizer_create(prv_tap, NULL));
  window_attach_recognizer(window, pan_recognizer_create(prv_pan, NULL, PanAxis_Vertical));
}

static void prv_unload(Window *window) {
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
