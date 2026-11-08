#include "theme.h"

// Storage keys (other keys see alarm_store.c: 1-5 and 101-108)
#define KEY_THEME_MODE   10
#define KEY_THEME_CUSTOM 11   // 4 bytes: GColor8 of bg, fg, hl_bg, hl_fg
#define KEY_LEFT_HANDED  12

static Theme s_theme;
static bool s_left_handed;
static ThemeMode s_mode = THEME_CERULEAN;
static ThemeBase s_custom;

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

static void prv_apply(void) {
  switch (s_mode) {
    case THEME_DARK:   prv_build_dark(); break;
    case THEME_CUSTOM: prv_build_custom(&s_custom); break;
    case THEME_CERULEAN: prv_build_preset(&CERULEAN_PRESET); break;
    case THEME_LIGHT:  prv_build_light(); break;
    default:
      if (s_mode >= THEME_CERULEAN_NIGHT && s_mode < THEME_MODE_COUNT) {
        prv_build_preset(&PRESETS[s_mode - THEME_CERULEAN_NIGHT]);
      } else {
        prv_build_light();
      }
      break;
  }
}

void theme_init(void) {
  s_custom = LIGHT_BASE;
  int32_t mode = persist_exists(KEY_THEME_MODE) ? persist_read_int(KEY_THEME_MODE) : THEME_CERULEAN;
  s_mode = (mode >= 0 && mode < THEME_MODE_COUNT) ? (ThemeMode)mode : THEME_CERULEAN;
  uint8_t raw[4];
  if (persist_read_data(KEY_THEME_CUSTOM, raw, sizeof(raw)) == (int)sizeof(raw)) {
    s_custom.bg = (GColor) {.argb = raw[0] | 0xC0};
    s_custom.fg = (GColor) {.argb = raw[1] | 0xC0};
    s_custom.hl_bg = (GColor) {.argb = raw[2] | 0xC0};
    s_custom.hl_fg = (GColor) {.argb = raw[3] | 0xC0};
  }
  s_left_handed = persist_exists(KEY_LEFT_HANDED) && persist_read_bool(KEY_LEFT_HANDED);
  prv_apply();
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

ThemeBase theme_custom_base(void) {
  return s_custom;
}

void theme_set(ThemeMode mode, const ThemeBase *base) {
  if (mode >= THEME_MODE_COUNT) mode = THEME_LIGHT;
  s_mode = mode;
  persist_write_int(KEY_THEME_MODE, mode);
  if (mode == THEME_CUSTOM && base) {
    s_custom = *base;
    uint8_t raw[4] = {base->bg.argb, base->fg.argb, base->hl_bg.argb, base->hl_fg.argb};
    persist_write_data(KEY_THEME_CUSTOM, raw, sizeof(raw));
  }
  prv_apply();
}
