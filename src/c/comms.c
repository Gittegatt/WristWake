#include "comms.h"
#include "alarm_store.h"
#include "scheduler.h"
#include "theme.h"
#include "ring.h"
#include "alert.h"
#include "i18n.h"

#ifndef APP_VERSION
#define APP_VERSION "?"
#endif

#define KEY_STATUS 5          // storage key: status | (slot << 8)
#define STATE_BUF_SIZE 2400
#define NAME_SEPARATOR '\x1f' // separates names in the NAMES field

static const char *const STATUS_KEYS[] = {"idle", "ringing", "snoozed", "dismissed"};

static char s_state[STATE_BUF_SIZE];
static bool s_send_pending;
static bool s_outbox_busy;
static AlarmStatus s_status;
static int s_status_slot;
static void (*s_on_theme_changed)(void);

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
  ThemeBase tc = theme_custom_base();
  APPEND("{\"v\":\"%s\",\"id\":", APP_VERSION);
  len = prv_append_str(len, store_watch_id());
  APPEND(",\"wn\":");
  len = prv_append_str(len, store_watch_name());
  APPEND(",\"lang\":\"%s\",\"t\":%ld,\"st\":\"%s\",\"ss\":%d",
         g_lang_de ? "de" : "en", (long)time(NULL), STATUS_KEYS[s_status], s_status_slot);
  APPEND(",\"th\":%d,\"tc\":[%d,%d,%d,%d],\"lh\":%d,\"a\":[",
         theme_mode(), tc.bg.argb, tc.fg.argb, tc.hl_bg.argb, tc.hl_fg.argb,
         theme_left_handed() ? 1 : 0);
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
    APPEND(",\"z\":%d,\"pr\":%d,\"vo\":\"%s\",\"r\":%lu,\"nx\":%ld}", a->snooze, a->protect,
           alarm_volume_key(a->volume),
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
  if (app_message_outbox_send() == APP_MSG_OK) {
    s_outbox_busy = true;
    s_send_pending = false;
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
// does not change the stored alarms. volume >= 0 overrides the alarm's volume
// (TEMPORARY volume test buttons on the settings page).
static void prv_start_test(int slot, int volume) {
  const Alarm *a = store_get(slot);
  AlertParams p = {.vibe_pattern = DEFAULT_VIBE_PATTERN, .vibrate = true,
                   .sound = ALARM_SOUND_OFF, .volume = DEFAULT_VOLUME};
  if (a && a->used) {
    p.vibe_pattern = a->vibe_pattern;
    p.vibrate = a->vibrate;
    p.sound = a->sound;
    p.volume = a->volume;
  }
  if (volume >= 0 && volume < ALARM_VOLUME_COUNT) p.volume = volume;
  if (!p.vibrate && p.sound == ALARM_SOUND_OFF) p.vibrate = true;
  ring_show_test(&p);
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  bool theme_changed = false;
  bool changed = false;
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_TEST_SLOT))) {
    Tuple *v = dict_find(iter, MESSAGE_KEY_TEST_VOLUME);
    prv_start_test(t->value->int32, v ? (int)v->value->int32 : -1);
    return;
  }

  store_batch_begin();
  if ((t = dict_find(iter, MESSAGE_KEY_THEME))) {
    ThemeBase base = theme_custom_base();
    Tuple *c = dict_find(iter, MESSAGE_KEY_THEME_COLORS);
    if (c && c->type == TUPLE_BYTE_ARRAY && c->length >= 4) {
      const uint8_t *d = (const uint8_t *)c->value;
      base.bg = (GColor) {.argb = d[0] | 0xC0};
      base.fg = (GColor) {.argb = d[1] | 0xC0};
      base.hl_bg = (GColor) {.argb = d[2] | 0xC0};
      base.hl_fg = (GColor) {.argb = d[3] | 0xC0};
    }
    ThemeBase old = theme_custom_base();
    int mode = t->value->int32;
    if (mode != theme_mode() || memcmp(&old, &base, sizeof(base)) != 0) {
      theme_set(mode, &base);
      theme_changed = true;
    }
    changed = true;
  }
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

  if (changed || dict_find(iter, MESSAGE_KEY_REQUEST_STATE)) comms_send_state();
  if (theme_changed && s_on_theme_changed) s_on_theme_changed();
}

void comms_init(void (*on_theme_changed)(void)) {
  s_on_theme_changed = on_theme_changed;
  int32_t raw = persist_exists(KEY_STATUS) ? persist_read_int(KEY_STATUS) : 0;
  s_status = (raw & 0xFF) <= ALARM_STATUS_DISMISSED ? (AlarmStatus)(raw & 0xFF) : ALARM_STATUS_IDLE;
  s_status_slot = (raw >> 8) & 0xFF;

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_outbox_sent(prv_outbox_sent);
  app_message_register_outbox_failed(prv_outbox_failed);
  uint32_t inbox = app_message_inbox_size_maximum();
  uint32_t outbox = app_message_outbox_size_maximum();
  app_message_open(inbox < 2048 ? inbox : 2048, outbox < STATE_BUF_SIZE + 64 ? outbox : STATE_BUF_SIZE + 64);
}
