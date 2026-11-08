#pragma once
// Shared UI drawing and helper functions.
#include <pebble.h>
#include "alarm_store.h"
#include "theme.h"

#ifndef APP_VERSION
#define APP_VERSION "?"
#endif

// Time as text. 24h: "7:00" or "07:00" (leading_zero). 12h: "7:00", *ampm = "AM"/"PM".
// ampm may be NULL. In 24h mode *ampm is set to NULL.
void ui_format_time(char *buf, size_t size, uint8_t hour, uint8_t minute,
                    bool leading_zero, const char **ampm);

// Short description of the days: "Once", "Every day", "Mon-Fri", "Weekends" or "Mon, Wed, Fri".
void ui_format_days(char *buf, size_t size, uint8_t days);

// Full day name, index 0 = Monday ... 6 = Sunday
const char *ui_day_name(int index);

// Draws the weekdays as a row of letters (active days strong, inactive dimmed).
void ui_draw_day_letters(GContext *ctx, GRect rect, uint8_t days, bool highlighted, bool dimmed);

// Draws a toggle switch (30 x 16 pixels) for ON/OFF.
#define UI_TOGGLE_W 30
#define UI_TOGGLE_H 16
void ui_draw_toggle(GContext *ctx, GPoint origin, bool on, bool highlighted);

// Draws a checkbox (14 x 14) or a radio button.
void ui_draw_checkbox(GContext *ctx, GPoint origin, bool checked, bool highlighted);
void ui_draw_radio(GContext *ctx, GPoint center, bool selected, bool highlighted);

const char *ui_sound_name(AlarmSound sound);
const char *ui_vibe_name(int pattern);
const char *ui_snooze_name(uint8_t minutes);
const char *ui_volume_name(AlarmVolume volume);

// Short message that closes itself after about 2 seconds.
void ui_show_message(const char *text);

// Checks whether the alarm in slot may be active at hour:minute (not exactly 1 minute
// next to another one). Otherwise shows a message and returns false.
bool ui_check_time_allowed(int slot, uint8_t hour, uint8_t minute);

// Confirmation: SELECT = yes (on_yes is called), BACK = no.
typedef void (*UiConfirmCallback)(void *context);
void ui_confirm(const char *question, UiConfirmCallback on_yes, void *context);

// Shared menu colors (from the current color scheme)
void ui_style_menu(MenuLayer *menu);

// Header: area in header color with the title on the left
void ui_draw_header_bar(GContext *ctx, GRect bounds, const char *title, GFont font, int text_y);
// Header text color (matching ui_draw_header_bar)
GColor ui_header_fg(void);
