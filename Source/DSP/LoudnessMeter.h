#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace an
{
    //==========================================================================
    /**
        Loudness (ITU-R BS.1770-4 / EBU R128, as used by Cubase's loudness meter)
        and RMS (as in Cubase's meters: plain RMS over an adjustable "Resolution"
        window, with an optional AES17 +3 dB offset).

        LUFS:
          - K-weighting: the BS.1770 pre-filter (high shelf) + RLB high-pass,
            coefficients recalculated for the sample rate
          - channel weights L = R = 1.0
          - Momentary  = 400 ms window, Short-Term = 3 s window, updated every 100 ms
          - Integrated = 400 ms blocks with 75 % overlap, absolute gate -70 LUFS,
            relative gate -10 LU
        RMS (per channel, unweighted):
          - mean square over the last N ms (N = Resolution, 1..1000), 1 ms granularity
          - AES17 adds +3.01 dB (a full-scale sine then reads 0 dB RMS)
    */
    class LoudnessMeter
    {
    public:
        struct Readings
        {
            float momentary = -120.0f, shortTerm = -120.0f, integrated = -120.0f;
            float rmsL = -120.0f, rmsR = -120.0f, rmsMax = -120.0f;
        };

        void prepare (double sampleRate, int numChannels);
        void reset();
        void setRmsResolutionMs (int ms) noexcept;
        void setAes17 (bool on) noexcept { aes17 = on; }

        void process (const float* const* channels, int numChannels, int numSamples);

        const Readings& getReadings() const noexcept { return readings; }

    private:
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            double z1 = 0, z2 = 0;
            double process (double x) noexcept
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
            void clear() noexcept { z1 = z2 = 0.0; }
        };

        void finishLoudnessBlock();
        void finishRmsChunk();
        float rmsDb (int ch) const;
        static float energyToLufs (double e) noexcept;

        int channels = 2;
        bool aes17 = false;

        // K-weighting, per channel: shelf then high-pass
        std::array<Biquad, 2> shelf, highPass;

        // 100 ms loudness sub-blocks (sum over channels of K-weighted mean squares)
        int blockLength = 4800, blockPos = 0;
        double blockSum = 0.0;
        std::array<double, 30> subBlocks {};   // last 3 s
        int subPos = 0, subFilled = 0;

        // Integrated: histogram of 400 ms block loudness (0.01 LU bins from -70 to +10 LUFS)
        static constexpr double kHistMin = -70.0, kHistStep = 0.01;
        static constexpr int kHistBins = 8000;
        std::vector<double> histEnergy;
        std::vector<int> histCount;

        // RMS: 1 ms chunks of mean square, per channel, up to 1000 ms
        int chunkLength = 48, chunkPos = 0, rmsWindowMs = 300;
        std::array<double, 2> chunkSum {};
        std::array<std::array<float, 1000>, 2> rmsRing {};
        int rmsPos = 0, rmsFilled = 0;
        float rmsMaxDb = -120.0f;

        Readings readings;
    };
}
