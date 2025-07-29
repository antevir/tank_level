#pragma once

#include "pump.h"
#include "tank.h"

#define CONSUMPTION_TMO_MIN 10

class Consumption
{
    int tot_consumption = 0;
    int timeout = 0;
    uint16_t start_level = 0;

public:
    Consumption(){};

    bool is_consuming()
    {
        return timeout > 0;
    }

    int get_consumption(bool clear = true)
    {
        int value = tot_consumption;
        if (clear)
            tot_consumption = 0;
        if (is_consuming())
        {
            uint16_t cur_level = tank_get_level();
            int diff = start_level - cur_level;
            if (diff < 0)
                diff = 0;
            value += diff;
            if (clear)
                start_level = cur_level;
        }
        return value;
    }

    void tick()
    {
        if (pump_is_on())
        {
            if (!is_consuming())
            {
                start_level = tank_get_level();
            }
            timeout = CONSUMPTION_TMO_MIN;
        }
        else if (is_consuming())
        {
            if (--timeout == 0)
            {
                int diff = start_level - tank_get_level();
                if (diff < 0)
                    diff = 0;
                tot_consumption += diff;
            }
        }
    }
};
