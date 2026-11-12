#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
AnalyserAudioProcessor::AnalyserAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ANALYSER", createLayout())
{
    trimParam     = apvts.getRawParameterValue (an::ParamIDs::trim);
    responseParam = apvts.getRawParameterValue (an::ParamIDs::response);
    for (int b = 0; b < an::kNumBands; ++b)
        soloParams[(size_t) b] = apvts.getRawParameterValue (an::ParamIDs::solo[b]);
    monitorParam = apvts.getRawParameterValue (an::ParamIDs::monitor);
}

AudioProcessorValueTreeState::ParameterLayout AnalyserAudioProcessor::createLayout()
{
    using P = AudioParameterFloat;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto dbText = [] (float v, int) { return (v > 0.0f ? "+" : "") + an::formatKnobValue (v) + " dB"; };

    params.push_back (std::make_unique<P> (ParameterID { an::ParamIDs::trim, 1 }, "Trim",
                                           NormalisableRange<float> (-30.0f, 30.0f, 0.1f), 0.0f,
                                           AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
    params.push_back (std::make_unique<P> (ParameterID { an::ParamIDs::response, 1 }, "Response",
                                           NormalisableRange<float> (1.0f, (float) an::BandMeter::kMaxWindowSeconds, 0.1f), 3.0f,
                                           AudioParameterFloatAttributes().withLabel ("s")
                                               .withStringFromValueFunction ([] (float v, int) { return an::formatKnobValue (v) + " s"; })));
    params.push_back (std::make_unique<P> (ParameterID { an::ParamIDs::range, 1 }, "Range",
                                           NormalisableRange<float> (3.0f, 24.0f, 0.1f), 12.0f,
                                           AudioParameterFloatAttributes().withLabel ("dB")
                                               .withStringFromValueFunction ([] (float v, int) { return "+/-" + an::formatKnobValue (v) + " dB"; })));

    for (int b = 0; b < an::kNumBands; ++b)
        params.push_back (std::make_unique<AudioParameterBool> (ParameterID { an::ParamIDs::solo[b], 1 },
                                                                String ("Solo ") + an::kBands[b].name, false));

    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { an::ParamIDs::monitor, 1 }, "Listen",
                                                              StringArray { "Stereo", "Mid", "Side" }, 0));

    return { params.begin(), params.end() };
}

bool AnalyserAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (in == AudioChannelSet::mono() || in == AudioChannelSet::stereo());
}

//==============================================================================
void AnalyserAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    shared.sampleRate.store (sampleRate);
    meter.prepare (sampleRate, getTotalNumInputChannels());
    work.setSize (2, std::max (1, samplesPerBlock));
    trimGain.reset (sampleRate, 0.05);
    trimGain.setCurrentAndTargetValue (Decibels::decibelsToGain (trimParam->load()));

    const dsp::ProcessSpec spec { sampleRate, (uint32) std::max (1, samplesPerBlock), 2 };
    const float maxCutoff = (float) sampleRate * 0.45f;
    bool anySolo = false;
    for (int b = 0; b < an::kNumBands; ++b)
    {
        const auto& band = an::kBands[b];
        soloUseHighPass[(size_t) b] = band.loHz > 0.0f;
        soloUseLowPass[(size_t) b]  = band.hiHz < maxCutoff || b < an::kNumBands - 1;
        soloHighPass[(size_t) b].setType (LR::Type::highpass);
        soloHighPass[(size_t) b].setCutoffFrequency (std::min (std::max (band.loHz, 10.0f), maxCutoff));
        soloHighPass[(size_t) b].prepare (spec);
        soloLowPass[(size_t) b].setType (LR::Type::lowpass);
        soloLowPass[(size_t) b].setCutoffFrequency (std::min (band.hiHz, maxCutoff));
        soloLowPass[(size_t) b].prepare (spec);

        const bool on = soloParams[(size_t) b]->load() > 0.5f;
        anySolo = anySolo || on;
        soloGain[(size_t) b].reset (sampleRate, 0.02);
        soloGain[(size_t) b].setCurrentAndTargetValue (on ? 1.0f : 0.0f);
    }
    dryGain.reset (sampleRate, 0.02);
    dryGain.setCurrentAndTargetValue (anySolo ? 0.0f : 1.0f);

    const int monitor = (int) monitorParam->load();
    for (auto* sv : { &stereoGain, &midGain, &sideGain })
        sv->reset (sampleRate, 0.02);
    stereoGain.setCurrentAndTargetValue (monitor == an::MonitorStereo ? 1.0f : 0.0f);
    midGain.setCurrentAndTargetValue (monitor == an::MonitorMid ? 1.0f : 0.0f);
    sideGain.setCurrentAndTargetValue (monitor == an::MonitorSide ? 1.0f : 0.0f);
}

void AnalyserAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = std::min (2, getTotalNumInputChannels());
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    if (shared.resetRequested.exchange (false))
        meter.reset();

    if (work.getNumSamples() < numSamples)
        work.setSize (2, numSamples, false, false, true);

    // Trimmed copy for analysis; the host's audio is left untouched.
    trimGain.setTargetValue (Decibels::decibelsToGain (trimParam->load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = trimGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            work.setSample (ch, i, buffer.getSample (ch, i) * g);
    }

    meter.setWindowSeconds (responseParam->load());
    meter.process (work.getArrayOfReadPointers(), numChannels, numSamples);

    const auto& r = meter.getReadings();
    for (int b = 0; b < an::kNumBands; ++b)
        shared.bandRmsDb[(size_t) b].store (r.bandRmsDb[(size_t) b]);
    shared.inputRmsDb.store (r.inputRmsDb);
    shared.inputPeakDb.store (r.inputPeakDb);

    // Mono tap for the spectrum curve
    int pos = shared.tapWritePos.load (std::memory_order_relaxed);
    const float monoScale = numChannels > 1 ? 0.5f : 1.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float m = work.getSample (0, i);
        if (numChannels > 1) m += work.getSample (1, i);
        shared.tap[(size_t) pos] = m * monoScale;
        pos = (pos + 1) & (Shared::kTapSize - 1);
    }
    shared.tapWritePos.store (pos, std::memory_order_release);

    // Band solo on the output (after the analysis, which always sees the full mix).
    // The band filters run all the time so switching solos is click-free.
    bool anySolo = false;
    for (int b = 0; b < an::kNumBands; ++b)
    {
        const bool on = soloParams[(size_t) b]->load() > 0.5f;
        anySolo = anySolo || on;
        soloGain[(size_t) b].setTargetValue (on ? 1.0f : 0.0f);
    }
    dryGain.setTargetValue (anySolo ? 0.0f : 1.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = dryGain.getNextValue();
        std::array<float, an::kNumBands> gains {};
        for (int b = 0; b < an::kNumBands; ++b)
            gains[(size_t) b] = soloGain[(size_t) b].getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            float y = dry * x;
            for (int b = 0; b < an::kNumBands; ++b)
            {
                float v = x;
                if (soloUseHighPass[(size_t) b]) v = soloHighPass[(size_t) b].processSample (ch, v);
                if (soloUseLowPass[(size_t) b])  v = soloLowPass[(size_t) b].processSample (ch, v);
                y += gains[(size_t) b] * v;
            }
            buffer.setSample (ch, i, y);
        }
    }

    for (int b = 0; b < an::kNumBands; ++b)
    {
        soloHighPass[(size_t) b].snapToZero();
        soloLowPass[(size_t) b].snapToZero();
    }

    // Mid / Side listening (stereo only): mid = (L+R)/2 and side = (L-R)/2, each on both speakers.
    const int monitor = (int) monitorParam->load();
    stereoGain.setTargetValue (monitor == an::MonitorStereo ? 1.0f : 0.0f);
    midGain.setTargetValue (monitor == an::MonitorMid ? 1.0f : 0.0f);
    sideGain.setTargetValue (monitor == an::MonitorSide ? 1.0f : 0.0f);

    if (numChannels == 2 && (monitor != an::MonitorStereo || stereoGain.isSmoothing()))
    {
        auto* outL = buffer.getWritePointer (0);
        auto* outR = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float gs = stereoGain.getNextValue(), gm = midGain.getNextValue(), gd = sideGain.getNextValue();
            const float mid = 0.5f * (outL[i] + outR[i]), side = 0.5f * (outL[i] - outR[i]);
            const float common = gm * mid + gd * side;
            outL[i] = gs * outL[i] + common;
            outR[i] = gs * outR[i] + common;
        }
    }
    else
    {
        stereoGain.skip (numSamples);
        midGain.skip (numSamples);
        sideGain.skip (numSamples);
    }
}

void AnalyserAudioProcessor::Shared::copyLatest (float* dest, int numSamples) const noexcept
{
    numSamples = std::min (numSamples, kTapSize);
    int pos = (tapWritePos.load (std::memory_order_acquire) - numSamples) & (kTapSize - 1);
    for (int i = 0; i < numSamples; ++i)
    {
        dest[i] = tap[(size_t) pos];
        pos = (pos + 1) & (kTapSize - 1);
    }
}

//==============================================================================
AudioProcessorEditor* AnalyserAudioProcessor::createEditor()
{
    return new AnalyserAudioProcessorEditor (*this);
}

void AnalyserAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorWidth", shared.editorWidth.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AnalyserAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = ValueTree::fromXml (*xml);
        if (state.hasType (apvts.state.getType()))
        {
            shared.editorWidth.store ((int) state.getProperty ("editorWidth", 0));
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnalyserAudioProcessor();
}
