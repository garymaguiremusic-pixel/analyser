#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "../Parameters.h"

//==============================================================================
/**
    Rotary knob with the same interaction as VIRUS DELAY:
      - vertical drag and scroll wheel move in whole steps (1.0);
        hold Shift for 0.1 steps
      - double-click resets to the default value
      - small knobs show their label underneath, which swaps to the value
        while hovering or dragging
      - tooltips show real units
    Drawing is delegated to CustomLookAndFeel::drawRotarySlider.
*/
class VirusKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };

    VirusKnob (Size, const juce::String& labelText);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }
    void setBipolar (bool b) noexcept           { bipolar = b; }
    bool isBipolar() const noexcept             { return bipolar; }

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (double)> describeValue;   // real units (tooltip and hover text)

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setValueWithGesture (double v);

    Size knobSize;
    juce::String label;
    double defaultValue = 0.0;
    float lastDragY = 0.0f;
    double dragValue = 0.0;          // un-snapped drag position
    bool dragShift = false;
    bool dragging = false, hovering = false, bipolar = false;
};

//==============================================================================
/** Connects a VirusKnob to a parameter with proper host gesture handling. */
class KnobAttachment
{
public:
    KnobAttachment (juce::RangedAudioParameter&, VirusKnob&);
    ~KnobAttachment();

private:
    VirusKnob& knob;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "TRIM      +2 dB". */
class LcdDisplay : public juce::Component
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void setValueColour (juce::Colour);
    void paint (juce::Graphics&) override;

private:
    juce::String nameText, valueText;
    juce::Colour valueColour { juce::Colours::white };
};

//==============================================================================
/** Horizontal input meter with the reference level marked (calibration). */
class InputMeter : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    InputMeter (const juce::String& name, float referenceDb);

    void setLevel (float db);
    void paint (juce::Graphics&) override;

private:
    juce::String name;
    float referenceDb, levelDb = -120.0f;
};

//==============================================================================
/** Turns the latest block of samples into a smoothed, log-spaced spectrum curve. */
class SpectrumCurve
{
public:
    static constexpr int kFftOrder = 13, kFftSize = 1 << kFftOrder;
    static constexpr int kNumPoints = 300;
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    static constexpr float kTiltDbPerOctave = 4.5f;   // so a typical mix looks roughly flat

    static float frequencyForPoint (int i) noexcept;

    /** samples: kFftSize mono samples, oldest first. */
    void process (const float* samples, double sampleRate);
    void clear() { valid = false; }

    bool isValid() const noexcept { return valid; }
    const std::array<float, kNumPoints>& getPointsDb() const noexcept { return smoothedDb; }

private:
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann, true };
    std::vector<float> fftData = std::vector<float> ((size_t) kFftSize * 2, 0.0f);
    std::array<float, kNumPoints> smoothedDb {};
    bool valid = false;
};

//==============================================================================
/**
    The main display: five band columns on a log frequency axis. The straight
    line through the middle is the target. Each band shows its RMS reading on
    top and a bar from the target line:
        green = inside the target window, red = too much, blue = too little.
    The spectrum curve is drawn relative to the band levels for context.
*/
class SpectrumDisplay : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    void setBandLevels (const std::array<float, an::kNumBands>& rmsDb);
    void setCurve (const SpectrumCurve&);
    void setRangeDb (float halfRangeDb);
    void setFrozen (bool);

    static juce::Colour colourFor (an::Status);

    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;

private:
    struct Geometry
    {
        juce::Rectangle<float> readouts, plot, gutter, freqLabels;
        float xForHz (float hz) const;
        float yForDb (float db, float range) const;
        float bandLeft (int b) const;
        float bandRight (int b) const;
    };
    Geometry geometry() const;

    void paintReadouts (juce::Graphics&, const Geometry&);
    void paintGrid (juce::Graphics&, const Geometry&);
    void paintBands (juce::Graphics&, const Geometry&);
    void paintCurve (juce::Graphics&, const Geometry&);
    void paintBandLabels (juce::Graphics&, const Geometry&);

    float deviationDb (int band) const;   // reading relative to the window centre

    std::array<float, an::kNumBands> bandDb {};
    std::array<float, SpectrumCurve::kNumPoints> curveDb {};
    bool hasCurve = false, frozen = false;
    float rangeDb = 12.0f;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
