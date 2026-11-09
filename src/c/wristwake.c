#include <pebble.h>
#include "alarm_store.h"
#include "alarm_list.h"
#include "scheduler.h"
#include "ring.h"
#include "comms.h"
#include "theme.h"
#include "ui_common.h"
#include "i18n.h"
#include "symbols.h"

bool g_lang_de = false;

// After every alarm change: reschedule and send the state to the phone
static void prv_store_changed(void) {
  scheduler_update();
  if (scheduler_failed_mask()) {
    ui_show_message(TR("An alarm could not be scheduled.", "Ein Wecker konnte nicht geplant werden."));
  }
  comms_send_state();
}

// New theme (from the phone or day/night switch): rebuild the UI (not while an alarm is ringing)
static void prv_theme_changed(void) {
  if (ring_is_active()) return;
  window_stack_pop_all(false);
  alarm_list_show();
}

// Every minute: switch between the day and night scheme when its time has come
static void prv_minute_tick(struct tm *tick_time, TimeUnits changed) {
  if (theme_update()) prv_theme_changed();
}

// Alarm rings while the app is open
static void prv_wakeup(WakeupId id, int32_t cookie) {
  scheduler_update();
  ring_show(cookie & SCHED_SLOT_MASK);
}

int main(void) {
  i18n_init();
  store_init();
  theme_init();
  symbols_init();
#ifdef PREVIEW_THEME
  // For test screenshots only: build with "#define PREVIEW_THEME 1" (Dark) or 2 (Custom).
  // Note: the theme is then stored on the watch.
  ThemeBase preview = {GColorOxfordBlue, GColorWhite, GColorOrange, GColorBlack};
  theme_set_custom(0, &preview);
  theme_set(PREVIEW_THEME);
#endif
  store_set_change_handler(prv_store_changed);
  comms_init(prv_theme_changed);
  // Touch: scroll lists and menus by swiping, open items by tapping
  app_touch_navigation_enable(true);

  // Launched by an alarm? Then show the ringing screen right away.
  uint8_t ring_mask = 0;
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    WakeupId id;
    int32_t cookie;
    if (wakeup_get_launch_event(&id, &cookie)) ring_mask = cookie & SCHED_SLOT_MASK;
  }

  // Recompute all times on every start (handles time zone and DST changes)
  scheduler_update();
  wakeup_service_subscribe(prv_wakeup);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_minute_tick);

  // Safety net: "ringing" is stored, but this start is not an alarm, so the last ringing
  // ended without snooze or stop (e.g. the app was closed before the status could be saved)
  if (!ring_mask && comms_status() == ALARM_STATUS_RINGING) {
    int slot = comms_status_slot();
    if (slot >= 1 && slot <= ALARM_SLOTS) ring_end_one_time(slot);   // rings only once
    comms_set_status(ALARM_STATUS_IDLE, 0);
  }

  if (ring_mask) {
    ring_show(ring_mask);   // after snooze/stop the app closes again
  } else {
    alarm_list_show();
  }
  app_event_loop();
}
