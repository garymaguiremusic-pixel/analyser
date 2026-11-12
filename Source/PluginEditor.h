#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/AnalyserComponents.h"

//==============================================================================
/** The whole user interface, laid out at its base size (780 x 470). The editor
    below scales it to the window size. */
class AnalyserPanel : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kBaseWidth = 780, kBaseHeight = 470;

    explicit AnalyserPanel (AnalyserAudioProcessor&);
    ~AnalyserPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pulls the latest readings from the processor (called by the timer). */
    void refresh();

private:
    void timerCallback() override { refresh(); }
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);

    AnalyserAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    VirusKnob trimKnob     { VirusKnob::Size::Large, "Trim" };
    VirusKnob responseKnob { VirusKnob::Size::Small, "Response" };
    VirusKnob rangeKnob    { VirusKnob::Size::Small, "Range" };
    juce::TextButton freezeButton { "Freeze" }, resetButton { "Reset" };
    std::array<juce::TextButton, an::kNumBands> soloButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    juce::String soloText;

    juce::TextButton midButton { "Mid" }, sideButton { "Side" };
    std::unique_ptr<juce::ParameterAttachment> monitorAttachment;
    void setMonitor (int mode);

    SpectrumDisplay spectrum;
    SpectrumCurve curve;
    std::vector<float> tapCopy = std::vector<float> ((size_t) SpectrumCurve::kFftSize, 0.0f);

    InputMeter peakMeter { "Input Peak", an::kRefPeakDb };
    LcdDisplay trimLcd, guideLcd;

    HelpButton helpButton;
    HelpOverlay helpOverlay;
    juce::TooltipWindow tooltipWindow { this, 500 };

    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;

    juce::Rectangle<float> spectrumPanel, calPanel, displayPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyserPanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio, 70 % .. 250 %); remembers its size. */
class AnalyserAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AnalyserAudioProcessorEditor (AnalyserAudioProcessor&);
    void resized() override;

    AnalyserPanel& getPanel() noexcept { return panel; }

private:
    AnalyserAudioProcessor& processor;
    AnalyserPanel panel;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyserAudioProcessorEditor)
};
