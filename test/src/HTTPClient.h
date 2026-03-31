// HTTPClient stub for native tests
#pragma once
#include "Arduino.h"

class HTTPClient {
public:
    void begin(const String&) {}
    void setTimeout(int) {}
    int GET() { return 200; }
    void end() {}
};
