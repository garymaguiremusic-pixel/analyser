#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace an
{
    //==========================================================================
    namespace ParamIDs
    {
        inline constexpr const char* trim     = "trim";      // input calibration trim, dB
        inline constexpr const char* response = "response";  // RMS integration window, seconds
        inline constexpr const char* range    = "range";     // display range above/below the target line, dB
        inline constexpr const char* monitor  = "monitor";   // listen: 0 = stereo, 1 = mid, 2 = side (output only)
    }

    //==========================================================================
    // Reference input level: the band targets below assume the mix peaks at
    // -10 dB (after the Trim knob), which for the reference material is -17.5 dB RMS.
    inline constexpr float kRefPeakDb = -10.0f;

    enum Monitor { MonitorStereo = 0, MonitorMid, MonitorSide };
    inline constexpr float kRefToleranceDb = 0.5f;   // input counts as calibrated within +/- this

    //==========================================================================
    struct BandSpec
    {
        const char* name;
        const char* rangeText;
        float loHz, hiHz;      // loHz = 0 means "everything below hiHz"
        float targetTopDb;     // louder end of the target window
        float targetBottomDb;  // quieter end of the target window

        float targetCentreDb() const noexcept   { return 0.5f * (targetTopDb + targetBottomDb); }
        float targetHalfWidthDb() const noexcept { return 0.5f * (targetTopDb - targetBottomDb); }
    };

    inline constexpr int kNumBands = 5;

    inline constexpr BandSpec kBands[kNumBands] =
    {
        { "Low",      "0 - 120 Hz",    0.0f,    120.0f,   -20.0f, -20.5f },
        { "Low Mid",  "120 - 480 Hz",  120.0f,  480.0f,   -28.0f, -29.5f },
        { "Mid",      "480 - 1.9k",    480.0f,  1920.0f,  -26.0f, -29.5f },
        { "High Mid", "1.9k - 7.7k",   1920.0f, 7680.0f,  -28.0f, -31.0f },
        { "High",     "7.7k - 20k",    7680.0f, 20000.0f, -33.0f, -35.0f },
    };

    namespace ParamIDs
    {
        // Band solo switches (audio output only; the analysis always measures the full mix)
        inline constexpr const char* solo[kNumBands] = { "solo1", "solo2", "solo3", "solo4", "solo5" };
    }

    //==========================================================================
    inline constexpr float kSilenceDb = -90.0f;   // below this a reading shows "--"

    enum class Status { Silent, TooLittle, Good, TooMuch };

    inline Status bandStatus (int band, float rmsDb) noexcept
    {
        if (rmsDb <= kSilenceDb)                 return Status::Silent;
        // compare the value as it is displayed (one decimal), so "-20.0" never shows red
        const float shown = std::round (rmsDb * 10.0f) / 10.0f;
        if (shown > kBands[band].targetTopDb)    return Status::TooMuch;
        if (shown < kBands[band].targetBottomDb) return Status::TooLittle;
        return Status::Good;
    }

    inline Status inputStatus (float measuredDb, float referenceDb) noexcept
    {
        if (measuredDb <= kSilenceDb)                       return Status::Silent;
        const float shown = std::round (measuredDb * 10.0f) / 10.0f;
        if (shown > referenceDb + kRefToleranceDb)          return Status::TooMuch;
        if (shown < referenceDb - kRefToleranceDb)          return Status::TooLittle;
        return Status::Good;
    }

    inline juce::String formatDb (float db, bool withSign = false)
    {
        if (db <= kSilenceDb)
            return "--";
        const float r = std::round (db * 10.0f) / 10.0f;
        return (withSign && r > 0.0f ? "+" : "") + juce::String (r, 1);
    }

    inline juce::String formatKnobValue (double v)
    {
        const double r = std::round (v * 10.0) / 10.0;
        return std::abs (r - std::round (r)) < 0.05 ? juce::String (juce::roundToInt (r)) : juce::String (r, 1);
    }

    inline juce::String formatHz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }
}
