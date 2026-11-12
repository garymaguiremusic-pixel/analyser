#include "AnalyserComponents.h"
#include "../CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
// VirusKnob
//==============================================================================
VirusKnob::VirusKnob (Size s, const String& labelText)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s), label (labelText)
{
    setRange (0.0, 100.0, 0.0);
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (labelText);
}

String VirusKnob::getTooltip()
{
    const auto v = getValue();
    return label.toUpperCase() + "  " + (describeValue ? describeValue (v) : an::formatKnobValue (v))
         + "\nDrag or scroll: 1.0 steps  |  Shift: 0.1 steps  |  Double-click: reset";
}

void VirusKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float pos = (float) valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();

    Rectangle<float> knobArea;
    if (knobSize == Size::Large)
    {
        knobArea = bounds;
    }
    else
    {
        auto labelArea = bounds.removeFromBottom (14.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        if ((hovering || dragging) && isEnabled())
        {
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (VirusColours::blue);
            g.drawFittedText (describeValue ? describeValue (getValue()) : an::formatKnobValue (getValue()),
                              labelArea.toNearestInt(), Justification::centred, 1, 0.7f);
        }
        else
        {
            CustomLookAndFeel::drawSmallLabel (g, label, labelArea);
        }
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
}

void VirusKnob::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void VirusKnob::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragging = true;
    lastDragY = e.position.y;
    dragValue = getValue();
    dragShift = e.mods.isShiftDown();
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void VirusKnob::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    // Incremental drag: whole steps normally, 0.1 steps with Shift. Pressing or
    // releasing Shift mid-drag continues from the current value.
    const bool shift = e.mods.isShiftDown();
    if (shift != dragShift)
    {
        dragShift = shift;
        dragValue = getValue();
    }

    const double pixels = (double) (lastDragY - e.position.y);
    lastDragY = e.position.y;

    dragValue += shift ? pixels * 0.05                                    // 0.1 every 2 pixels
                       : pixels * std::max (0.25, (getMaximum() - getMinimum()) / 200.0);
    dragValue = jlimit (getMinimum(), getMaximum(), dragValue);

    const double snapped = shift ? std::round (dragValue * 10.0) / 10.0 : std::round (dragValue);
    if (std::abs (snapped - getValue()) > 1.0e-9)
        setValue (snapped, sendNotificationSync);
}

void VirusKnob::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void VirusKnob::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        setValueWithGesture (defaultValue);
}

void VirusKnob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    // One notch = 1.0, or 0.1 with Shift.
    const bool shift = e.mods.isShiftDown();
    const double step = shift ? 0.1 : 1.0;
    const double target = getValue() + (dir > 0.0f ? step : -step);
    setValueWithGesture (shift ? std::round (target * 10.0) / 10.0 : std::round (target));
}

void VirusKnob::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void VirusKnob::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// KnobAttachment
//==============================================================================
KnobAttachment::KnobAttachment (RangedAudioParameter& p, VirusKnob& k)
    : knob (k),
      attachment (p, [this] (float v) { knob.setValue (v, dontSendNotification); knob.repaint(); })
{
    const auto range = p.getNormalisableRange();
    knob.setRange (range.start, range.end, 0.0);
    knob.setDefaultValue (p.convertFrom0to1 (p.getDefaultValue()));

    knob.onGestureBegin = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };
    knob.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) knob.getValue()); };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onGestureBegin = nullptr;
    knob.onGestureEnd = nullptr;
    knob.onValueChange = nullptr;
}

//==============================================================================
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::setValueColour (Colour c)
{
    if (c != valueColour)
    {
        valueColour = c;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, false);

    const auto font = CustomLookAndFeel::uiFont (11.0f, true);
    g.setFont (font);
    auto textArea = b.reduced (7.0f, 0.0f);
    const float valueWidth = GlyphArrangement::getStringWidth (font, valueText) + 4.0f;
    auto nameArea = textArea.withTrimmedRight (valueWidth).toNearestInt();

    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawFittedText (nameText, nameArea.translated (0, 1), Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea.translated (0.0f, 1.0f), Justification::centredRight);
    g.setColour (Colours::white);
    g.drawFittedText (nameText, nameArea, Justification::centredLeft, 1, 0.75f);
    g.setColour (valueColour);
    g.drawText (valueText, textArea, Justification::centredRight);
}

//==============================================================================
// InputMeter
//==============================================================================
InputMeter::InputMeter (const String& n, float ref) : name (n), referenceDb (ref) {}

void InputMeter::setLevel (float db)
{
    if (std::abs (db - levelDb) > 0.01f)
    {
        levelDb = db;
        repaint();
    }
}

void InputMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto top = b.removeFromTop (14.0f);
    const auto status = an::inputStatus (levelDb, referenceDb);
    const auto colour = SpectrumDisplay::colourFor (status);

    CustomLookAndFeel::drawSmallLabel (g, name, top, Justification::centredLeft);
    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    g.setColour (status == an::Status::Silent ? VirusColours::labelText : colour);
    g.drawText (an::formatDb (levelDb) + " dB", top, Justification::centredRight);

    constexpr float minDb = -40.0f, maxDb = 0.0f;
    auto bar = b.removeFromTop (11.0f).translated (0.0f, 2.0f);
    CustomLookAndFeel::drawInsetBox (g, bar, 2.0f);
    auto inner = bar.reduced (1.5f);
    auto xFor = [&] (float db) { return inner.getX() + inner.getWidth() * jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); };

    // tolerance zone around the reference
    const float z0 = xFor (referenceDb - an::kRefToleranceDb), z1 = xFor (referenceDb + an::kRefToleranceDb);
    g.setColour (VirusColours::green.withAlpha (0.25f));
    g.fillRect (Rectangle<float> (z0 - 1.0f, inner.getY(), z1 - z0 + 2.0f, inner.getHeight()));

    if (status != an::Status::Silent)
    {
        const float x = xFor (levelDb);
        g.setGradientFill (ColourGradient (colour.withAlpha (0.35f), inner.getX(), 0.0f, colour, x, 0.0f, false));
        g.fillRoundedRectangle (inner.withRight (std::max (inner.getX() + 1.0f, x)), 1.5f);
    }

    // reference marker
    const float rx = xFor (referenceDb);
    g.setColour (Colours::white);
    g.fillRect (Rectangle<float> (rx - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f));
}

//==============================================================================
// SpectrumCurve
//==============================================================================
float SpectrumCurve::frequencyForPoint (int i) noexcept
{
    return kMinHz * std::pow (kMaxHz / kMinHz, (float) i / (float) (kNumPoints - 1));
}

void SpectrumCurve::process (const float* samples, double sampleRate)
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (samples, samples + kFftSize, fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const int numBins = kFftSize / 2;
    const double binHz = sampleRate / kFftSize;
    const float norm = 2.0f / (float) kFftSize;

    for (int i = 0; i < kNumPoints; ++i)
    {
        // average power over 1/3 octave around the point
        const float f = frequencyForPoint (i);
        const double lo = f * std::pow (2.0, -1.0 / 6.0), hi = f * std::pow (2.0, 1.0 / 6.0);
        int k0 = (int) std::floor (lo / binHz), k1 = (int) std::ceil (hi / binHz);
        k0 = jlimit (1, numBins - 1, k0);
        k1 = jlimit (k0, numBins - 1, k1);

        double power = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            const double m = fftData[(size_t) k] * norm;
            power += m * m;
        }
        power /= (k1 - k0 + 1);

        const float db = (float) (10.0 * std::log10 (power + 1.0e-14))
                       + kTiltDbPerOctave * std::log2 (f / 1000.0f);

        smoothedDb[(size_t) i] = valid ? smoothedDb[(size_t) i] * 0.75f + db * 0.25f : db;
    }
    valid = true;
}

//==============================================================================
// SpectrumDisplay
//==============================================================================
Colour SpectrumDisplay::colourFor (an::Status s)
{
    switch (s)
    {
        case an::Status::TooMuch:   return VirusColours::red;
        case an::Status::TooLittle: return VirusColours::blue;
        case an::Status::Good:      return VirusColours::green;
        case an::Status::Silent:    break;
    }
    return Colour (0xff6a7178);
}

void SpectrumDisplay::setBandLevels (const std::array<float, an::kNumBands>& rmsDb)
{
    bandDb = rmsDb;
    repaint();
}

void SpectrumDisplay::setCurve (const SpectrumCurve& c)
{
    hasCurve = c.isValid();
    if (hasCurve)
        curveDb = c.getPointsDb();
    repaint();
}

void SpectrumDisplay::setRangeDb (float r)    { if (r != rangeDb) { rangeDb = r; repaint(); } }
void SpectrumDisplay::setFrozen (bool f)      { if (f != frozen)  { frozen = f;  repaint(); } }

void SpectrumDisplay::setSoloed (const std::array<bool, an::kNumBands>& s)
{
    if (s != soloed)
    {
        soloed = s;
        repaint();
    }
}

Rectangle<int> SpectrumDisplay::getSoloButtonArea (int b) const
{
    const auto geo = geometry();
    const float x1 = geo.bandRight (b);
    return Rectangle<float> (x1 - 1.5f - 3.0f - 15.0f, geo.readouts.getY() + 3.5f, 15.0f, 13.0f).toNearestInt();
}

String SpectrumDisplay::getTooltip()
{
    return "Each band's RMS against its target window.\n"
           "Green = on target, red = too much, blue = too little.\n"
           "The centre line is the target; bars show how far off a band is.";
}

float SpectrumDisplay::Geometry::xForHz (float hz) const
{
    const float t = std::log (jlimit (SpectrumCurve::kMinHz, SpectrumCurve::kMaxHz, hz) / SpectrumCurve::kMinHz)
                  / std::log (SpectrumCurve::kMaxHz / SpectrumCurve::kMinHz);
    return plot.getX() + plot.getWidth() * t;
}

float SpectrumDisplay::Geometry::yForDb (float db, float range) const
{
    const float half = plot.getHeight() * 0.5f - 6.0f;
    return plot.getCentreY() - jlimit (-1.0f, 1.0f, db / range) * half;
}

float SpectrumDisplay::Geometry::bandLeft (int b) const  { return xForHz (std::max (an::kBands[b].loHz, SpectrumCurve::kMinHz)); }
float SpectrumDisplay::Geometry::bandRight (int b) const { return xForHz (std::min (an::kBands[b].hiHz, SpectrumCurve::kMaxHz)); }

SpectrumDisplay::Geometry SpectrumDisplay::geometry() const
{
    Geometry geo;
    auto inner = getLocalBounds().toFloat().reduced (6.0f);
    geo.readouts = inner.removeFromTop (66.0f);
    inner.removeFromTop (4.0f);
    geo.freqLabels = inner.removeFromBottom (15.0f);
    geo.gutter = inner.removeFromLeft (30.0f);
    geo.plot = inner;
    geo.freqLabels = geo.freqLabels.withLeft (geo.plot.getX() - 16.0f).withRight (geo.plot.getRight() + 4.0f);
    geo.readouts = geo.readouts.withLeft (geo.gutter.getX());
    return geo;
}

float SpectrumDisplay::deviationDb (int b) const
{
    return bandDb[(size_t) b] - an::kBands[b].targetCentreDb();
}

void SpectrumDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, bounds, 4.0f);

    const auto geo = geometry();

    // plot background
    g.setGradientFill (ColourGradient (Colour (0xff0d1013), 0.0f, geo.plot.getY(), Colour (0xff15191d), 0.0f, geo.plot.getBottom(), false));
    g.fillRect (geo.plot);

    paintGrid (g, geo);
    paintBands (g, geo);

    // the target line straight through the middle
    const float cy = geo.plot.getCentreY();
    g.setColour (Colours::white.withAlpha (0.10f));
    g.fillRect (Rectangle<float> (geo.plot.getX(), cy - 2.5f, geo.plot.getWidth(), 5.0f));
    g.setColour (Colour (0xffeef2f5));
    g.fillRect (Rectangle<float> (geo.plot.getX(), cy - 0.75f, geo.plot.getWidth(), 1.5f));

    Path marker;
    marker.addTriangle (geo.plot.getX() - 6.0f, cy - 4.0f, geo.plot.getX() - 6.0f, cy + 4.0f, geo.plot.getX() - 1.0f, cy);
    g.fillPath (marker);

    paintCurve (g, geo);

    paintBandLabels (g, geo);

    g.setColour (VirusColours::bevelDark);
    g.drawRect (geo.plot, 1.0f);

    paintReadouts (g, geo);

    bool anySignal = false;
    for (auto db : bandDb)
        anySignal = anySignal || db > an::kSilenceDb;

    if (! anySignal)
    {
        g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
        g.setColour (VirusColours::labelText);
        g.drawText ("NO SIGNAL", geo.plot.withTrimmedTop (geo.plot.getHeight() * 0.25f).withHeight (20.0f), Justification::centred);
    }

    if (frozen)
    {
        auto badge = Rectangle<float> (geo.plot.getRight() - 64.0f, geo.plot.getY() + 6.0f, 58.0f, 16.0f);
        CustomLookAndFeel::drawLcd (g, badge, false);
        g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
        g.setColour (Colours::white);
        g.drawText ("FROZEN", badge, Justification::centred);
    }
}

void SpectrumDisplay::paintGrid (Graphics& g, const Geometry& geo)
{
    // dB lines relative to the target
    const float step = rangeDb <= 4.0f ? 1.0f : (rangeDb <= 8.0f ? 2.0f : (rangeDb <= 15.0f ? 3.0f : 6.0f));
    g.setFont (CustomLookAndFeel::uiFont (9.0f, true));
    for (float db = -std::floor (rangeDb / step) * step; db <= rangeDb + 0.01f; db += step)
    {
        const float y = geo.yForDb (db, rangeDb);
        if (std::abs (db) > 0.01f)
        {
            g.setColour (Colours::white.withAlpha (0.055f));
            g.drawHorizontalLine ((int) y, geo.plot.getX(), geo.plot.getRight());
        }
        g.setColour (std::abs (db) < 0.01f ? Colour (0xffeef2f5) : Colour (0xff7d858d));
        const String text = std::abs (db) < 0.01f ? "0" : (db > 0.0f ? "+" : "") + String (roundToInt (db));
        g.drawText (text, Rectangle<float> (geo.gutter.getX(), y - 6.0f, geo.gutter.getWidth() - 9.0f, 12.0f), Justification::centredRight);
    }

    // frequency lines + labels
    const std::pair<float, const char*> freqs[] =
    {
        { 20.0f, "20" }, { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
        { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" },
    };
    for (auto& [hz, text] : freqs)
    {
        const float x = geo.xForHz (hz);
        g.setColour (Colours::white.withAlpha (0.045f));
        g.drawVerticalLine ((int) x, geo.plot.getY(), geo.plot.getBottom());
        g.setColour (Colour (0xff7d858d));
        auto label = Rectangle<float> (x - 20.0f, geo.freqLabels.getY() + 2.0f, 40.0f, 12.0f);
        label = label.withX (jlimit (geo.freqLabels.getX(), geo.freqLabels.getRight() - 40.0f, label.getX()));
        g.drawText (text, label, Justification::centred);
    }
}

void SpectrumDisplay::paintBands (Graphics& g, const Geometry& geo)
{
    const float cy = geo.plot.getCentreY();

    for (int b = 0; b < an::kNumBands; ++b)
    {
        const auto& spec = an::kBands[b];
        const float x0 = geo.bandLeft (b), x1 = geo.bandRight (b);
        const auto column = Rectangle<float> (x0, geo.plot.getY(), x1 - x0, geo.plot.getHeight());
        const auto status = an::bandStatus (b, bandDb[(size_t) b]);
        const auto colour = colourFor (status);

        // faint column tint
        if (status != an::Status::Silent)
        {
            g.setColour (colour.withAlpha (0.05f));
            g.fillRect (column);
        }

        // target window around the centre line
        const float half = spec.targetHalfWidthDb();
        float zTop = geo.yForDb (half, rangeDb), zBottom = geo.yForDb (-half, rangeDb);
        if (zBottom - zTop < 3.0f) { zTop = cy - 1.5f; zBottom = cy + 1.5f; }
        g.setColour (VirusColours::green.withAlpha (0.13f));
        g.fillRect (Rectangle<float> (x0, zTop, x1 - x0, zBottom - zTop));
        g.setColour (VirusColours::green.withAlpha (0.35f));
        g.drawHorizontalLine ((int) zTop, x0 + 1.0f, x1 - 1.0f);
        g.drawHorizontalLine ((int) zBottom, x0 + 1.0f, x1 - 1.0f);

        // band separators (bevelled groove)
        if (b > 0)
        {
            g.setColour (Colours::black.withAlpha (0.8f));
            g.drawVerticalLine ((int) x0, geo.plot.getY(), geo.plot.getBottom());
            g.setColour (Colours::white.withAlpha (0.10f));
            g.drawVerticalLine ((int) x0 + 1, geo.plot.getY(), geo.plot.getBottom());
        }

        if (status == an::Status::Silent)
            continue;

        // level bar from the target line
        const float dev = deviationDb (b);
        const float yv = geo.yForDb (dev, rangeDb);
        const float inset = std::min (9.0f, (x1 - x0) * 0.12f);
        const auto barX = Rectangle<float> (x0 + inset, 0.0f, x1 - x0 - 2.0f * inset, 0.0f);
        const auto bar = Rectangle<float> (barX.getX(), std::min (yv, cy), barX.getWidth(), std::abs (yv - cy));

        g.setGradientFill (ColourGradient (colour.withAlpha (0.55f), 0.0f, yv, colour.withAlpha (0.10f), 0.0f, cy, false));
        g.fillRect (bar);

        g.setColour (colour.withAlpha (0.30f));
        g.fillRect (Rectangle<float> (barX.getX(), yv - 3.5f, barX.getWidth(), 7.0f));
        g.setColour (colour);
        g.fillRect (Rectangle<float> (barX.getX(), yv - 1.5f, barX.getWidth(), 3.0f));

    }
}

void SpectrumDisplay::paintBandLabels (Graphics& g, const Geometry& geo)
{
    for (int b = 0; b < an::kNumBands; ++b)
    {
        const auto& spec = an::kBands[b];
        const auto status = an::bandStatus (b, bandDb[(size_t) b]);
        if (status == an::Status::Silent)
            continue;

        const auto colour = colourFor (status);
        const float x0 = geo.bandLeft (b), x1 = geo.bandRight (b);
        const float dev = deviationDb (b);
        const float yv = geo.yForDb (dev, rangeDb);
        const float half = spec.targetHalfWidthDb();
        const float zTop = std::min (geo.yForDb (half, rangeDb), geo.plot.getCentreY() - 1.5f);
        const float zBottom = std::max (geo.yForDb (-half, rangeDb), geo.plot.getCentreY() + 1.5f);

        // deviation text next to the bar cap
        String text;
        if (status == an::Status::Good)
            text = "ON TARGET";
        else
        {
            const float off = status == an::Status::TooMuch ? bandDb[(size_t) b] - spec.targetTopDb
                                                            : bandDb[(size_t) b] - spec.targetBottomDb;
            text = an::formatDb (off, true) + " dB";
        }

        const bool above = status == an::Status::Good ? true : dev > 0.0f;
        float ty = above ? std::min (yv, zTop) - 20.0f : std::max (yv, zBottom) + 6.0f;
        ty = jlimit (geo.plot.getY() + 3.0f, geo.plot.getBottom() - 17.0f, ty);

        const auto font = CustomLookAndFeel::uiFont (10.0f, true);
        const float w = std::min (x1 - x0 - 6.0f, GlyphArrangement::getStringWidth (font, text) + 12.0f);
        const auto pill = Rectangle<float> (w, 15.0f).withCentre ({ 0.5f * (x0 + x1), ty + 7.0f });
        g.setColour (Colour (0xe0101316));
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (colour.withAlpha (0.6f));
        g.drawRoundedRectangle (pill, 3.0f, 1.0f);
        g.setFont (font);
        g.setColour (colour.brighter (0.3f));
        g.drawFittedText (text, pill.toNearestInt(), Justification::centred, 1, 0.75f);
    }
}

void SpectrumDisplay::paintCurve (Graphics& g, const Geometry& geo)
{
    if (! hasCurve)
        return;

    // The curve keeps its own shape but is pinned to the band bars: each band's
    // average curve level is moved onto that band's reading. Offsets are blended
    // between band centres (log frequency) so the curve stays continuous.
    constexpr float shapeScale = 0.45f;
    std::array<float, an::kNumBands> centreLog {}, devAt {}, meanAt {};
    bool ok = true;

    for (int b = 0; b < an::kNumBands; ++b)
    {
        const float lo = std::max (an::kBands[b].loHz, SpectrumCurve::kMinHz), hi = std::min (an::kBands[b].hiHz, SpectrumCurve::kMaxHz);
        centreLog[(size_t) b] = 0.5f * (std::log (lo) + std::log (hi));

        double sum = 0.0;
        int n = 0;
        for (int i = 0; i < SpectrumCurve::kNumPoints; ++i)
        {
            const float f = SpectrumCurve::frequencyForPoint (i);
            if (f >= lo && f < hi) { sum += curveDb[(size_t) i]; ++n; }
        }
        meanAt[(size_t) b] = n > 0 ? (float) (sum / n) : 0.0f;
        devAt[(size_t) b] = bandDb[(size_t) b] > an::kSilenceDb ? deviationDb (b) : -rangeDb;
        ok = ok && bandDb[(size_t) b] > an::kSilenceDb;
    }

    if (! ok)
        return;

    auto interp = [&] (const std::array<float, an::kNumBands>& v, float logF)
    {
        if (logF <= centreLog[0]) return v[0];
        for (int b = 1; b < an::kNumBands; ++b)
            if (logF <= centreLog[(size_t) b])
            {
                const float t = (logF - centreLog[(size_t) b - 1]) / (centreLog[(size_t) b] - centreLog[(size_t) b - 1]);
                return v[(size_t) b - 1] + t * (v[(size_t) b] - v[(size_t) b - 1]);
            }
        return v[an::kNumBands - 1];
    };

    Path curve;
    for (int i = 0; i < SpectrumCurve::kNumPoints; ++i)
    {
        const float f = SpectrumCurve::frequencyForPoint (i);
        const float logF = std::log (f);
        const float value = interp (devAt, logF) + shapeScale * (curveDb[(size_t) i] - interp (meanAt, logF));
        const Point<float> p (geo.xForHz (f), geo.yForDb (value, rangeDb));
        if (i == 0) curve.startNewSubPath (p);
        else        curve.lineTo (p);
    }

    for (int b = 0; b < an::kNumBands; ++b)
    {
        const auto colour = colourFor (an::bandStatus (b, bandDb[(size_t) b]));
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (Rectangle<float> (geo.bandLeft (b), geo.plot.getY(), geo.bandRight (b) - geo.bandLeft (b) + 1.0f, geo.plot.getHeight()).toNearestInt());
        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (curve, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colour.interpolatedWith (Colours::white, 0.5f).withAlpha (0.7f));
        g.strokePath (curve, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void SpectrumDisplay::paintReadouts (Graphics& g, const Geometry& geo)
{
    const auto area = geo.readouts;

    // row captions in the gutter
    const float rows[] = { 0.0f, 14.0f, 26.0f, 50.0f, 64.0f };
    const char* captions[] = { "BAND", "HZ", "RMS", "TGT" };
    g.setFont (CustomLookAndFeel::uiFont (8.0f, true));
    g.setColour (Colour (0xff6d747b));
    for (int r = 0; r < 4; ++r)
        g.drawText (captions[r], Rectangle<float> (area.getX(), area.getY() + rows[r], geo.gutter.getWidth() - 4.0f, rows[r + 1] - rows[r]),
                    Justification::centredRight);

    for (int b = 0; b < an::kNumBands; ++b)
    {
        const auto& spec = an::kBands[b];
        const float x0 = geo.bandLeft (b), x1 = geo.bandRight (b);
        auto cell = Rectangle<float> (x0, area.getY(), x1 - x0, area.getHeight()).reduced (1.5f, 0.0f);
        const auto status = an::bandStatus (b, bandDb[(size_t) b]);
        const auto colour = colourFor (status);
        const bool anySolo = std::find (soloed.begin(), soloed.end(), true) != soloed.end();

        // cell background with a coloured top edge
        g.setGradientFill (ColourGradient (Colour (0xff1a1e22), 0.0f, cell.getY(), Colour (0xff101316), 0.0f, cell.getBottom(), false));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (colour.withAlpha (status == an::Status::Silent ? 0.4f : 0.9f));
        g.fillRoundedRectangle (cell.withHeight (2.5f), 1.0f);
        g.setColour (soloed[(size_t) b] ? VirusColours::blue.withAlpha (0.8f) : VirusColours::bevelDark);
        g.drawRoundedRectangle (cell, 3.0f, 1.0f);

        auto row = [&] (int r) { return Rectangle<float> (cell.getX() + 2.0f, area.getY() + rows[r], cell.getWidth() - 4.0f, rows[r + 1] - rows[r]).toNearestInt(); };

        g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
        g.setColour (VirusColours::headerText);
        g.drawFittedText (String (spec.name).toUpperCase(), row (0).translated (0, 2), Justification::centred, 1, 0.7f);

        g.setFont (CustomLookAndFeel::uiFont (9.0f, false));
        g.setColour (Colour (0xff8d949b));
        g.drawFittedText (spec.rangeText, row (1), Justification::centred, 1, 0.7f);

        g.setFont (CustomLookAndFeel::uiFont (19.0f, true));
        g.setColour (status == an::Status::Silent ? Colour (0xff6a7178) : colour.brighter (0.15f));
        g.drawFittedText (an::formatDb (bandDb[(size_t) b]), row (2), Justification::centred, 1, 0.7f);

        g.setFont (CustomLookAndFeel::uiFont (9.0f, true));
        g.setColour (Colour (0xff8d949b));
        g.drawFittedText (String (spec.targetTopDb, 1) + " / " + String (spec.targetBottomDb, 1), row (3), Justification::centred, 1, 0.7f);

        if (anySolo && ! soloed[(size_t) b])
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (cell, 3.0f);
        }
    }
}

//==============================================================================
// LoudnessDisplay
//==============================================================================
LoudnessDisplay::LoudnessDisplay()
{
    setTooltip ("LUFS: ITU-R BS.1770-4 / EBU R128 (K-weighted; momentary 400 ms, short-term 3 s, integrated gated).\n"
                "RMS: unweighted, over the RMS Res window, +3 dB with AES17.\n"
                "Measured on the plugin's input before Trim, so it matches your DAW's meter. RESET clears Integrated and RMS Max.");
}

void LoudnessDisplay::setValues (const Values& v)
{
    if (v != values)
    {
        values = v;
        repaint();
    }
}

void LoudnessDisplay::paint (Graphics& g)
{
    struct Cell { const char* caption; float value; const char* unit; bool lufs; };
    const Cell cells[] =
    {
        { "Momentary",  values.momentary,  "LUFS", true },
        { "Short-Term", values.shortTerm,  "LUFS", true },
        { "Integrated", values.integrated, "LUFS", true },
        { "RMS L",      values.rmsL,       "dB",   false },
        { "RMS R",      values.rmsR,       "dB",   false },
        { "RMS Max",    values.rmsMax,     "dB",   false },
    };

    const auto b = getLocalBounds().toFloat();
    const float gap = 6.0f, w = (b.getWidth() - gap * 5.0f) / 6.0f;

    for (int i = 0; i < 6; ++i)
    {
        auto cell = Rectangle<float> (b.getX() + i * (w + gap), b.getY(), w, b.getHeight());
        CustomLookAndFeel::drawInsetBox (g, cell, 3.0f);

        auto inner = cell.reduced (6.0f, 3.0f);
        CustomLookAndFeel::drawSmallLabel (g, cells[i].caption, inner.removeFromTop (13.0f), Justification::centred);

        const bool silent = cells[i].value <= (cells[i].lufs ? -70.0f : an::kSilenceDb);
        const auto valueArea = inner.removeFromTop (22.0f);
        g.setFont (CustomLookAndFeel::uiFont (18.0f, true));
        g.setColour (silent ? Colour (0xff6a7178) : (i == 2 ? VirusColours::blue : Colour (0xffeef2f5)));
        g.drawText (silent ? String ("--") : an::formatDb (cells[i].value), valueArea, Justification::centred);

        g.setFont (CustomLookAndFeel::uiFont (8.5f, true));
        g.setColour (Colour (0xff7d858d));
        g.drawText (cells[i].unit, inner, Justification::centred);
    }
}

//==============================================================================
// Help
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (VirusColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (60.0f, 50.0f);
    CustomLookAndFeel::drawPanel (g, box, "Mix Analyser - Quick Help");

    const char* lines[] =
    {
        "1.  Play your mix and turn TRIM until INPUT PEAK reads -10 dB and turns green.",
        "     The LCD below it tells you how far to move TRIM. Only the analysis is trimmed;",
        "     the audio passes through unchanged.",
        "3.  Play the loudest section. Each band shows its RMS against its target window:",
        "     GREEN = on target,  RED = too much energy,  BLUE = too little.",
        "4.  The bars grow up (too much) or down (too little) from the target line in the middle.",
        "",
        "RESPONSE: band RMS window in seconds.  RANGE: dB shown above/below the target line.",
        "LOUDNESS: LUFS as Cubase (EBU R128). RMS RES + AES17: set these like Cubase's Meter Settings.",
        "FREEZE holds the readings.  RESET clears the RMS history.",
        "S (on each band) solos that band so you can hear it; solo several to combine them.",
        "MID / SIDE (top) lets you hear the mid or side signal on its own; works with the solos.",
        "     Solo and Mid/Side change only what you hear - the meters always measure the full mix.",
        "Knobs: drag or scroll = 1.0 steps, Shift = 0.1 steps, double-click = default.",
        "Drag the bottom-right corner to resize the window.",
    };

    g.setFont (CustomLookAndFeel::uiFont (12.0f, false));
    g.setColour (Colour (0xffe3e6e8));
    float y = box.getY() + 32.0f;
    for (auto* line : lines)
    {
        g.drawText (line, Rectangle<float> (box.getX() + 18.0f, y, box.getWidth() - 36.0f, 18.0f), Justification::centredLeft);
        y += 19.0f;
    }

    g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
    g.setColour (VirusColours::labelText);
    g.drawText ("CLICK ANYWHERE TO CLOSE", box.removeFromBottom (24.0f), Justification::centred);
}
