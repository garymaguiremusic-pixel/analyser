#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr int kWidth = AnalyserPanel::kBaseWidth, kHeight = AnalyserPanel::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 438;

    String signedDb (double v) { return (v > 0.0 ? "+" : "") + an::formatKnobValue (v) + " dB"; }
}

//==============================================================================
AnalyserPanel::AnalyserPanel (AnalyserAudioProcessor& p) : processor (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    setSize (kWidth, kHeight);
    createBackground();

    spectrumPanel = { 8.0f, 42.0f, 580.0f, 388.0f };
    calPanel      = { 596.0f, 42.0f, 176.0f, 262.0f };
    displayPanel  = { 596.0f, 310.0f, 176.0f, 120.0f };

    auto& apvts = processor.apvts;
    const std::pair<VirusKnob*, const char*> knobs[] =
    {
        { &trimKnob, an::ParamIDs::trim }, { &responseKnob, an::ParamIDs::response }, { &rangeKnob, an::ParamIDs::range },
    };
    for (auto& [knob, id] : knobs)
    {
        addAndMakeVisible (*knob);
        knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (id), *knob));
    }

    trimKnob.setBipolar (true);
    trimKnob.describeValue     = [] (double v) { return signedDb (v); };
    responseKnob.describeValue = [] (double v) { return an::formatKnobValue (v) + " s"; };
    rangeKnob.describeValue    = [] (double v) { return "+/-" + an::formatKnobValue (v) + " dB"; };

    freezeButton.setClickingTogglesState (true);
    freezeButton.setTooltip ("Hold the current readings");
    freezeButton.onClick = [this] { spectrum.setFrozen (freezeButton.getToggleState()); };
    resetButton.setTooltip ("Clear the RMS history and start measuring again");
    resetButton.onClick = [this]
    {
        processor.shared.resetRequested.store (true);
        curve.clear();
    };
    addAndMakeVisible (freezeButton);
    addAndMakeVisible (resetButton);

    addAndMakeVisible (spectrum);

    // Mid / Side listen buttons (one at a time; both off = normal stereo)
    midButton.setTooltip ("Listen to the Mid (L+R) only. Click again for normal stereo. The meters still measure the full mix.");
    sideButton.setTooltip ("Listen to the Side (L-R) only. Click again for normal stereo. The meters still measure the full mix.");
    midButton.onClick  = [this] { setMonitor (midButton.getToggleState()  ? an::MonitorStereo : an::MonitorMid); };
    sideButton.onClick = [this] { setMonitor (sideButton.getToggleState() ? an::MonitorStereo : an::MonitorSide); };
    addAndMakeVisible (midButton);
    addAndMakeVisible (sideButton);
    monitorAttachment = std::make_unique<ParameterAttachment> (*processor.apvts.getParameter (an::ParamIDs::monitor),
        [this] (float v)
        {
            const int mode = roundToInt (v);
            midButton.setToggleState (mode == an::MonitorMid, dontSendNotification);
            sideButton.setToggleState (mode == an::MonitorSide, dontSendNotification);
        });
    monitorAttachment->sendInitialUpdate();

    for (int b = 0; b < an::kNumBands; ++b)
    {
        auto& button = soloButtons[(size_t) b];
        button.setButtonText ("S");
        button.setClickingTogglesState (true);
        button.setTooltip (String ("Solo ") + an::kBands[b].name + " (" + an::kBands[b].rangeText
                           + ") - listen to this band only. The meters still measure the full mix.");
        addAndMakeVisible (button);
        buttonAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
            processor.apvts, an::ParamIDs::solo[b], button));
    }
    addAndMakeVisible (peakMeter);
    addAndMakeVisible (trimLcd);
    addAndMakeVisible (guideLcd);
    peakMeter.setTooltip ("Highest sample peak of the trimmed input over the Response window. Set TRIM so this reads -10 dB.");

    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    helpButton.setTooltip ("Quick help");
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    resized();
    refresh();
    startTimerHz (30);
}

void AnalyserPanel::setMonitor (int mode)
{
    monitorAttachment->setValueAsCompleteGesture ((float) mode);
}

AnalyserPanel::~AnalyserPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AnalyserPanel::resized()
{
    midButton.setBounds (452, 6, 64, 22);
    sideButton.setBounds (522, 6, 64, 22);
    freezeButton.setBounds (616, 6, 72, 22);
    resetButton.setBounds (696, 6, 72, 22);

    spectrum.setBounds (16, 68, 564, 354);
    for (int b = 0; b < an::kNumBands; ++b)
        soloButtons[(size_t) b].setBounds (spectrum.getSoloButtonArea (b) + spectrum.getPosition());

    trimKnob.setBounds (636, 82, 96, 96);
    trimLcd.setBounds (606, 186, 156, 20);
    peakMeter.setBounds (606, 220, 156, 28);
    guideLcd.setBounds (606, 266, 156, 20);

    responseKnob.setBounds (612, 334, 64, 80);
    rangeKnob.setBounds (692, 334, 64, 80);

    helpButton.setBounds (kWidth - 34, kBottomBarY + 5, 22, 22);
    helpOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void AnalyserPanel::refresh()
{
    auto& s = processor.shared;
    const bool frozen = freezeButton.getToggleState();

    if (! frozen)
    {
        std::array<float, an::kNumBands> bands {};
        for (int b = 0; b < an::kNumBands; ++b)
            bands[(size_t) b] = s.bandRmsDb[(size_t) b].load();
        spectrum.setBandLevels (bands);

        s.copyLatest (tapCopy.data(), SpectrumCurve::kFftSize);
        curve.process (tapCopy.data(), s.sampleRate.load());
        spectrum.setCurve (curve);

        const float peak = s.inputPeakDb.load();
        peakMeter.setLevel (peak);

        const auto status = an::inputStatus (peak, an::kRefPeakDb);
        if (status == an::Status::Silent)
        {
            guideLcd.setContent ("INPUT", "NO SIGNAL");
            guideLcd.setValueColour (Colours::white);
        }
        else if (status == an::Status::Good)
        {
            guideLcd.setContent ("INPUT", "CALIBRATED");
            guideLcd.setValueColour (Colour (0xffb8ffcc));
        }
        else
        {
            guideLcd.setContent ("ADJUST TRIM", an::formatDb (an::kRefPeakDb - peak, true) + " dB");
            guideLcd.setValueColour (Colours::white);
        }
    }

    trimLcd.setContent ("TRIM", signedDb (trimKnob.getValue()));

    std::array<bool, an::kNumBands> soloed {};
    StringArray soloNames;
    for (int b = 0; b < an::kNumBands; ++b)
        if ((soloed[(size_t) b] = soloButtons[(size_t) b].getToggleState()))
            soloNames.add (String (an::kBands[b].name).toUpperCase());
    spectrum.setSoloed (soloed);

    String newSoloText = soloNames.isEmpty() ? String() : "SOLO:  " + soloNames.joinIntoString ("  +  ");
    if (midButton.getToggleState() || sideButton.getToggleState())
        newSoloText = (midButton.getToggleState() ? "LISTEN:  MID" : "LISTEN:  SIDE")
                    + (newSoloText.isEmpty() ? String() : "     " + newSoloText);
    if (newSoloText != soloText)
    {
        soloText = newSoloText;
        repaint (0, kBottomBarY, kWidth, kHeight - kBottomBarY);
    }
    spectrum.setRangeDb ((float) rangeKnob.getValue());
}

//==============================================================================
void AnalyserPanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged.
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0x7a11);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 2200; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    // soft vignette
    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void AnalyserPanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Stylised lowercase wordmark: chrome "mix", blue "analyser", and a swoosh.
    const float baseline = area.getBottom() - 7.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "mix", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "analyser", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (VirusColours::blue);
    g.fillPath (secondPath);

    // swoosh under the wordmark
    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (VirusColours::blue.withAlpha (0.0f), sx, sy, VirusColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void AnalyserPanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // --- top strip ----------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);

    drawWordmark (g, { 14.0f, 0.0f, 260.0f, (float) kTopStripH });
    CustomLookAndFeel::drawSmallLabel (g, "Listen", { 390.0f, 8.0f, 56.0f, 19.0f }, Justification::centredRight);

    // --- panels ---------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, spectrumPanel, "Spectrum Balance");
    CustomLookAndFeel::drawPanel (g, calPanel,      "Input Calibration");
    CustomLookAndFeel::drawPanel (g, displayPanel,  "Display");

    // reference level line in the calibration panel
    CustomLookAndFeel::drawSmallLabel (g, "Set Peak To", { 606.0f, 64.0f, 90.0f, 12.0f }, Justification::centredLeft);
    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    g.setColour (VirusColours::blue);
    g.drawText ("-10.0 dB", Rectangle<float> (682.0f, 64.0f, 80.0f, 12.0f), Justification::centredRight);

    // --- bottom bar -----------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);

    auto tab = Rectangle<float> (8.0f, bar.getY() + 7.0f, 70.0f, 18.0f);
    g.setGradientFill (ColourGradient (VirusColours::headerTop, 0.0f, tab.getY(), VirusColours::headerBottom, 0.0f, tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (VirusColours::bevelDark);
    g.drawRoundedRectangle (tab, 3.0f, 1.0f);
    g.setColour (VirusColours::headerText);
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    g.drawText ("ANALYSER", tab, Justification::centred);

    auto inset = bar.reduced (90.0f, 7.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (soloText.isEmpty() ? Colour (0xffc8cdd2) : VirusColours::blue);
    g.drawText (soloText.isEmpty() ? String ("MIX ANALYSER") : soloText, inset, Justification::centred);
}

//==============================================================================
// Resizable editor: the panel is laid out at its base size and scaled as a whole.
//==============================================================================
AnalyserAudioProcessorEditor::AnalyserAudioProcessorEditor (AnalyserAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    // Read the remembered size first: setting up the resize limits below briefly
    // resizes the window, and that must not overwrite the saved size.
    const int savedWidth = processor.shared.editorWidth.load();

    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kWidth * 7 / 10, kHeight * 7 / 10, kWidth * 5 / 2, kHeight * 5 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    const int width = savedWidth > 0 ? jlimit (kWidth * 7 / 10, kWidth * 5 / 2, savedWidth) : kWidth;
    setSize (width, roundToInt (width * (double) kHeight / (double) kWidth));
    sizeRestored = true;
}

void AnalyserAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kWidth));

    if (sizeRestored)
        processor.shared.editorWidth.store (getWidth());
}
