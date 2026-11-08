#pragma once
// Ringing screen with a narrow bar at the button edge ("zZ" at the top, check mark at the bottom):
// short press on any button or tap = snooze, long press on the bottom button = stop,
// no reaction for 1 minute = automatic snooze.
#include <pebble.h>
#include "alert.h"

// slot_mask: bits 0-7 = slots 1-8 that are ringing
void ring_show(uint8_t slot_mask);

// Test alarm from the settings page: shows "Test", rings with the sound and vibration pattern
// of the selected alarm. Snooze/stop change nothing and are not reported to Home Assistant.
void ring_show_test(const AlertParams *params);

// true while the ringing screen is open
bool ring_is_active(void);
