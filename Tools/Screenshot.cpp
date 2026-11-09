// Renders the editor to a PNG after running band-shaped test noise through the
// processor, so the screenshot shows real readings.
//   AnalyserScreenshot <out.png> [scale]
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File out (File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshot.png"));
    const float scale = argc > 2 ? (float) std::atof (argv[2]) : 2.0f;

    constexpr double sr = 48000.0;
    constexpr int block = 512, seconds = 6, total = (int) sr * seconds;

    // Target band levels for the demo: a mix with a bit too much low-mid and mid,
    // and not enough upper mids.
    const float wanted[an::kNumBands] = { -20.2f, -26.4f, -25.0f, -33.4f, -34.0f };

    AudioBuffer<float> signal (2, total);
    signal.clear();
    Random rng (1234);
    for (int b = 0; b < an::kNumBands; ++b)
    {
        AudioBuffer<float> band (2, total);
        for (int ch = 0; ch < 2; ++ch)
        {
            dsp::LinkwitzRileyFilter<float> hp, lp;
            hp.setType (dsp::LinkwitzRileyFilter<float>::Type::highpass);
            lp.setType (dsp::LinkwitzRileyFilter<float>::Type::lowpass);
            hp.setCutoffFrequency (std::max (an::kBands[b].loHz, 25.0f));
            lp.setCutoffFrequency (std::min (an::kBands[b].hiHz, 19000.0f));
            hp.prepare ({ sr, 4096, 1 });
            lp.prepare ({ sr, 4096, 1 });
            for (int i = 0; i < total; ++i)
                band.setSample (ch, i, lp.processSample (0, hp.processSample (0, rng.nextFloat() * 2.0f - 1.0f)));
        }
        const double ms = (std::pow (band.getRMSLevel (0, 0, total), 2.0) + std::pow (band.getRMSLevel (1, 0, total), 2.0)) * 0.5;
        const float gain = Decibels::decibelsToGain (wanted[b]) / (float) std::sqrt (ms);
        for (int ch = 0; ch < 2; ++ch)
            signal.addFrom (ch, 0, band, ch, 0, total, gain);
    }

    // limit peaks to -10 dBFS like a mastered reference
    const float ceiling = Decibels::decibelsToGain (an::kRefPeakDb);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
            signal.setSample (ch, i, jlimit (-ceiling, ceiling, signal.getSample (ch, i)));

    AnalyserAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    MidiBuffer midi;
    for (int pos = 0; pos + block <= total; pos += block)
    {
        AudioBuffer<float> chunk (2, block);
        for (int ch = 0; ch < 2; ++ch)
            chunk.copyFrom (ch, 0, signal, ch, pos, block);
        proc.processBlock (chunk, midi);
    }

    std::unique_ptr<AnalyserAudioProcessorEditor> editor (static_cast<AnalyserAudioProcessorEditor*> (proc.createEditor()));
    for (int i = 0; i < 12; ++i)
        editor->getPanel().refresh();

    for (int b = 0; b < an::kNumBands; ++b)
        std::printf ("band %d: %.2f dB RMS\n", b + 1, proc.shared.bandRmsDb[(size_t) b].load());
    std::printf ("input: peak %.2f dB, RMS %.2f dB\n", proc.shared.inputPeakDb.load(), proc.shared.inputRmsDb.load());

    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    out.deleteFile();
    FileOutputStream stream (out);
    PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    editor.reset();
    return ok ? 0 : 1;
}
