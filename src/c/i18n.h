#pragma once
// Simple bilingual UI: German if the watch language is German, otherwise English.
// Call i18n_init() once at start; then use TR("English", "German").
#include <pebble.h>

extern bool g_lang_de;

static inline void i18n_init(void) {
  const char *locale = i18n_get_system_locale();
  g_lang_de = locale && strncmp(locale, "de", 2) == 0;
}

#define TR(en, de) (g_lang_de ? (de) : (en))
