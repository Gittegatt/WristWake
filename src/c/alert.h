#pragma once
// Sound and vibration while ringing, both looping until alert_stop().
#include <pebble.h>
#include "alarm_store.h"

typedef struct {
  VibePattern_t vibe_pattern;  // one of the built-in patterns
  bool vibrate;
  AlarmSound sound;
} AlertParams;

void alert_start(const AlertParams *params);
void alert_stop(void);

// Preview: plays a vibration pattern in a loop until alert_stop() is called.
void alert_preview_vibe(VibePattern_t pattern);
