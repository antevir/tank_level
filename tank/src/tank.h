#pragma once

#include <stdint.h>

void tank_init();
uint16_t tank_get_level(); // Returns level in per mille
String tank_get_stats_json();
String tank_get_last_24h_json();
bool tank_get_last_30days_file_and_offset(String &filename, int &data_offset);
void tank_handle();

// Health state
void tank_set_sd_ok(bool ok);
bool tank_is_sd_ok();
bool tank_is_sensor_ok();
String tank_get_health_json();
