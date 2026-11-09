#include "theme.h"

// Storage keys (other keys see alarm_store.c: 1-5 and 101-108)
#define KEY_THEME_MODE   10
#define KEY_THEME_CUSTOM 11   // 4 bytes: GColor8 of bg, fg, hl_bg, hl_fg (Custom 1)
#define KEY_LEFT_HANDED  12
#define KEY_THEME_SCHED  13   // ThemeSchedule
#define KEY_THEME_SUN    14   // SunTable
#define KEY_THEME_CUSTOM2 16  // like KEY_THEME_CUSTOM, for Custom 2 (15 = symbol flags)

static Theme s_theme;
static bool s_left_handed;
static ThemeMode s_mode = THEME_CERULEAN;
static ThemeBase s_custom[THEME_CUSTOM_COUNT];
static const uint32_t CUSTOM_KEYS[THEME_CUSTOM_COUNT] = {KEY_THEME_CUSTOM, KEY_THEME_CUSTOM2};
static ThemeSchedule s_sched = {.mode = THEME_SCHED_OFF, .night = THEME_CERULEAN_NIGHT,
                                .day_min = 7 * 60, .night_min = 21 * 60};
static ThemeMode s_active;   // scheme currently shown (day or night)

typedef struct {
  uint16_t first_day;
  uint8_t count;
  uint8_t reserved;
  uint16_t rise_set[2 * THEME_SUN_DAYS];
} SunTable;
static SunTable s_sun;

// Light: the original design
static const ThemeBase LIGHT_BASE = {
  .bg = {.argb = GColorWhiteARGB8}, .fg = {.argb = GColorBlackARGB8},
  .hl_bg = {.argb = GColorBlackARGB8}, .hl_fg = {.argb = GColorWhiteARGB8},
};

// Mixes two colors. weight_a = share of a in thirds (0-3).
// The watch has 4 levels per color channel, so thirds are enough.
static GColor prv_mix(GColor a, GColor b, int weight_a) {
  GColor c;
  c.a = 3;
  c.r = (a.r * weight_a + b.r * (3 - weight_a) + 1) / 3;
  c.g = (a.g * weight_a + b.g * (3 - weight_a) + 1) / 3;
  c.b = (a.b * weight_a + b.b * (3 - weight_a) + 1) / 3;
  return c;
}

static void prv_build_light(void) {
  s_theme = (Theme) {
    .bg = GColorWhite, .fg = GColorBlack,
    .dim = GColorDarkGray, .faint = GColorLightGray,
    .hl_bg = GColorBlack, .hl_fg = GColorWhite,
    .hl_dim = GColorLightGray, .hl_faint = GColorDarkGray,
    .header_bg = GColorBlack, .header_fg = GColorWhite, .header_dim = GColorLightGray,
    .bar_bg = GColorLightGray, .bar_fg = GColorBlack,
  };
}

// Dark: Light with swapped colors (black <-> white, light gray <-> dark gray)
static void prv_build_dark(void) {
  s_theme = (Theme) {
    .bg = GColorBlack, .fg = GColorWhite,
    .dim = GColorLightGray, .faint = GColorDarkGray,
    .hl_bg = GColorWhite, .hl_fg = GColorBlack,
    .hl_dim = GColorDarkGray, .hl_faint = GColorLightGray,
    .header_bg = GColorWhite, .header_fg = GColorBlack, .header_dim = GColorDarkGray,
    .bar_bg = GColorDarkGray, .bar_fg = GColorWhite,
  };
}

// Custom: from 4 base colors, in-between shades are mixed
static void prv_build_custom(const ThemeBase *b) {
  GColor header_bg = b->hl_bg;   // header in the selection colors
  s_theme = (Theme) {
    .bg = b->bg, .fg = b->fg,
    .dim = prv_mix(b->fg, b->bg, 2), .faint = prv_mix(b->fg, b->bg, 1),
    .hl_bg = b->hl_bg, .hl_fg = b->hl_fg,
    .hl_dim = prv_mix(b->hl_fg, b->hl_bg, 2), .hl_faint = prv_mix(b->hl_fg, b->hl_bg, 1),
    .header_bg = header_bg, .header_fg = b->hl_fg, .header_dim = prv_mix(b->hl_fg, header_bg, 2),
    .bar_bg = prv_mix(b->fg, b->bg, 1), .bar_fg = b->fg,
  };
}

// Presets: 4 base colors like Custom, plus a fixed color for the "+ NEW" bar
// (instead of the mixed color). Values as GColor8 (argb, 2 bits per channel). The same table
// is in src/config/index.html (PRESETS) for the design preview.
typedef struct {
  uint8_t bg, fg, hl_bg, hl_fg, bar_bg, bar_fg;
} ThemePreset;

// "Cerulean Blue" (default): like Light, but selection and headers in blue #18BCF2. The watch
// only has 64 colors; the closest is Vivid Cerulean (#00AAFF). Black text on it,
// light blue bar (#AAFFFF).
static const ThemePreset CERULEAN_PRESET = {0xFF, 0xC0, 0xCB, 0xC0, 0xEF, 0xC0};

static const ThemePreset PRESETS[] = {
  [THEME_CERULEAN_NIGHT - THEME_CERULEAN_NIGHT] = {0xC0, 0xFF, 0xCB, 0xC0, 0xC6, 0xFF},  // blue on black
  [THEME_FOREST - THEME_CERULEAN_NIGHT]         = {0xC0, 0xFF, 0xCD, 0xC0, 0xC4, 0xFF},  // green on black
  [THEME_NIGHT_LIGHT - THEME_CERULEAN_NIGHT]    = {0xC0, 0xF8, 0xF4, 0xC0, 0xE4, 0xC0},  // gold/orange
  [THEME_HIGH_CONTRAST - THEME_CERULEAN_NIGHT]  = {0xC0, 0xFF, 0xFC, 0xC0, 0xD5, 0xFF},  // yellow on black
  [THEME_BERRY - THEME_CERULEAN_NIGHT]          = {0xFF, 0xC0, 0xF1, 0xFF, 0xFA, 0xC0},  // raspberry on white
  [THEME_LAVENDER - THEME_CERULEAN_NIGHT]       = {0xFF, 0xC0, 0xE7, 0xFF, 0xEB, 0xC0},  // purple on white
  [THEME_MINT - THEME_CERULEAN_NIGHT]           = {0xFF, 0xC0, 0xDE, 0xC0, 0xEE, 0xC0},  // mint on white
};

static void prv_build_preset(const ThemePreset *p) {
  ThemeBase b = {
    .bg = {.argb = p->bg}, .fg = {.argb = p->fg}, .hl_bg = {.argb = p->hl_bg}, .hl_fg = {.argb = p->hl_fg},
  };
  prv_build_custom(&b);
  s_theme.bar_bg = (GColor) {.argb = p->bar_bg};
  s_theme.bar_fg = (GColor) {.argb = p->bar_fg};
}

static void prv_apply(ThemeMode mode) {
  s_active = mode;
  switch (mode) {
    case THEME_DARK:   prv_build_dark(); break;
    case THEME_CUSTOM: prv_build_custom(&s_custom[0]); break;
    case THEME_CUSTOM_2: prv_build_custom(&s_custom[1]); break;
    case THEME_CERULEAN: prv_build_preset(&CERULEAN_PRESET); break;
    case THEME_LIGHT:  prv_build_light(); break;
    default:
      if (mode >= THEME_CERULEAN_NIGHT && mode <= THEME_MINT) {
        prv_build_preset(&PRESETS[mode - THEME_CERULEAN_NIGHT]);
      } else {
        prv_build_light();
      }
      break;
  }
}

static void prv_sanitize_schedule(ThemeSchedule *s) {
  if (s->mode > THEME_SCHED_SUN) s->mode = THEME_SCHED_OFF;
  if (s->night >= THEME_MODE_COUNT) s->night = THEME_CERULEAN_NIGHT;
  if (s->day_min >= 24 * 60) s->day_min = 7 * 60;
  if (s->night_min >= 24 * 60) s->night_min = 21 * 60;
  if (s->day_offset < -12 * 60 || s->day_offset > 12 * 60) s->day_offset = 0;
  if (s->night_offset < -12 * 60 || s->night_offset > 12 * 60) s->night_offset = 0;
}

// Days since 1970-01-01 for a calendar date (proleptic Gregorian)
static int32_t prv_days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static int prv_wrap_day(int minutes) {
  minutes %= 24 * 60;
  return minutes < 0 ? minutes + 24 * 60 : minutes;
}

// Is it day at minute "now" if the day starts at "day" and the night at "night"?
static bool prv_is_day(int now, int day, int night) {
  if (day == night) return true;
  if (day < night) return now >= day && now < night;
  return now >= day || now < night;
}

static ThemeMode prv_wanted_mode(void) {
  if (s_sched.mode == THEME_SCHED_OFF) return s_mode;
  time_t t = time(NULL);
  struct tm *lt = localtime(&t);
  int now = lt->tm_hour * 60 + lt->tm_min;
  int day, night;
  if (s_sched.mode == THEME_SCHED_TIMES) {
    day = s_sched.day_min;
    night = s_sched.night_min;
  } else {
    if (s_sun.count == 0) return s_mode;   // no sun times from the phone yet
    int32_t today = prv_days_from_civil(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    int32_t i = today - s_sun.first_day;
    if (i < 0) i = 0;
    if (i >= s_sun.count) i = s_sun.count - 1;   // table too old: use its last day
    int rise = s_sun.rise_set[2 * i], set = s_sun.rise_set[2 * i + 1];
    if (rise == set) return s_sched.night;        // polar night
    if (rise == 0 && set >= 24 * 60) return s_mode;   // polar day
    day = prv_wrap_day(rise + s_sched.day_offset);
    night = prv_wrap_day(set + s_sched.night_offset);
  }
  return prv_is_day(now, day, night) ? s_mode : (ThemeMode)s_sched.night;
}

bool theme_update(void) {
  ThemeMode wanted = prv_wanted_mode();
  if (wanted == s_active) return false;
  prv_apply(wanted);
  return true;
}

ThemeMode theme_active(void) {
  return s_active;
}

ThemeSchedule theme_schedule(void) {
  return s_sched;
}

void theme_set_schedule(const ThemeSchedule *schedule) {
  ThemeSchedule s = *schedule;
  prv_sanitize_schedule(&s);
  s_sched = s;
  persist_write_data(KEY_THEME_SCHED, &s_sched, sizeof(s_sched));
  prv_apply(prv_wanted_mode());
}

void theme_set_sun_table(uint16_t first_day, const uint8_t *rise_set, int count) {
  if (count < 0) count = 0;
  if (count > THEME_SUN_DAYS) count = THEME_SUN_DAYS;
  s_sun.first_day = first_day;
  s_sun.count = count;
  for (int i = 0; i < 2 * count; i++) {   // little endian, byte by byte (no alignment needed)
    s_sun.rise_set[i] = rise_set[2 * i] | (rise_set[2 * i + 1] << 8);
  }
  persist_write_data(KEY_THEME_SUN, &s_sun, sizeof(s_sun));
  prv_apply(prv_wanted_mode());
}

void theme_init(void) {
  int32_t mode = persist_exists(KEY_THEME_MODE) ? persist_read_int(KEY_THEME_MODE) : THEME_CERULEAN;
  s_mode = (mode >= 0 && mode < THEME_MODE_COUNT) ? (ThemeMode)mode : THEME_CERULEAN;
  for (int i = 0; i < THEME_CUSTOM_COUNT; i++) {
    s_custom[i] = LIGHT_BASE;
    uint8_t raw[4];
    if (persist_read_data(CUSTOM_KEYS[i], raw, sizeof(raw)) == (int)sizeof(raw)) {
      s_custom[i].bg = (GColor) {.argb = raw[0] | 0xC0};
      s_custom[i].fg = (GColor) {.argb = raw[1] | 0xC0};
      s_custom[i].hl_bg = (GColor) {.argb = raw[2] | 0xC0};
      s_custom[i].hl_fg = (GColor) {.argb = raw[3] | 0xC0};
    }
  }
  s_left_handed = persist_exists(KEY_LEFT_HANDED) && persist_read_bool(KEY_LEFT_HANDED);
  ThemeSchedule sched;
  if (persist_read_data(KEY_THEME_SCHED, &sched, sizeof(sched)) == (int)sizeof(sched)) {
    prv_sanitize_schedule(&sched);
    s_sched = sched;
  }
  if (persist_read_data(KEY_THEME_SUN, &s_sun, sizeof(s_sun)) != (int)sizeof(s_sun) ||
      s_sun.count > THEME_SUN_DAYS) {
    s_sun.count = 0;
  }
  prv_apply(prv_wanted_mode());
}

bool theme_left_handed(void) {
  return s_left_handed;
}

void theme_set_left_handed(bool on) {
  s_left_handed = on;
  persist_write_bool(KEY_LEFT_HANDED, on);
}

int theme_button_edge_x(int screen_w, int w) {
  return s_left_handed ? 0 : screen_w - w;
}

const Theme *theme(void) {
  return &s_theme;
}

ThemeMode theme_mode(void) {
  return s_mode;
}

ThemeBase theme_custom_base(int index) {
  return s_custom[index == 1 ? 1 : 0];
}

void theme_set(ThemeMode mode) {
  if (mode >= THEME_MODE_COUNT) mode = THEME_LIGHT;
  s_mode = mode;
  persist_write_int(KEY_THEME_MODE, mode);
  prv_apply(prv_wanted_mode());
}

void theme_set_custom(int index, const ThemeBase *base) {
  if (index < 0 || index >= THEME_CUSTOM_COUNT || !base) return;
  s_custom[index] = *base;
  uint8_t raw[4] = {base->bg.argb, base->fg.argb, base->hl_bg.argb, base->hl_fg.argb};
  persist_write_data(CUSTOM_KEYS[index], raw, sizeof(raw));
  prv_apply(prv_wanted_mode());
}
