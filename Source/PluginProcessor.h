#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Parameters.h"
#include "DSP/BandMeter.h"

//==============================================================================
/**
    Mix analyser. The audio passes through untouched; the Trim knob only scales
    the copy that is measured.
*/
class AnalyserAudioProcessor : public juce::AudioProcessor
{
public:
    AnalyserAudioProcessor();
    ~AnalyserAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ANALYSER"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // Shared with the editor (written on the audio thread, read on the GUI thread)
    struct Shared
    {
        std::array<std::atomic<float>, an::kNumBands> bandRmsDb;
        std::atomic<float> inputRmsDb  { -120.0f };
        std::atomic<float> inputPeakDb { -120.0f };
        std::atomic<bool>  resetRequested { false };
        std::atomic<double> sampleRate { 44100.0 };
        std::atomic<int>   editorWidth { 0 };

        // Mono tap of the trimmed signal for the spectrum curve
        static constexpr int kTapSize = 16384;   // power of two
        std::array<float, kTapSize> tap {};
        std::atomic<int> tapWritePos { 0 };

        Shared() { for (auto& b : bandRmsDb) b.store (-120.0f); }

        /** Copies the most recent numSamples (<= kTapSize) samples, oldest first. */
        void copyLatest (float* dest, int numSamples) const noexcept;
    } shared;

private:
    an::BandMeter meter;
    juce::AudioBuffer<float> work;
    juce::SmoothedValue<float> trimGain;
    std::atomic<float>* trimParam = nullptr;
    std::atomic<float>* responseParam = nullptr;
    std::array<std::atomic<float>*, an::kNumBands> soloParams {};

    // Band solo: the untrimmed input split with the same crossovers as the analysis
    using LR = juce::dsp::LinkwitzRileyFilter<float>;
    std::array<LR, an::kNumBands> soloHighPass, soloLowPass;
    std::array<bool, an::kNumBands> soloUseHighPass {}, soloUseLowPass {};
    std::array<juce::SmoothedValue<float>, an::kNumBands> soloGain;
    juce::SmoothedValue<float> dryGain;

    // Mid / Side listening
    std::atomic<float>* monitorParam = nullptr;
    juce::SmoothedValue<float> stereoGain, midGain, sideGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyserAudioProcessor)
};
