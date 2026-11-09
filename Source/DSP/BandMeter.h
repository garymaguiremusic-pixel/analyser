#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "../Parameters.h"

namespace an
{
    //==========================================================================
    /**
        Splits the (trimmed) input into the five analysis bands with 4th-order
        Linkwitz-Riley filters and measures each band's RMS over a sliding window.

        The signal is cut into 10 ms chunks. Each chunk stores its mean square per
        band, the full-band mean square and the sample peak, in a ring buffer that
        holds up to kMaxWindowSeconds. The window average is recomputed after every
        chunk, so the readings are a true sliding-window RMS.

        Stereo readings are the average power of the channels, so a signal at
        -17.5 dB RMS on both channels reads -17.5 dB.
    */
    class BandMeter
    {
    public:
        static constexpr int kMaxWindowSeconds = 30;
        static constexpr int kChunksPerSecond  = 100;

        struct Readings
        {
            std::array<float, kNumBands> bandRmsDb {};
            float inputRmsDb = -120.0f;
            float inputPeakDb = -120.0f;
        };

        void prepare (double sampleRate, int numChannels);
        void reset();
        void setWindowSeconds (float seconds) noexcept;

        /** Feed (already trimmed) audio. */
        void process (const float* const* channels, int numChannels, int numSamples);

        const Readings& getReadings() const noexcept { return readings; }

    private:
        void finishChunk();

        struct Chunk
        {
            std::array<float, kNumBands> bandMs {};
            float fullMs = 0.0f;
            float peak = 0.0f;
        };

        using LR = juce::dsp::LinkwitzRileyFilter<float>;
        std::array<LR, kNumBands> highPass, lowPass;
        std::array<bool, kNumBands> useHighPass {}, useLowPass {};

        int channels = 2;
        int chunkLength = 480, chunkPos = 0;
        std::array<double, kNumBands> chunkBand {};
        double chunkFull = 0.0;
        float chunkPeak = 0.0f;

        std::vector<Chunk> ring;
        int ringPos = 0, ringFilled = 0;
        int windowChunks = 300;

        Readings readings;
    };
}
