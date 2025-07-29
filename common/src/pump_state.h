#pragma once

enum PumpState
{
    PumpDryRun = -2,
    PumpOff = -1,
    PumpIdle = 0,
    PumpRunning,
    PumpWarning
};
