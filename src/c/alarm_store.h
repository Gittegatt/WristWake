#pragma once
// Data model and persistent storage of the 8 alarm slots plus the watch identity.
#include <pebble.h>

#define ALARM_SLOTS 8          // fixed slots 1-8 (limit: 8 wakeups per app)
#define ALARM_NAME_MAX_CHARS 24
#define ALARM_NAME_BUF 72      // 24 UTF-8 characters (some need 2 bytes) + terminator
#define WATCH_ID_LEN 16        // random hex characters
#define WATCH_NAME_BUF 64

// Weekday bit mask: Mon=1, Tue=2 ... Sun=64; 0 = once
#define DAY_MON 1
#define DAY_TUE 2
#define DAY_WED 4
#define DAY_THU 8
#define DAY_FRI 16
#define DAY_SAT 32
#define DAY_SUN 64

typedef enum {
  ALARM_SOUND_OFF = 0,
  ALARM_SOUND_REVEILLE,
  ALARM_SOUND_BEEP,
  ALARM_SOUND_CHIME,
  ALARM_SOUND_RISING,
  ALARM_SOUND_KOROBEINIKI,   // always append new sounds (numbers are persisted)
  ALARM_SOUND_WESTMINSTER,
  ALARM_SOUND_FIELD_AGENT,
  ALARM_SOUND_MAPLE_LEAF,
  ALARM_SOUND_RONDO,
  ALARM_SOUND_CLASSIC_RING,
  ALARM_SOUND_DIGITAL,
  ALARM_SOUND_SUNRISE,
  ALARM_SOUND_SIREN,
  ALARM_SOUND_CHIRP,
  ALARM_SOUND_PULSE,
  ALARM_SOUND_DAYBREAK,
  ALARM_SOUND_BREEZE,
  ALARM_SOUND_BLOOM,
  ALARM_SOUND_COUNT
} AlarmSound;

// Built-in vibration patterns (sequences in alert.c)
typedef enum {
  VIBE_STANDARD = 0,
  VIBE_PULSE,
  VIBE_DOUBLE,
  VIBE_HEARTBEAT,
  VIBE_RISING,
  VIBE_SOS,
  VIBE_LONG,
  VIBE_GENTLE,
  VIBE_AURORA,          // always append new patterns (numbers are persisted)
  VIBE_PATTERN_COUNT
} VibePattern_t;

// Allowed snooze durations in minutes
#define SNOOZE_OPTION_COUNT 5
extern const uint8_t SNOOZE_OPTIONS[SNOOZE_OPTION_COUNT];

#define DEFAULT_SOUND ALARM_SOUND_OFF
#define DEFAULT_VIBRATE true
#define DEFAULT_VIBE_PATTERN VIBE_STANDARD
#define DEFAULT_SNOOZE 5

// One alarm slot. Changing this layout requires a new STORE_SCHEMA_VERSION in
// alarm_store.c and a migration of the old data.
// The first fields (rev to name) are unchanged since schema 1.
typedef struct {
  uint32_t rev;                // change counter (for future sync)
  uint8_t used;                // 1 = in use
  uint8_t enabled;             // 1 = on
  uint8_t hour;                // 0-23, local time
  uint8_t minute;              // 0-59
  uint8_t days;                // bit mask 0-127
  uint8_t sound;               // AlarmSound
  uint8_t vibrate;             // 1 = vibration on
  uint8_t snooze;              // minutes, one of SNOOZE_OPTIONS
  char name[ALARM_NAME_BUF];
  uint8_t vibe_pattern;        // VibePattern_t, since schema 4
  uint8_t protect;             // 1 = protected against deletion, since schema 5
} Alarm;

// Called after every saved alarm change (e.g. to reschedule).
typedef void (*StoreChangeHandler)(void);
void store_set_change_handler(StoreChangeHandler handler);

// Batch several changes: the change handler runs once, at store_batch_end().
void store_batch_begin(void);
void store_batch_end(void);

// Copies a name, truncates it to 24 characters (UTF-8 safe) and strips trailing punctuation.
void store_copy_name(char *dst, const char *src);

// Loads all data; creates the watch_id on first start.
void store_init(void);

// slot: 1-8. Returns NULL for an invalid slot.
const Alarm *store_get(int slot);

int store_count_used(void);

// First free slot (1-8), or 0 if all are in use.
int store_find_free_slot(void);

// Fills an alarm with defaults (in use, on, once, 07:00).
void store_alarm_defaults(Alarm *alarm);

// Persists an alarm. Increments rev automatically. Returns false on error.
bool store_save(int slot, const Alarm *alarm);

// Frees the slot (used = false). Increments rev.
bool store_delete(int slot);

// Snooze: time (UTC) when the alarm rings again; 0 = not snoozed.
time_t store_snooze_until(int slot);
void store_set_snooze_until(int slot, time_t when);

const char *store_watch_id(void);
const char *store_watch_name(void);
bool store_set_watch_name(const char *name);

// Language-neutral keys for Home Assistant: "off", "reveille", ... / "standard", "pulse", ...
const char *alarm_sound_key(AlarmSound sound);
const char *alarm_vibe_key(VibePattern_t pattern);
bool alarm_snooze_valid(uint8_t minutes);
