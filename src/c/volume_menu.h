#pragma once
// Volume menu of an alarm: Quiet, Low, Medium, High.
// The alarm's sound plays as a looping preview at the highlighted volume.
#include <pebble.h>

void volume_menu_show(int slot);
