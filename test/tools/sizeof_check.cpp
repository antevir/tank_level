#include <cstring>
#include "greenhouse_config.h"
#include <cstdio>
int main() {
    printf("GreenhouseCfgData = %zu\n", sizeof(GreenhouseCfgData));
    printf("LightCfg = %zu\n", sizeof(LightCfg));
    printf("IrrigationCfg = %zu\n", sizeof(IrrigationCfg));
    printf("NexaCfg = %zu\n", sizeof(NexaCfg));
    printf("NexaPlugCfg = %zu\n", sizeof(NexaPlugCfg));
    printf("TimeSpanCfg = %zu\n", sizeof(TimeSpanCfg));
    printf("Budget remaining = %zu\n", 512 - sizeof(GreenhouseCfgData));
}
