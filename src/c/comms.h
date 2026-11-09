#pragma once
// Data exchange with the phone (JavaScript part of the app):
// - watch -> phone: complete state as JSON (alarms, next times, status, settings)
// - phone -> watch: settings from the settings page (theme, names, watch name, left-handed mode)
// The watch is the source of truth; the phone only mirrors it.
#include <pebble.h>

typedef enum {
  ALARM_STATUS_IDLE = 0,
  ALARM_STATUS_RINGING,
  ALARM_STATUS_SNOOZED,
  ALARM_STATUS_DISMISSED,
} AlarmStatus;

// on_theme_changed: called when the phone has sent a new theme.
void comms_init(void (*on_theme_changed)(void));

// Send the current state to the phone (if connected; otherwise next time).
void comms_send_state(void);

// Set the Home Assistant status (idle/ringing/snoozed/dismissed) and send it.
void comms_set_status(AlarmStatus status, int slot);
AlarmStatus comms_status(void);
int comms_status_slot(void);
