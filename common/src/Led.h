#pragma once

#include <arduino.h>
#include <stdint.h>

#define LED_BLINK_FAST_TICK 5
#define LED_BLINK_SLOW_TICK 20

enum LedColor
{
    LedOff = 0,
    LedGreen,
    LedRed,
    LedOrange
};

enum LedBlink
{
    LedBlinkOff = 0,
    LedBlinkSlow,
    LedBlinkFast
};

class Led
{
private:
    LedColor color;
    LedBlink blink;
    uint32_t last_blink_tick;
    bool blink_state;
    int green_pin;
    int red_pin;
public:
    inline Led() { }

    inline void begin(int green_pin, int red_pin)
    {
        this->green_pin = green_pin;
        this->red_pin = red_pin;
        pinMode(green_pin, OUTPUT);
        pinMode(red_pin, OUTPUT);
        set(LedOff, LedBlinkOff);
    }

    inline void set(LedColor color, LedBlink blink)
    {
        this->blink_state = true;
        this->color = color;
        this->blink = blink;
        this->last_blink_tick = 0;
    }

    inline void update(uint32_t ticks)
    {
        LedColor led_color = this->color;
        if (this->blink != LedBlinkOff)
        {
            uint32_t blink_tick = (this->blink == LedBlinkFast) ?
                                   LED_BLINK_FAST_TICK : LED_BLINK_SLOW_TICK;
            if (ticks - this->last_blink_tick > blink_tick)
            {
                this->last_blink_tick = ticks;
                this->blink_state = !this->blink_state;
            }
            if (!this->blink_state)
            {
                led_color = LedOff;
            }
        }
        bool red = led_color == LedRed;
        bool green = led_color == LedGreen;
        if (led_color == LedOrange)
        {
            red = true;
            green = true;
        }

        digitalWrite(this->red_pin, red ? HIGH : LOW);
        digitalWrite(this->green_pin, green ? HIGH : LOW);
    }
};
