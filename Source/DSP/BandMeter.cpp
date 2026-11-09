#include "BandMeter.h"

namespace an
{
    static float powerToDb (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-12 ? (float) (10.0 * std::log10 (meanSquare)) : -120.0f;
    }

    void BandMeter::prepare (double sampleRate, int numChannels)
    {
        channels = juce::jlimit (1, 2, numChannels);
        chunkLength = std::max (1, (int) std::round (sampleRate / kChunksPerSecond));

        const juce::dsp::ProcessSpec spec { sampleRate, 4096, (juce::uint32) channels };
        const float maxCutoff = (float) sampleRate * 0.45f;

        for (int b = 0; b < kNumBands; ++b)
        {
            useHighPass[b] = kBands[b].loHz > 0.0f;
            useLowPass[b]  = kBands[b].hiHz < maxCutoff || b < kNumBands - 1;

            highPass[b].setType (LR::Type::highpass);
            highPass[b].setCutoffFrequency (std::min (std::max (kBands[b].loHz, 10.0f), maxCutoff));
            highPass[b].prepare (spec);

            lowPass[b].setType (LR::Type::lowpass);
            lowPass[b].setCutoffFrequency (std::min (kBands[b].hiHz, maxCutoff));
            lowPass[b].prepare (spec);
        }

        ring.assign ((size_t) (kMaxWindowSeconds * kChunksPerSecond + 1), Chunk {});
        reset();
    }

    void BandMeter::reset()
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            highPass[b].reset();
            lowPass[b].reset();
        }

        chunkPos = 0;
        chunkBand.fill (0.0);
        chunkFull = 0.0;
        chunkPeak = 0.0f;
        ringPos = ringFilled = 0;

        readings = {};
        readings.bandRmsDb.fill (-120.0f);
    }

    void BandMeter::setWindowSeconds (float seconds) noexcept
    {
        windowChunks = juce::jlimit (1, kMaxWindowSeconds * kChunksPerSecond, (int) std::round (seconds * kChunksPerSecond));
    }

    void BandMeter::process (const float* const* in, int numChannels, int numSamples)
    {
        const int nch = std::min (numChannels, channels);
        if (nch <= 0 || ring.empty())
            return;

        const double chScale = 1.0 / nch;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < nch; ++ch)
            {
                const float x = in[ch][i];
                chunkFull += (double) x * x * chScale;
                chunkPeak = std::max (chunkPeak, std::abs (x));

                for (int b = 0; b < kNumBands; ++b)
                {
                    float y = x;
                    if (useHighPass[b]) y = highPass[b].processSample (ch, y);
                    if (useLowPass[b])  y = lowPass[b].processSample (ch, y);
                    chunkBand[(size_t) b] += (double) y * y * chScale;
                }
            }

            if (++chunkPos >= chunkLength)
                finishChunk();
        }

        for (int b = 0; b < kNumBands; ++b)
        {
            highPass[b].snapToZero();
            lowPass[b].snapToZero();
        }
    }

    void BandMeter::finishChunk()
    {
        auto& c = ring[(size_t) ringPos];
        const double inv = 1.0 / chunkLength;
        for (int b = 0; b < kNumBands; ++b)
            c.bandMs[(size_t) b] = (float) (chunkBand[(size_t) b] * inv);
        c.fullMs = (float) (chunkFull * inv);
        c.peak = chunkPeak;

        ringPos = (ringPos + 1) % (int) ring.size();
        ringFilled = std::min (ringFilled + 1, (int) ring.size());
        chunkPos = 0;
        chunkBand.fill (0.0);
        chunkFull = 0.0;
        chunkPeak = 0.0f;

        // Average the most recent chunks of the window.
        const int n = std::min (windowChunks, ringFilled);
        std::array<double, kNumBands> band {};
        double full = 0.0;
        float peak = 0.0f;
        int idx = ringPos;
        for (int k = 0; k < n; ++k)
        {
            idx = (idx == 0 ? (int) ring.size() : idx) - 1;
            const auto& r = ring[(size_t) idx];
            for (int b = 0; b < kNumBands; ++b)
                band[(size_t) b] += r.bandMs[(size_t) b];
            full += r.fullMs;
            peak = std::max (peak, r.peak);
        }

        for (int b = 0; b < kNumBands; ++b)
            readings.bandRmsDb[(size_t) b] = powerToDb (band[(size_t) b] / n);
        readings.inputRmsDb = powerToDb (full / n);
        readings.inputPeakDb = peak > 1.0e-6f ? juce::Decibels::gainToDecibels (peak) : -120.0f;
    }
}
