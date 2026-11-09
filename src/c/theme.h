#pragma once
// UI color schemes: presets, Light, Dark or Custom 1/2 (4 base colors each from the phone,
// in-between shades are derived from them).
// The scheme is stored on the watch and selected on the phone.
#include <pebble.h>

typedef enum {
  THEME_LIGHT = 0,
  THEME_DARK = 1,
  THEME_CUSTOM = 2,       // "Custom 1"
  THEME_CERULEAN = 3,     // "Cerulean Blue": white, black text, blue selection (default)
  // More presets; colors in theme.c, PRESETS. Numbers are persisted: only append.
  THEME_CERULEAN_NIGHT = 4,
  THEME_FOREST = 5,
  THEME_NIGHT_LIGHT = 6,
  THEME_HIGH_CONTRAST = 7,
  THEME_BERRY = 8,
  THEME_LAVENDER = 9,
  THEME_MINT = 10,
  THEME_CUSTOM_2 = 11,    // "Custom 2", e.g. for darkness
  THEME_MODE_COUNT
} ThemeMode;

// The 4 base colors of a custom scheme
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

// Stored (day) scheme; theme() returns the night scheme at night if a schedule is set.
ThemeMode theme_mode(void);
#define THEME_CUSTOM_COUNT 2
// Base colors of Custom 1 (index 0) or Custom 2 (index 1)
ThemeBase theme_custom_base(int index);

// Stores and activates the (day) scheme.
void theme_set(ThemeMode mode);
// Stores the colors of Custom 1 (index 0) or Custom 2 (index 1) and redraws if it is shown.
void theme_set_custom(int index, const ThemeBase *base);

// Day/night: a second scheme for the night, switched by fixed times or by the sun.
typedef enum {
  THEME_SCHED_OFF = 0,     // always the day scheme
  THEME_SCHED_TIMES = 1,   // day from day_min, night from night_min (minutes after midnight)
  THEME_SCHED_SUN = 2,     // day from sunrise + day_offset, night from sunset + night_offset
} ThemeScheduleMode;

typedef struct {
  uint8_t mode;            // ThemeScheduleMode
  uint8_t night;           // ThemeMode at night
  uint16_t day_min;
  uint16_t night_min;
  int16_t day_offset;      // minutes, may be negative
  int16_t night_offset;
} ThemeSchedule;

#define THEME_SUN_DAYS 60  // sunrise/sunset table from the phone (local minutes per day)

ThemeSchedule theme_schedule(void);
// Scheme shown right now (day or night)
ThemeMode theme_active(void);
void theme_set_schedule(const ThemeSchedule *schedule);
// Table from the phone: first day (days since 1970-01-01, local date) and count pairs of
// sunrise/sunset in minutes after local midnight (uint16 little endian; rise == set: polar
// night, 0/1440: polar day). count <= THEME_SUN_DAYS.
void theme_set_sun_table(uint16_t first_day, const uint8_t *rise_set, int count);
// Re-evaluates day/night for the current time. Returns true if the active scheme changed.
bool theme_update(void);

// Left-handed mode (set on the phone): the watch is rotated, the buttons are on the left.
// Button hints (ringing screen, confirmation) are then drawn on the left instead of the right.
// Apps cannot read the system setting (yet), hence an own setting.
bool theme_left_handed(void);
void theme_set_left_handed(bool on);

// x position of a bar of width w at the button edge (right; left in left-handed mode)
int theme_button_edge_x(int screen_w, int w);
