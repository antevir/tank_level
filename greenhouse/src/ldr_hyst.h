#pragma once

// ---------------------------------------------------------------------------
// LdrHyst — hysteresis-based dark/bright state machine
//
// Two paths exist in each direction:
//
//   While BRIGHT (is_dark == false):
//     - confirmed (LDR was > T+H) AND LDR drops below T  → immediate DARK
//     - unconfirmed                AND LDR drops below T-H → DARK (deep-dark reached)
//
//   While DARK (is_dark == true):
//     - confirmed (LDR was < T-H) AND LDR rises above T  → immediate BRIGHT
//     - unconfirmed                AND LDR rises above T+H → BRIGHT (full-day reached)
//
// Parameters:
//   threshold  — ADC value at which the state ideally flips
//   hysteresis — ADC band around threshold (separate per caller, passed in)
//
// Usage:
//   LdrHyst h;
//   if (h.update(compensated_adc, threshold, LDR_LAMP_HYSTERESIS)) {
//       // is_dark changed — act on h.is_dark
//   }
// ---------------------------------------------------------------------------
struct LdrHyst {
    bool is_dark      = false;  // Current dark/bright state
    bool confirmed    = true;   // LDR has reached the deep zone of the current state
                                // (init true = assume startup in confirmed daylight)

    // Feed a new (already lamp-compensated) ADC reading.
    // Returns true when is_dark changed so the caller can react.
    bool update(int compensated, int threshold, int hysteresis)
    {
        // Clamp confirmation boundaries to the reachable ADC range [0, 1023].
        // e.g. threshold=150, H=200 → dark_confirm=0 (never -50),
        //      threshold=900, H=200 → bright_confirm=1023 (never 1100).
        int dark_confirm   = (threshold - hysteresis < 0)    ? 0    : threshold - hysteresis;
        int bright_confirm = (threshold + hysteresis > 1023) ? 1023 : threshold + hysteresis;

        bool changed = false;

        if (!is_dark)
        {
            // Track confirmation of current BRIGHT state
            if (compensated >= bright_confirm)
                confirmed = true;

            // Two paths to DARK
            if ((confirmed  && compensated < threshold) ||
                (!confirmed && compensated <= dark_confirm))
            {
                is_dark   = true;
                confirmed = false;
                changed   = true;
            }
        }
        else
        {
            // Track confirmation of current DARK state
            if (compensated <= dark_confirm)
                confirmed = true;

            // Two paths to BRIGHT
            if ((confirmed  && compensated >= threshold) ||
                (!confirmed && compensated >= bright_confirm))
            {
                is_dark   = false;
                confirmed = false;
                changed   = true;
            }
        }

        return changed;
    }
};
