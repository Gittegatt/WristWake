#pragma once
// Pickers for weekdays and for single-choice lists (sound, snooze duration).
#include <pebble.h>

// Weekdays: 7 checkbox rows and "Done". BACK cancels without changes.
typedef void (*DaysPickerCallback)(uint8_t days, void *context);
void days_picker_show(uint8_t days, DaysPickerCallback done, void *context);

// Choose one option from a list. labels must stay valid until the window closes.
typedef void (*OptionPickerCallback)(int index, void *context);
void option_picker_show(const char *title, const char *const *labels, int count, int selected,
                        OptionPickerCallback done, void *context);
