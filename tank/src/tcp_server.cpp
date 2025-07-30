#include "tcp_server.h"
#include "common.h"

#define MAX_CLIENTS 3
#define CLIENT_TIMEOUT_MS 40000

WiFiServer server(TCP_SERVER_PORT);
WiFiClient clients[MAX_CLIENTS];
unsigned long last_activity[MAX_CLIENTS] = {0};

static PumpState current_state = PumpIdle;
static CommandHandler command_handler = nullptr;

void tcp_server_begin()
{
    server.begin();
    server.setNoDelay(true);
}

void tcp_server_loop()
{
    unsigned long now = millis();

    // Accept new clients
    WiFiClient newClient = server.accept();
    if (newClient) {
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (!clients[i] || !clients[i].connected()) {
                clients[i].stop();
                clients[i] = newClient;
                clients[i].setNoDelay(true);
                last_activity[i] = now;
                String msg = "PUMP_STATE:" + String((int)current_state) + "\n";
                clients[i].print(msg);  // Send initial state
                break;
            }
        }
    }

    // Handle existing clients
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) continue;

        if (!clients[i].connected()) {
            clients[i].stop();
            continue;
        }

        if (clients[i].available()) {
            String line = clients[i].readStringUntil('\n');
            line.trim();
            last_activity[i] = now;

            if (line.equalsIgnoreCase("PING")) {
                clients[i].println("PONG");
            } else if (!line.isEmpty() && command_handler) {
                command_handler(line);
            }
        }

        // Timeout handling
        if (now - last_activity[i] > CLIENT_TIMEOUT_MS) {
            clients[i].println("TIMEOUT");
            clients[i].stop();
        }
    }
}

void tcp_server_notify_state(PumpState state)
{
    current_state = state;
    String msg = "PUMP_STATE:" + String((int)state) + "\n";
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i].connected()) {
            clients[i].print(msg);
        }
    }
}

void tcp_server_set_command_handler(CommandHandler handler)
{
    command_handler = handler;
}
