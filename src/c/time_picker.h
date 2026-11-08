#pragma once
// Time picker: UP/DOWN change the value, SELECT moves on or confirms,
// BACK goes back or cancels.
#include <pebble.h>

typedef void (*TimePickerCallback)(uint8_t hour, uint8_t minute, void *context);

// done is only called on confirmation, after the window has closed.
void time_picker_show(const char *title, uint8_t hour, uint8_t minute,
                      TimePickerCallback done, void *context);
