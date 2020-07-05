#pragma once

#include <Arduino.h>
#include <stdint.h>

void pump_init();
void pump_enable();
void pump_disable();
int pump_get_current_mA();
bool pump_is_on();
String pump_get_stats_json();
void pump_handle();
