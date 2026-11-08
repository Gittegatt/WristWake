#pragma once
// UI color schemes: presets, Light, Dark or Custom (4 base colors from the phone,
// in-between shades are derived from them).
// The scheme is stored on the watch and selected on the phone.
#include <pebble.h>

typedef enum {
  THEME_LIGHT = 0,
  THEME_DARK = 1,
  THEME_CUSTOM = 2,
  THEME_CERULEAN = 3,     // "Cerulean Blue": white, black text, blue selection (default)
  // More presets; colors in theme.c, PRESETS. Numbers are persisted: only append.
  THEME_CERULEAN_NIGHT = 4,
  THEME_FOREST = 5,
  THEME_NIGHT_LIGHT = 6,
  THEME_HIGH_CONTRAST = 7,
  THEME_BERRY = 8,
  THEME_LAVENDER = 9,
  THEME_MINT = 10,
  THEME_MODE_COUNT
} ThemeMode;

// The 4 base colors for Custom
typedef struct {
  GColor bg;      // background
  GColor fg;      // text
  GColor hl_bg;   // selection background
  GColor hl_fg;   // selection text
} ThemeBase;

typedef struct {
  GColor bg, fg;               // normal background and text
  GColor dim, faint;           // secondary text / inactive elements on normal background
  GColor hl_bg, hl_fg;         // selected item
  GColor hl_dim, hl_faint;     // secondary text / inactive elements on the selection
  GColor header_bg, header_fg, header_dim;  // headers and messages
  GColor bar_bg, bar_fg;       // "+ NEW" bar when not selected
} Theme;

// Loads the stored scheme (once at start).
void theme_init(void);

// Current color scheme for all drawing functions.
const Theme *theme(void);

ThemeMode theme_mode(void);
ThemeBase theme_custom_base(void);

// Stores and activates a scheme. base is only used for THEME_CUSTOM (may be NULL otherwise).
void theme_set(ThemeMode mode, const ThemeBase *base);

// Left-handed mode (set on the phone): the watch is rotated, the buttons are on the left.
// Button hints (ringing screen, confirmation) are then drawn on the left instead of the right.
// Apps cannot read the system setting (yet), hence an own setting.
bool theme_left_handed(void);
void theme_set_left_handed(bool on);

// x position of a bar of width w at the button edge (right; left in left-handed mode)
int theme_button_edge_x(int screen_w, int w);
