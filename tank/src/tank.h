#pragma once

#include <stdint.h>

void tank_init();
uint16 tank_get_level(); // Returns level in per mille
String tank_get_stats_json();
String tank_get_last_24h_json();
bool tank_get_last_30days_file_and_offset(String &filename, int &data_offset);
void tank_handle();
