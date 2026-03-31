// Stub for EEPROM on native
#pragma once

struct EEPROMClass {
    char data[512] = {};
    void begin(int size) { (void)size; }
    void commit() {}
    template<typename T> void get(int addr, T& val) { memcpy(&val, data + addr, sizeof(T)); }
    template<typename T> void put(int addr, const T& val) { memcpy(data + addr, &val, sizeof(T)); }
};
static EEPROMClass EEPROM;
