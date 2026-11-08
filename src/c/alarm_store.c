#include "alarm_store.h"

// Persistent storage keys (theme: 10-12, see theme.c; status: 5, see comms.c)
#define STORE_SCHEMA_VERSION 5
#define KEY_SCHEMA      1
#define KEY_WATCH_ID    2
#define KEY_WATCH_NAME  3
#define KEY_SNOOZE      4   // int32_t[8]: snooze end per slot
#define KEY_OLD_VIBE_MS 20  // 20-22: S/L/P lengths from schema 2-3, no longer used
#define KEY_SLOT_BASE 100   // 101-108 = slots 1-8

#define DEFAULT_WATCH_NAME "Pebble"

const uint8_t SNOOZE_OPTIONS[SNOOZE_OPTION_COUNT] = {5, 10, 15, 30, 60};

static const char *const SOUND_KEYS[ALARM_SOUND_COUNT] = {
  "off", "reveille", "beep", "chime", "rising", "korobeiniki", "westminster", "field_agent",
  "maple_leaf", "rondo", "classic_ring", "digital", "sunrise", "siren", "chirp", "pulse"
};

static const char *const VIBE_KEYS[VIBE_PATTERN_COUNT] = {
  "standard", "pulse", "double", "heartbeat", "rising", "sos", "long", "gentle", "aurora"
};

static Alarm s_alarms[ALARM_SLOTS];
static int32_t s_snooze[ALARM_SLOTS];
static char s_watch_id[WATCH_ID_LEN + 1];
static char s_watch_name[WATCH_NAME_BUF];
static StoreChangeHandler s_change_handler;

// Common start of all previous formats (schema 1-4): rev to name
typedef struct {
  uint32_t rev;
  uint8_t used, enabled, hour, minute, days, sound, vibrate, snooze;
  char name[ALARM_NAME_BUF];
} AlarmPrefix;

_Static_assert(sizeof(Alarm) <= PERSIST_DATA_MAX_LENGTH, "Alarm does not fit into one storage slot");
_Static_assert(offsetof(Alarm, vibe_pattern) >= sizeof(AlarmPrefix), "start of the format changed");

static int s_batch_depth;
static bool s_batch_dirty;

static void prv_notify_change(void) {
  if (s_batch_depth > 0) {
    s_batch_dirty = true;
    return;
  }
  if (s_change_handler) s_change_handler();
}

void store_set_change_handler(StoreChangeHandler handler) {
  s_change_handler = handler;
}

void store_batch_begin(void) {
  s_batch_depth++;
}

void store_batch_end(void) {
  if (s_batch_depth > 0) s_batch_depth--;
  if (s_batch_depth == 0 && s_batch_dirty) {
    s_batch_dirty = false;
    prv_notify_change();
  }
}

void store_copy_name(char *dst, const char *src) {
  size_t out = 0;
  int chars = 0;
  for (size_t i = 0; src && src[i] && out < ALARM_NAME_BUF - 1; i++) {
    unsigned char c = (unsigned char)src[i];
    if ((c & 0xC0) != 0x80) {  // start of a character
      if (chars == ALARM_NAME_MAX_CHARS) break;
      int len = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2));
      if (out + len > ALARM_NAME_BUF - 1) break;
      chars++;
    }
    dst[out++] = src[i];
  }
  dst[out] = '\0';
  while (out > 0 && (dst[out - 1] == '.' || dst[out - 1] == ' ' || dst[out - 1] == '!' ||
                     dst[out - 1] == '?')) {
    dst[--out] = '\0';
  }
}

static bool prv_slot_valid(int slot) {
  return slot >= 1 && slot <= ALARM_SLOTS;
}

static void prv_empty_slot(Alarm *alarm, uint32_t rev) {
  store_alarm_defaults(alarm);
  alarm->used = 0;
  alarm->enabled = 0;
  alarm->rev = rev;
}

// Brings invalid values (e.g. corrupted data) into a valid range.
static void prv_sanitize(Alarm *alarm) {
  alarm->used = alarm->used ? 1 : 0;
  alarm->enabled = alarm->enabled ? 1 : 0;
  alarm->vibrate = alarm->vibrate ? 1 : 0;
  if (alarm->hour > 23) alarm->hour = 0;
  if (alarm->minute > 59) alarm->minute = 0;
  alarm->days &= 0x7F;
  if (alarm->sound >= ALARM_SOUND_COUNT) alarm->sound = DEFAULT_SOUND;
  if (!alarm_snooze_valid(alarm->snooze)) alarm->snooze = DEFAULT_SNOOZE;
  // A silent alarm is not allowed: turn vibration on
  if (alarm->sound == ALARM_SOUND_OFF && !alarm->vibrate) alarm->vibrate = 1;
  alarm->name[ALARM_NAME_BUF - 1] = '\0';
  if (alarm->vibe_pattern >= VIBE_PATTERN_COUNT) alarm->vibe_pattern = DEFAULT_VIBE_PATTERN;
  alarm->protect = alarm->protect ? 1 : 0;
}

static void prv_generate_watch_id(void) {
  // Randomness from time and milliseconds; enough to tell several watches apart
  // in one Home Assistant instance.
  time_t seconds;
  uint16_t millis;
  time_ms(&seconds, &millis);
  srand((unsigned int)(seconds * 1000 + millis));
  static const char HEX[] = "0123456789abcdef";
  for (int i = 0; i < WATCH_ID_LEN; i++) {
    s_watch_id[i] = HEX[rand() % 16];
  }
  s_watch_id[WATCH_ID_LEN] = '\0';
  persist_write_string(KEY_WATCH_ID, s_watch_id);
  APP_LOG(APP_LOG_LEVEL_INFO, "New watch_id: %s", s_watch_id);
}

static void prv_wipe_slots(void) {
  for (int i = 0; i < ALARM_SLOTS; i++) {
    persist_delete(KEY_SLOT_BASE + i + 1);
  }
}

// Older formats -> current format: keep the basic data (time, days, name, sound, snooze ...).
// Schema 4 also has the vibration pattern at the same position as today; for schema 1-3
// (free-form patterns/strengths) it becomes "Standard". Protection is off.
static void prv_migrate_old(int32_t schema) {
  for (int i = 0; i < ALARM_SLOTS; i++) {
    uint32_t key = KEY_SLOT_BASE + i + 1;
    uint8_t raw[PERSIST_DATA_MAX_LENGTH];
    int read = persist_read_data(key, raw, sizeof(raw));
    if (read < (int)sizeof(AlarmPrefix)) continue;
    Alarm alarm;
    memset(&alarm, 0, sizeof(alarm));
    memcpy(&alarm, raw, sizeof(AlarmPrefix));
    alarm.vibe_pattern = DEFAULT_VIBE_PATTERN;
    if (schema == 4 && read > (int)offsetof(Alarm, vibe_pattern)) {
      alarm.vibe_pattern = raw[offsetof(Alarm, vibe_pattern)];
    }
    alarm.protect = 0;
    persist_write_data(key, &alarm, sizeof(alarm));
  }
  for (int k = 0; k < 3; k++) persist_delete(KEY_OLD_VIBE_MS + k);
  APP_LOG(APP_LOG_LEVEL_INFO, "Storage migrated from version %d to %d", (int)schema,
          STORE_SCHEMA_VERSION);
}

void store_init(void) {
  int32_t schema = persist_exists(KEY_SCHEMA) ? persist_read_int(KEY_SCHEMA) : 0;
  if (schema != STORE_SCHEMA_VERSION) {
    if (schema >= 1 && schema < STORE_SCHEMA_VERSION) {
      prv_migrate_old(schema);
    } else {
      if (schema != 0) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Unknown storage version %d, resetting alarms", (int)schema);
      }
      prv_wipe_slots();
    }
    persist_write_int(KEY_SCHEMA, STORE_SCHEMA_VERSION);
  }

  // Snooze state
  memset(s_snooze, 0, sizeof(s_snooze));
  persist_read_data(KEY_SNOOZE, s_snooze, sizeof(s_snooze));

  // Watch identity
  if (persist_read_string(KEY_WATCH_ID, s_watch_id, sizeof(s_watch_id)) != WATCH_ID_LEN + 1) {
    prv_generate_watch_id();
  }
  if (persist_read_string(KEY_WATCH_NAME, s_watch_name, sizeof(s_watch_name)) <= 1) {
    strncpy(s_watch_name, DEFAULT_WATCH_NAME, sizeof(s_watch_name) - 1);
    s_watch_name[sizeof(s_watch_name) - 1] = '\0';
  }

  // Alarm slots
  for (int i = 0; i < ALARM_SLOTS; i++) {
    Alarm *alarm = &s_alarms[i];
    int read = persist_read_data(KEY_SLOT_BASE + i + 1, alarm, sizeof(Alarm));
    if (read != (int)sizeof(Alarm)) {
      prv_empty_slot(alarm, 0);
    } else {
      prv_sanitize(alarm);
    }
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "Storage loaded: %d of %d slots in use", store_count_used(), ALARM_SLOTS);
}

const Alarm *store_get(int slot) {
  return prv_slot_valid(slot) ? &s_alarms[slot - 1] : NULL;
}

int store_count_used(void) {
  int count = 0;
  for (int i = 0; i < ALARM_SLOTS; i++) {
    if (s_alarms[i].used) count++;
  }
  return count;
}

int store_find_free_slot(void) {
  for (int i = 0; i < ALARM_SLOTS; i++) {
    if (!s_alarms[i].used) return i + 1;
  }
  return 0;
}

void store_alarm_defaults(Alarm *alarm) {
  memset(alarm, 0, sizeof(Alarm));
  alarm->used = 1;
  alarm->enabled = 1;
  alarm->hour = 7;
  alarm->minute = 0;
  alarm->days = 0;
  alarm->sound = DEFAULT_SOUND;
  alarm->vibrate = DEFAULT_VIBRATE ? 1 : 0;
  alarm->snooze = DEFAULT_SNOOZE;
  alarm->vibe_pattern = DEFAULT_VIBE_PATTERN;
}

bool store_save(int slot, const Alarm *alarm) {
  if (!prv_slot_valid(slot) || !alarm) return false;
  Alarm copy = *alarm;
  prv_sanitize(&copy);
  copy.rev = s_alarms[slot - 1].rev + 1;
  int written = persist_write_data(KEY_SLOT_BASE + slot, &copy, sizeof(Alarm));
  if (written != (int)sizeof(Alarm)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Saving slot %d failed (%d)", slot, written);
    return false;
  }
  s_alarms[slot - 1] = copy;
  prv_notify_change();
  return true;
}

bool store_delete(int slot) {
  if (!prv_slot_valid(slot)) return false;
  Alarm empty;
  prv_empty_slot(&empty, 0);
  // The counter keeps running so that a future sync can detect the deletion.
  empty.rev = s_alarms[slot - 1].rev + 1;
  int written = persist_write_data(KEY_SLOT_BASE + slot, &empty, sizeof(Alarm));
  if (written != (int)sizeof(Alarm)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Deleting slot %d failed (%d)", slot, written);
    return false;
  }
  s_alarms[slot - 1] = empty;
  store_set_snooze_until(slot, 0);
  prv_notify_change();
  return true;
}

time_t store_snooze_until(int slot) {
  return prv_slot_valid(slot) ? (time_t)s_snooze[slot - 1] : 0;
}

void store_set_snooze_until(int slot, time_t when) {
  if (!prv_slot_valid(slot) || s_snooze[slot - 1] == (int32_t)when) return;
  s_snooze[slot - 1] = (int32_t)when;
  persist_write_data(KEY_SNOOZE, s_snooze, sizeof(s_snooze));
}

const char *store_watch_id(void) {
  return s_watch_id;
}

const char *store_watch_name(void) {
  return s_watch_name;
}

bool store_set_watch_name(const char *name) {
  if (!name || !name[0]) return false;
  strncpy(s_watch_name, name, sizeof(s_watch_name) - 1);
  s_watch_name[sizeof(s_watch_name) - 1] = '\0';
  return persist_write_string(KEY_WATCH_NAME, s_watch_name) > 0;
}

const char *alarm_sound_key(AlarmSound sound) {
  return (sound < ALARM_SOUND_COUNT) ? SOUND_KEYS[sound] : SOUND_KEYS[ALARM_SOUND_OFF];
}

const char *alarm_vibe_key(VibePattern_t pattern) {
  return pattern < VIBE_PATTERN_COUNT ? VIBE_KEYS[pattern] : VIBE_KEYS[DEFAULT_VIBE_PATTERN];
}

bool alarm_snooze_valid(uint8_t minutes) {
  for (int i = 0; i < SNOOZE_OPTION_COUNT; i++) {
    if (SNOOZE_OPTIONS[i] == minutes) return true;
  }
  return false;
}
