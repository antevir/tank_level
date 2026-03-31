#pragma once

#include <Arduino.h> // for String
#include "pump_state.h"

void tcp_server_begin();
void tcp_server_loop();
void tcp_server_notify_state(PumpState state);
int  tcp_server_connected_count();

// Set a callback that handles commands from clients
typedef void (*CommandHandler)(const String &cmd);
void tcp_server_set_command_handler(CommandHandler handler);
