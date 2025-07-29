#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WifiUdp.h>
#include "Log.h"

#define UDP_PORT 15010

static WiFiUDP udp;
static IPAddress multicastAddr = IPAddress(224,3,29,72);

void udp_server_init(void)
{
    udp.beginMulticast(WiFi.localIP(), multicastAddr, UDP_PORT);
}

void udp_server_send(const char *pName, int value)
{
    char buf[64];
    if (!udp.beginPacketMulticast(multicastAddr, UDP_PORT, WiFi.localIP())) {
        Log.error("Failed to start multicast packet!");
        return;
    }
    sprintf(buf, "%s:%d", pName, value);
    udp.write((uint8_t *)&buf[0], strlen(buf));
    udp.endPacket();
}
