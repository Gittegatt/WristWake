#pragma once
// Time picker: UP/DOWN change the value, SELECT moves on or confirms,
// BACK goes back or cancels.
#include <pebble.h>

typedef void (*TimePickerCallback)(uint8_t hour, uint8_t minute, void *context);

// Options of the time input (set on the phone), as bit flags
#define TIME_INPUT_REVERSE 1   // swipe up = more (normal: swipe down = more, like a wheel)
#define TIME_INPUT_NO_ANIM 2   // numbers change without the rolling animation
uint8_t time_picker_flags(void);
void time_picker_set_flags(uint8_t flags);

// done is only called on confirmation, after the window has closed.
void time_picker_show(const char *title, uint8_t hour, uint8_t minute,
                      TimePickerCallback done, void *context);
