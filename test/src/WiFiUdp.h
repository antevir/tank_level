// WiFiUdp stub for native tests
#pragma once
struct WiFiUDP {
    void beginPacket(const char*, int) {}
    void write(const uint8_t*, size_t) {}
    void endPacket() {}
};
