#include "LoudnessMeter.h"

namespace an
{
    void LoudnessMeter::prepare (double fs, int numChannels)
    {
        channels = juce::jlimit (1, 2, numChannels);

        // BS.1770 K-weighting, coefficients for any sample rate (same derivation as libebur128)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            for (auto& f : shelf)
            {
                f.b0 = (Vh + Vb * K / Q + K * K) / a0;
                f.b1 = 2.0 * (K * K - Vh) / a0;
                f.b2 = (Vh - Vb * K / Q + K * K) / a0;
                f.a1 = 2.0 * (K * K - 1.0) / a0;
                f.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            for (auto& f : highPass)
            {
                f.b0 = 1.0; f.b1 = -2.0; f.b2 = 1.0;
                f.a1 = 2.0 * (K * K - 1.0) / a0;
                f.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }

        blockLength = std::max (1, (int) std::round (fs * 0.1));
        chunkLength = std::max (1, (int) std::round (fs * 0.001));
        histEnergy.assign (kHistBins, 0.0);
        histCount.assign (kHistBins, 0);
        reset();
    }

    void LoudnessMeter::reset()
    {
        for (auto& f : shelf)    f.clear();
        for (auto& f : highPass) f.clear();
        blockPos = 0;
        blockSum = 0.0;
        subBlocks.fill (0.0);
        subPos = subFilled = 0;
        std::fill (histEnergy.begin(), histEnergy.end(), 0.0);
        std::fill (histCount.begin(), histCount.end(), 0);

        chunkPos = 0;
        chunkSum.fill (0.0);
        for (auto& r : rmsRing) r.fill (0.0f);
        rmsPos = rmsFilled = 0;
        rmsMaxDb = -120.0f;
        rmsHoldDb = { -120.0f, -120.0f };
        readings = {};
    }

    void LoudnessMeter::resetHolds()
    {
        std::fill (histEnergy.begin(), histEnergy.end(), 0.0);
        std::fill (histCount.begin(), histCount.end(), 0);
        rmsMaxDb = -120.0f;
        rmsHoldDb = { -120.0f, -120.0f };
        readings.integrated = readings.rmsL = readings.rmsR = readings.rmsMax = -120.0f;
    }

    void LoudnessMeter::setRmsResolutionMs (int ms) noexcept
    {
        rmsWindowMs = juce::jlimit (1, 1000, ms);
    }

    float LoudnessMeter::energyToLufs (double e) noexcept
    {
        return e > 1.0e-15 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -120.0f;
    }

    void LoudnessMeter::process (const float* const* in, int numChannels, int numSamples)
    {
        const int nch = std::min (numChannels, channels);
        if (nch <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < nch; ++ch)
            {
                const double x = in[ch][i];
                const double k = highPass[(size_t) ch].process (shelf[(size_t) ch].process (x));
                blockSum += k * k;              // channel weight 1.0 (L, R)
                chunkSum[(size_t) ch] += x * x;
            }

            if (++chunkPos >= chunkLength)
                finishRmsChunk();
            if (++blockPos >= blockLength)
                finishLoudnessBlock();
        }

    }

    void LoudnessMeter::finishRmsChunk()
    {
        for (int ch = 0; ch < 2; ++ch)
            rmsRing[(size_t) ch][(size_t) rmsPos] = (float) (chunkSum[(size_t) ch] / chunkLength);
        rmsPos = (rmsPos + 1) % 1000;
        rmsFilled = std::min (rmsFilled + 1, 1000);
        chunkPos = 0;
        chunkSum.fill (0.0);

        const float nowL = rmsDb (0);
        const float nowR = channels > 1 ? rmsDb (1) : nowL;
        if (rmsFilled >= rmsWindowMs)   // hold only full-window readings
        {
            rmsHoldDb[0] = std::max (rmsHoldDb[0], nowL);
            rmsHoldDb[1] = std::max (rmsHoldDb[1], nowR);
        }
        readings.rmsL = rmsHoldDb[0];
        readings.rmsR = rmsHoldDb[1];
        rmsMaxDb = std::max (rmsHoldDb[0], rmsHoldDb[1]);
        readings.rmsMax = rmsMaxDb;
    }

    float LoudnessMeter::rmsDb (int ch) const
    {
        const int n = std::min (rmsWindowMs, rmsFilled);
        if (n <= 0)
            return -120.0f;

        double sum = 0.0;
        int idx = rmsPos;
        for (int k = 0; k < n; ++k)
        {
            idx = (idx == 0 ? 1000 : idx) - 1;
            sum += rmsRing[(size_t) ch][(size_t) idx];
        }
        const double ms = sum / n;
        if (ms <= 1.0e-12)
            return -120.0f;
        return (float) (10.0 * std::log10 (ms)) + (aes17 ? 3.0103f : 0.0f);
    }

    void LoudnessMeter::finishLoudnessBlock()
    {
        subBlocks[(size_t) subPos] = blockSum / blockLength;
        subPos = (subPos + 1) % (int) subBlocks.size();
        subFilled = std::min (subFilled + 1, (int) subBlocks.size());
        blockPos = 0;
        blockSum = 0.0;

        auto meanOfLast = [this] (int n)
        {
            double s = 0.0;
            int idx = subPos;
            for (int k = 0; k < n; ++k)
            {
                idx = (idx == 0 ? (int) subBlocks.size() : idx) - 1;
                s += subBlocks[(size_t) idx];
            }
            return s / n;
        };

        // Momentary (400 ms) and short-term (3 s) need a full window
        if (subFilled >= 4)
        {
            const double e400 = meanOfLast (4);
            readings.momentary = energyToLufs (e400);

            // Integrated: every 400 ms block (75 % overlap) goes into the gated histogram
            const double blockLufs = -0.691 + 10.0 * std::log10 (std::max (e400, 1.0e-20));
            if (blockLufs > kHistMin)
            {
                const int bin = juce::jlimit (0, kHistBins - 1, (int) ((blockLufs - kHistMin) / kHistStep));
                histEnergy[(size_t) bin] += e400;
                histCount[(size_t) bin] += 1;
            }

            double sum = 0.0;
            int count = 0;
            for (int b = 0; b < kHistBins; ++b) { sum += histEnergy[(size_t) b]; count += histCount[(size_t) b]; }

            if (count > 0)
            {
                const double relativeGate = -0.691 + 10.0 * std::log10 (sum / count) - 10.0;
                double gSum = 0.0;
                int gCount = 0;
                for (int b = 0; b < kHistBins; ++b)
                {
                    // bin b holds blocks from (kHistMin + b*step) up to the next bin
                    if (kHistMin + (b + 1) * kHistStep > relativeGate && histCount[(size_t) b] > 0)
                    {
                        gSum += histEnergy[(size_t) b];
                        gCount += histCount[(size_t) b];
                    }
                }
                readings.integrated = gCount > 0 ? energyToLufs (gSum / gCount) : -120.0f;
            }
        }

        if (subFilled >= (int) subBlocks.size())
            readings.shortTerm = energyToLufs (meanOfLast ((int) subBlocks.size()));
    }
}
