#include "comms.h"
#include "alarm_store.h"
#include "scheduler.h"
#include "theme.h"
#include "ring.h"
#include "symbols.h"
#include "alert.h"
#include "i18n.h"

#ifndef APP_VERSION
#define APP_VERSION "?"
#endif

#define KEY_STATUS 5          // storage key: status | (slot << 8)
#define KEY_HA_BASE 30        // 30-33: copy of the Home Assistant connection, in chunks
#define HA_CHUNKS 4
#define HA_CHUNK_LEN 250      // characters per chunk (persist strings hold at most 256 bytes)
#define HA_BUF_SIZE (HA_CHUNKS * HA_CHUNK_LEN + 1)
#define STATE_BUF_SIZE 3300
#define NAME_SEPARATOR '\x1f' // separates names in the NAMES field

static const char *const STATUS_KEYS[] = {"idle", "ringing", "snoozed", "dismissed"};

static char s_state[STATE_BUF_SIZE];
static bool s_send_pending;
static bool s_outbox_busy;
static AlarmStatus s_status;
static int s_status_slot;
static void (*s_on_theme_changed)(void);
// Copy of the Home Assistant connection from the phone (JSON with address and webhook ID,
// never the token). If the phone loses its copy (e.g. reinstalled app), it gets it back from
// here instead of registering again, which would create a second set of sensors in HA.
static char s_ha[HA_BUF_SIZE];
static bool s_send_ha;   // phone asked for the copy: send it with the next state

static void prv_ha_load(void) {
  s_ha[0] = '\0';
  for (int i = 0; i < HA_CHUNKS; i++) {
    char part[HA_CHUNK_LEN + 1];
    if (persist_read_string(KEY_HA_BASE + i, part, sizeof(part)) <= 0) break;
    strncat(s_ha, part, sizeof(s_ha) - strlen(s_ha) - 1);
    if (strlen(part) < HA_CHUNK_LEN) break;
  }
}

static void prv_ha_save(const char *text) {
  strncpy(s_ha, text, sizeof(s_ha) - 1);
  s_ha[sizeof(s_ha) - 1] = '\0';
  size_t len = strlen(s_ha);
  for (int i = 0; i < HA_CHUNKS; i++) {
    size_t start = (size_t)i * HA_CHUNK_LEN;
    if (start < len) {
      char part[HA_CHUNK_LEN + 1];
      strncpy(part, s_ha + start, HA_CHUNK_LEN);
      part[HA_CHUNK_LEN] = '\0';
      persist_write_string(KEY_HA_BASE + i, part);
    } else {
      persist_delete(KEY_HA_BASE + i);
    }
  }
}

// ---------------------------------------------------------------------------
// Build JSON
// ---------------------------------------------------------------------------

// Appends formatted text to s_state (vsnprintf is not available on the watch).
#define APPEND(...) do { \
    if (len < sizeof(s_state)) { \
      int n_ = snprintf(s_state + len, sizeof(s_state) - len, __VA_ARGS__); \
      if (n_ > 0) len += n_; \
      if (len > sizeof(s_state) - 1) len = sizeof(s_state) - 1; \
    } \
  } while (0)

// Appends text as a JSON string (quotes and control characters escaped)
static size_t prv_append_str(size_t len, const char *text) {
  APPEND("\"");
  for (size_t i = 0; text && text[i] && len < sizeof(s_state) - 8; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == '"' || c == '\\') {
      APPEND("\\%c", c);
    } else if (c < 0x20) {
      APPEND("\\u%04x", c);
    } else {
      s_state[len++] = c;
      s_state[len] = '\0';
    }
  }
  APPEND("\"");
  return len;
}

static void prv_build_state(void) {
  size_t len = 0;
  s_state[0] = '\0';
  ThemeBase tc = theme_custom_base(0), tc2 = theme_custom_base(1);
  APPEND("{\"v\":\"%s\",\"id\":", APP_VERSION);
  len = prv_append_str(len, store_watch_id());
  APPEND(",\"wn\":");
  len = prv_append_str(len, store_watch_name());
  APPEND(",\"lang\":\"%s\",\"t\":%ld,\"st\":\"%s\",\"ss\":%d",
         g_lang_de ? "de" : "en", (long)time(NULL), STATUS_KEYS[s_status], s_status_slot);
  ThemeSchedule ts = theme_schedule();
  // "hb": the watch keeps a copy of the Home Assistant connection
  // "tc": base colors of Custom 1 and Custom 2
  APPEND(",\"th\":%d,\"tc\":[%d,%d,%d,%d,%d,%d,%d,%d],\"lh\":%d,\"ts\":[%d,%d,%d,%d,%d,%d],\"hb\":%d,\"sf\":%d,\"a\":[",
         theme_mode(), tc.bg.argb, tc.fg.argb, tc.hl_bg.argb, tc.hl_fg.argb,
         tc2.bg.argb, tc2.fg.argb, tc2.hl_bg.argb, tc2.hl_fg.argb,
         theme_left_handed() ? 1 : 0, ts.mode, ts.night, ts.day_min, ts.night_min,
         ts.day_offset, ts.night_offset, s_ha[0] ? 1 : 0,
         symbols_flags());
  for (int slot = 1; slot <= ALARM_SLOTS; slot++) {
    const Alarm *a = store_get(slot);
    if (slot > 1) APPEND(",");
    if (!a->used) {
      APPEND("{\"s\":%d,\"u\":0,\"r\":%lu}", slot, (unsigned long)a->rev);
      continue;
    }
    APPEND("{\"s\":%d,\"u\":1,\"n\":", slot);
    len = prv_append_str(len, a->name);
    APPEND(",\"h\":%d,\"m\":%d,\"d\":%d,\"e\":%d,\"so\":\"%s\",\"vi\":%d,\"p\":\"%s\"",
           a->hour, a->minute, a->days, a->enabled, alarm_sound_key(a->sound), a->vibrate,
           alarm_vibe_key(a->vibe_pattern));
    APPEND(",\"z\":%d,\"pr\":%d,\"vo\":\"%s\",\"ch\":\"%s\",\"cl\":%d,\"cp\":%d,\"rl\":%d,\"sy\":\"%s\",\"r\":%lu,\"nx\":%ld}", a->snooze,
           a->protect, alarm_volume_key(a->volume), alarm_challenge_key(a->challenge),
           a->challenge_len, CHALLENGE_PAUSES_MS[a->challenge_pause], a->remember_len, symbol_key(a->symbol),
           (unsigned long)a->rev, (long)scheduler_next_time(slot));
  }
  APPEND("]}");
}

// ---------------------------------------------------------------------------
// Send
// ---------------------------------------------------------------------------

void comms_send_state(void) {
  if (s_outbox_busy) {
    s_send_pending = true;  // again after the current send
    return;
  }
  if (!connection_service_peek_pebble_app_connection()) {
    s_send_pending = true;
    return;
  }
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    s_send_pending = true;
    return;
  }
  prv_build_state();
  dict_write_cstring(iter, MESSAGE_KEY_STATE, s_state);
  bool with_ha = s_send_ha && s_ha[0];
  if (with_ha) dict_write_cstring(iter, MESSAGE_KEY_HA_CONFIG, s_ha);
  if (app_message_outbox_send() == APP_MSG_OK) {
    s_outbox_busy = true;
    s_send_pending = false;
    if (with_ha) s_send_ha = false;
  } else {
    s_send_pending = true;
  }
}

static void prv_outbox_sent(DictionaryIterator *iter, void *context) {
  s_outbox_busy = false;
  if (s_send_pending) comms_send_state();
}

static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  s_outbox_busy = false;
  s_send_pending = true;  // retry on the next occasion
  APP_LOG(APP_LOG_LEVEL_WARNING, "Sending to phone failed (%d)", reason);
}

void comms_set_status(AlarmStatus status, int slot) {
  s_status = status;
  s_status_slot = slot;
  persist_write_int(KEY_STATUS, (int32_t)status | (slot << 8));
  comms_send_state();
}

// ---------------------------------------------------------------------------
// Receive: settings from the settings page
// ---------------------------------------------------------------------------

// Splits text at sep and calls apply(slot, part) for each part (index 0-7).
static void prv_split(const char *text, char sep, void (*apply)(int slot, const char *part)) {
  char part[ALARM_NAME_BUF];
  int index = 0;
  size_t len = 0;
  for (size_t i = 0;; i++) {
    char c = text[i];
    if (c == sep || c == '\0') {
      part[len] = '\0';
      if (index < ALARM_SLOTS) apply(index + 1, part);
      index++;
      len = 0;
      if (c == '\0') break;
    } else if (len < sizeof(part) - 1) {
      part[len++] = c;
    }
  }
}

static void prv_apply_name(int slot, const char *name) {
  const Alarm *current = store_get(slot);
  if (!current->used) return;
  Alarm a = *current;
  store_copy_name(a.name, name);
  if (strcmp(a.name, current->name) != 0) store_save(slot, &a);
}

// Test alarm from the settings page: sound and vibration pattern of the alarm,
// does not change the stored alarms
static void prv_start_test(int slot) {
  const Alarm *a = store_get(slot);
  AlertParams p = {.vibe_pattern = DEFAULT_VIBE_PATTERN, .vibrate = true,
                   .sound = ALARM_SOUND_OFF, .volume = DEFAULT_VOLUME};
  if (a && a->used) {
    p.vibe_pattern = a->vibe_pattern;
    p.vibrate = a->vibrate;
    p.sound = a->sound;
    p.volume = a->volume;
  }
  if (!p.vibrate && p.sound == ALARM_SOUND_OFF) p.vibrate = true;
  bool used = a && a->used;
  RingChallenge ch = {CHALLENGE_OFF, DEFAULT_CHALLENGE_LEN, 1000};
  if (used) {
    ch.mode = a->challenge;
    ch.length = a->challenge == CHALLENGE_REMEMBER ? a->remember_len : a->challenge_len;
    ch.pause_ms = CHALLENGE_PAUSES_MS[a->challenge_pause];
  }
  ring_show_test(&p, !(used && a->snooze == 0), &ch, used ? a->symbol : SYMBOL_NONE);
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  bool theme_changed = false;
  bool changed = false;
  Tuple *t;

  if (dict_find(iter, MESSAGE_KEY_STOP_ALARM)) {   // "Stop alarm" on the settings page
    if (!ring_stop_remote()) comms_send_state();  // nothing to stop: send the current state anyway
    return;
  }

  if ((t = dict_find(iter, MESSAGE_KEY_TEST_SLOT))) {
    prv_start_test(t->value->int32);
    return;
  }

  store_batch_begin();
  if ((t = dict_find(iter, MESSAGE_KEY_THEME))) {
    // Colors: 4 bytes for Custom 1, optionally 4 more for Custom 2
    Tuple *c = dict_find(iter, MESSAGE_KEY_THEME_COLORS);
    int sets = (c && c->type == TUPLE_BYTE_ARRAY) ? c->length / 4 : 0;
    if (sets > THEME_CUSTOM_COUNT) sets = THEME_CUSTOM_COUNT;
    for (int i = 0; i < sets; i++) {
      const uint8_t *d = c->value->data + 4 * i;
      ThemeBase base = {
        .bg = {.argb = d[0] | 0xC0}, .fg = {.argb = d[1] | 0xC0},
        .hl_bg = {.argb = d[2] | 0xC0}, .hl_fg = {.argb = d[3] | 0xC0},
      };
      ThemeBase old = theme_custom_base(i);
      if (memcmp(&old, &base, sizeof(base)) != 0) {
        theme_set_custom(i, &base);
        theme_changed = true;
      }
    }
    int mode = t->value->int32;
    if (mode != theme_mode()) {
      theme_set(mode);
      theme_changed = true;
    }
    changed = true;
  }
  ThemeMode active_before = theme_active();
  if ((t = dict_find(iter, MESSAGE_KEY_THEME_SCHEDULE)) && t->type == TUPLE_BYTE_ARRAY &&
      t->length >= 10) {
    // mode, night scheme, day/night minute (uint16 LE), day/night offset (int16 LE)
    const uint8_t *d = t->value->data;
    ThemeSchedule ts = {
      .mode = d[0], .night = d[1],
      .day_min = d[2] | (d[3] << 8), .night_min = d[4] | (d[5] << 8),
      .day_offset = (int16_t)(d[6] | (d[7] << 8)), .night_offset = (int16_t)(d[8] | (d[9] << 8)),
    };
    theme_set_schedule(&ts);
    changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_TIMES)) && t->type == TUPLE_BYTE_ARRAY && t->length >= 2) {
    // first day (uint16 LE), then sunrise/sunset pairs (uint16 LE each)
    const uint8_t *d = t->value->data;
    theme_set_sun_table(d[0] | (d[1] << 8), d + 2, (t->length - 2) / 4);
  }
  if (theme_active() != active_before) theme_changed = true;
  if ((t = dict_find(iter, MESSAGE_KEY_NAMES)) && t->type == TUPLE_CSTRING) {
    prv_split(t->value->cstring, NAME_SEPARATOR, prv_apply_name);
    changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WATCH_NAME)) && t->type == TUPLE_CSTRING) {
    store_set_watch_name(t->value->cstring);
    changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_LEFT_HANDED))) {
    bool on = t->value->int32 != 0;
    if (on != theme_left_handed()) theme_set_left_handed(on);
    changed = true;
  }
  store_batch_end();

  if ((t = dict_find(iter, MESSAGE_KEY_SYMBOL_FLAGS))) {
    uint8_t flags = (uint8_t)t->value->int32;
    if (flags != symbols_flags()) {
      symbols_set_flags(flags);
      theme_changed = true;   // redraw list and menus
    }
    changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_HA_CONFIG)) && t->type == TUPLE_CSTRING) {
    prv_ha_save(t->value->cstring);   // empty = connection removed
    changed = true;
  }
  Tuple *request = dict_find(iter, MESSAGE_KEY_REQUEST_STATE);
  if (request && request->value->int32 == 2) s_send_ha = true;   // phone lost its copy
  if (changed || request) comms_send_state();
  if (theme_changed && s_on_theme_changed) s_on_theme_changed();
}

void comms_init(void (*on_theme_changed)(void)) {
  s_on_theme_changed = on_theme_changed;
  int32_t raw = persist_exists(KEY_STATUS) ? persist_read_int(KEY_STATUS) : 0;
  s_status = (raw & 0xFF) <= ALARM_STATUS_DISMISSED ? (AlarmStatus)(raw & 0xFF) : ALARM_STATUS_IDLE;
  s_status_slot = (raw >> 8) & 0xFF;
  prv_ha_load();

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_outbox_sent(prv_outbox_sent);
  app_message_register_outbox_failed(prv_outbox_failed);
  uint32_t inbox = app_message_inbox_size_maximum();
  uint32_t outbox = app_message_outbox_size_maximum();
  uint32_t wanted = STATE_BUF_SIZE + HA_BUF_SIZE + 64;
  app_message_open(inbox < 2048 ? inbox : 2048, outbox < wanted ? outbox : wanted);
}
