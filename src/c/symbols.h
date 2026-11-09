#pragma once
// Alarm symbols: one symbol per alarm, a white picture on a tile in a fixed color.
// Small (18 px: list, menu header) and large (36 px: ringing screen, picker) as bitmaps.
// The numbers are persisted (Alarm.symbol): only append new symbols at the end.
#include <pebble.h>

#define SYMBOL_NONE 0
#define SYMBOL_COUNT 31          // including "none"
#define SYMBOL_SMALL 18
#define SYMBOL_LARGE 36

// Where symbols are shown (set on the phone); all on by default
#define SYMBOL_SHOW_LIST 0x01
#define SYMBOL_SHOW_MENU 0x02
#define SYMBOL_SHOW_RING 0x04
#define SYMBOL_SHOW_HA   0x08
#define SYMBOL_SHOW_ALL  0x0F

void symbols_init(void);
uint8_t symbols_flags(void);
void symbols_set_flags(uint8_t flags);
bool symbols_show(uint8_t where);

const char *symbol_key(int symbol);    // language neutral, e.g. "sun" ("none" for 0)
const char *symbol_name(int symbol);   // display name in the watch language

// Draws the symbol with its top left corner at origin. bg = color behind it: if it equals
// the tile color, a thin frame in frame_color keeps the tile visible.
void symbol_draw(GContext *ctx, int symbol, GPoint origin, bool large, GColor bg, GColor frame_color);
