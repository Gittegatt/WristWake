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
// can_snooze = false: like an alarm with snooze off (only holding the bottom button stops it)
// Challenge that must be solved before the alarm stops
typedef struct {
  int mode;       // AlarmChallenge
  int length;     // Follow: number of buttons
  int pause_ms;   // Follow: pause before the next button appears
} RingChallenge;

// symbol: symbols.h number shown on the ringing screen (0 = none)
void ring_show_test(const AlertParams *params, bool can_snooze, const RingChallenge *challenge, int symbol);

// true while the ringing screen is open
bool ring_is_active(void);

// Stop from the phone (settings page): ends a ringing alarm, also during a challenge, or
// cancels a running snooze. Returns true if something was stopped.
bool ring_stop_remote(void);

// Turns a one-time alarm off after it has rung (no effect on repeating alarms)
void ring_end_one_time(int slot);
