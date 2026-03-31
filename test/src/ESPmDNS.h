// ESPmDNS stub for native tests
#pragma once
#include "Arduino.h"

struct IPAddress {
    uint8_t b[4] = {};
    IPAddress() {}
    IPAddress(uint8_t a, uint8_t b_, uint8_t c, uint8_t d) { b[0]=a; b[1]=b_; b[2]=c; b[3]=d; }
    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
        return String(buf);
    }
    bool operator !=(const IPAddress& o) const { return memcmp(b, o.b, 4) != 0; }
    bool operator ==(const IPAddress& o) const { return memcmp(b, o.b, 4) == 0; }
};

struct MDNSResponder {
    bool begin(const char*) { return true; }
    int queryService(const char*, const char*) { return 0; }
    String hostname(int) { return ""; }
    IPAddress address(int) { return IPAddress(); }
};
static MDNSResponder MDNS;
