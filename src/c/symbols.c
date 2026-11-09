#include "symbols.h"
#include "i18n.h"

#define KEY_SYMBOL_FLAGS 15

typedef struct {
  const char *key;
  const char *en;
  const char *de;
  uint32_t rgb;          // tile color (watch palette)
  uint32_t res_small;
  uint32_t res_large;
} SymbolInfo;

static const SymbolInfo SYMBOLS[SYMBOL_COUNT - 1] = {
  {"sun", "Sun", "Sonne", 0xFFAA00, RESOURCE_ID_SYM_SUN_S, RESOURCE_ID_SYM_SUN_L},
  {"sun_horizon", "Sun horizon", "Sonne Horizont", 0xFFAA00, RESOURCE_ID_SYM_SUN_HORIZON_S, RESOURCE_ID_SYM_SUN_HORIZON_L},
  {"moon", "Moon", "Mond", 0x5555FF, RESOURCE_ID_SYM_MOON_S, RESOURCE_ID_SYM_MOON_L},
  {"moon_horizon", "Moon horizon", "Mond Horizont", 0x5555FF, RESOURCE_ID_SYM_MOON_HORIZON_S, RESOURCE_ID_SYM_MOON_HORIZON_L},
  {"cup", "Cup", "Tasse", 0xAA5500, RESOURCE_ID_SYM_CUP_S, RESOURCE_ID_SYM_CUP_L},
  {"work", "Work", "Arbeit", 0x0055AA, RESOURCE_ID_SYM_WORK_S, RESOURCE_ID_SYM_WORK_L},
  {"sleep", "Sleep", "Schlafen", 0xAA55FF, RESOURCE_ID_SYM_SLEEP_S, RESOURCE_ID_SYM_SLEEP_L},
  {"medicine", "Medicine", "Medizin", 0xFF5500, RESOURCE_ID_SYM_MEDICINE_S, RESOURCE_ID_SYM_MEDICINE_L},
  {"sport", "Sport", "Sport", 0x00AA00, RESOURCE_ID_SYM_SPORT_S, RESOURCE_ID_SYM_SPORT_L},
  {"learn", "Learn", "Lernen", 0xFF00AA, RESOURCE_ID_SYM_LEARN_S, RESOURCE_ID_SYM_LEARN_L},
  {"heart", "Heart", "Herz", 0xFF0055, RESOURCE_ID_SYM_HEART_S, RESOURCE_ID_SYM_HEART_L},
  {"pet", "Pet", "Haustier", 0x00AAAA, RESOURCE_ID_SYM_PET_S, RESOURCE_ID_SYM_PET_L},
  {"bell", "Bell", "Glocke", 0x5555AA, RESOURCE_ID_SYM_BELL_S, RESOURCE_ID_SYM_BELL_L},
  {"home", "Home", "Haus", 0x00AAFF, RESOURCE_ID_SYM_HOME_S, RESOURCE_ID_SYM_HOME_L},
  {"train", "Train", "Zug", 0xAAAA00, RESOURCE_ID_SYM_TRAIN_S, RESOURCE_ID_SYM_TRAIN_L},
  {"car", "Car", "Auto", 0xAA0000, RESOURCE_ID_SYM_CAR_S, RESOURCE_ID_SYM_CAR_L},
  {"trash", "Trash", "Müll", 0x555555, RESOURCE_ID_SYM_TRASH_S, RESOURCE_ID_SYM_TRASH_L},
  {"phone", "Phone", "Telefon", 0xAA00AA, RESOURCE_ID_SYM_PHONE_S, RESOURCE_ID_SYM_PHONE_L},
  {"birthday", "Birthday", "Geburtstag", 0xFF55AA, RESOURCE_ID_SYM_BIRTHDAY_S, RESOURCE_ID_SYM_BIRTHDAY_L},
  {"child", "Child", "Kind", 0x55AA55, RESOURCE_ID_SYM_CHILD_S, RESOURCE_ID_SYM_CHILD_L},
  {"plane", "Plane", "Flugzeug", 0x55AAFF, RESOURCE_ID_SYM_PLANE_S, RESOURCE_ID_SYM_PLANE_L},
  {"bike", "Bike", "Fahrrad", 0x005555, RESOURCE_ID_SYM_BIKE_S, RESOURCE_ID_SYM_BIKE_L},
  {"plant", "Plant", "Pflanze", 0x55AA00, RESOURCE_ID_SYM_PLANT_S, RESOURCE_ID_SYM_PLANT_L},
  {"food", "Food", "Essen", 0xAA5555, RESOURCE_ID_SYM_FOOD_S, RESOURCE_ID_SYM_FOOD_L},
  {"water", "Water", "Wasser", 0x0055FF, RESOURCE_ID_SYM_WATER_S, RESOURCE_ID_SYM_WATER_L},
  {"calendar", "Calendar", "Kalender", 0xAA0055, RESOURCE_ID_SYM_CALENDAR_S, RESOURCE_ID_SYM_CALENDAR_L},
  {"star", "Star", "Stern", 0xFFFF00, RESOURCE_ID_SYM_STAR_S, RESOURCE_ID_SYM_STAR_L},
  {"music", "Music", "Musik", 0x5500AA, RESOURCE_ID_SYM_MUSIC_S, RESOURCE_ID_SYM_MUSIC_L},
  {"important", "Important", "Wichtig", 0xFF0000, RESOURCE_ID_SYM_IMPORTANT_S, RESOURCE_ID_SYM_IMPORTANT_L},
  {"question", "Question", "Frage", 0xFF5500, RESOURCE_ID_SYM_QUESTION_S, RESOURCE_ID_SYM_QUESTION_L},
};

static const uint8_t ORDER[SYMBOL_COUNT - 1] = {27, 1, 2, 8, 30, 29, 11, 19, 10, 26, 16, 24, 5, 15, 20, 23, 9, 22, 12, 14, 21, 25, 3, 4, 6, 13, 28, 18, 7, 17};

static uint8_t s_flags = SYMBOL_SHOW_ALL;
static GBitmap *s_small[SYMBOL_COUNT];   // loaded on first use (a few hundred bytes each)
static GBitmap *s_large;                 // only one large symbol at a time
static int s_large_id;

void symbols_init(void) {
  s_flags = persist_exists(KEY_SYMBOL_FLAGS) ? (uint8_t)persist_read_int(KEY_SYMBOL_FLAGS) : SYMBOL_SHOW_ALL;
}

uint8_t symbols_flags(void) {
  return s_flags;
}

void symbols_set_flags(uint8_t flags) {
  s_flags = flags & SYMBOL_SHOW_ALL;
  persist_write_int(KEY_SYMBOL_FLAGS, s_flags);
}

bool symbols_show(uint8_t where) {
  return (s_flags & where) != 0;
}

const char *symbol_key(int symbol) {
  return symbol > 0 && symbol < SYMBOL_COUNT ? SYMBOLS[symbol - 1].key : "none";
}

const char *symbol_name(int symbol) {
  if (symbol <= 0 || symbol >= SYMBOL_COUNT) return TR("None", "Kein Symbol");
  return TR(SYMBOLS[symbol - 1].en, SYMBOLS[symbol - 1].de);
}

int symbol_at(int index) {
  return index > 0 && index < SYMBOL_COUNT ? ORDER[index - 1] : SYMBOL_NONE;
}

static GBitmap *prv_bitmap(int symbol, bool large) {
  const SymbolInfo *info = &SYMBOLS[symbol - 1];
  if (!large) {
    if (!s_small[symbol]) s_small[symbol] = gbitmap_create_with_resource(info->res_small);
    return s_small[symbol];
  }
  if (s_large_id != symbol) {
    if (s_large) gbitmap_destroy(s_large);
    s_large = gbitmap_create_with_resource(info->res_large);
    s_large_id = symbol;
  }
  return s_large;
}

// Same or similar color (at most two levels apart in total, 4 levels per channel):
// the tile would hardly stand out from this background
static bool prv_similar(GColor a, GColor b) {
  int d = 0;
  for (int shift = 0; shift <= 4; shift += 2) {
    int diff = ((a.argb >> shift) & 3) - ((b.argb >> shift) & 3);
    d += diff < 0 ? -diff : diff;
  }
  return d <= 2;
}

void symbol_draw(GContext *ctx, int symbol, GPoint origin, bool large, GColor bg, GColor frame_color) {
  if (symbol <= 0 || symbol >= SYMBOL_COUNT) return;
  GBitmap *bmp = prv_bitmap(symbol, large);
  if (!bmp) return;
  int size = large ? SYMBOL_LARGE : SYMBOL_SMALL;
  GRect r = GRect(origin.x, origin.y, size, size);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, r);
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  if (prv_similar(bg, GColorFromHEX(SYMBOLS[symbol - 1].rgb))) {
    graphics_context_set_stroke_color(ctx, frame_color);   // keep the tile visible on a similar background
    graphics_draw_round_rect(ctx, r, large ? 6 : 3);
  }
}
