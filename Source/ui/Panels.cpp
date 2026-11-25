#include "Panels.h"

#include "PluginProcessor.h"
#include "dsp/Lfo.h"

#include <cmath>

namespace wob::ui
{

juce::String parameterIdForIndex (int index) { return "p" + juce::String (index); }
int globalParam (int which)                  { return wob::ParamTable::globalIndex (which); }
int procParam (int processorIndex, int which){ return wob::ParamTable::procIndex (processorIndex, which); }
int bandParam (int bandIndex, int which)     { return wob::ParamTable::bandIndex (bandIndex, which); }

namespace
{
float readParam (MKWoplerAudioProcessor& processor, int index)
{
    if (auto* value = processor.apvts.getRawParameterValue (parameterIdForIndex (index)))
        return value->load();

    return wob::ParamTable::get()[(size_t) index].defv;
}

constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 20000.0f;
constexpr float kMaxDb = 18.0f;
constexpr float kMinDb = -42.0f;
} // namespace

//==============================================================================
FilterGraph::FilterGraph (MKWoplerAudioProcessor& processorToUse, int processorIndexToUse)
    : processor (processorToUse), processorIndex (processorIndexToUse)
{
    fft = std::make_unique<juce::dsp::FFT> (kFFTOrder);
    magnitudeDb.assign ((size_t) (kFFTSize / 2), 0.0f);

    setInterceptsMouseClicks (true, false);
    startTimerHz (12);
}

FilterGraph::~FilterGraph() = default;

void FilterGraph::resized() { dirty = true; }

void FilterGraph::refresh()
{
    dirty = true;
    recompute();
    repaint();
}

void FilterGraph::timerCallback()
{
    const float type = readParam (processor, procParam (processorIndex, wob::ppType));
    const float freq = readParam (processor, procParam (processorIndex, wob::ppFreq));
    const float res = readParam (processor, procParam (processorIndex, wob::ppResonance));
    const float gain = readParam (processor, procParam (processorIndex, wob::ppGain));
    const float chr = readParam (processor, procParam (processorIndex, wob::ppCharacter));
    const float quality = readParam (processor, procParam (processorIndex, wob::ppQuality));

    if (type != lastType || freq != lastFreq || res != lastRes || gain != lastGain
        || chr != lastCharacter || quality != lastQuality || dirty)
    {
        lastType = type;
        lastFreq = freq;
        lastRes = res;
        lastGain = gain;
        lastCharacter = chr;
        lastQuality = quality;
        dirty = false;

        recompute();
        repaint();
    }
}

void FilterGraph::recompute()
{
    wob::FilterInstance filter;
    filter.prepare (48000.0);
    filter.setType ((int) std::lround (readParam (processor, procParam (processorIndex, wob::ppType))));

    wob::FilterSettings settings;
    settings.freq = readParam (processor, procParam (processorIndex, wob::ppFreq));
    settings.resonance = readParam (processor, procParam (processorIndex, wob::ppResonance));
    settings.gainDb = readParam (processor, procParam (processorIndex, wob::ppGain));
    settings.character = readParam (processor, procParam (processorIndex, wob::ppCharacter));
    settings.quality = readParam (processor, procParam (processorIndex, wob::ppQuality));
    filter.setSettings (settings);

    std::vector<float> left ((size_t) kFFTSize, 0.0f);
    std::vector<float> right ((size_t) kFFTSize, 0.0f);

    left[0] = 1.0f;
    right[0] = 1.0f;

    filter.process (left.data(), right.data(), kFFTSize);

    std::vector<float> data ((size_t) kFFTSize * 2, 0.0f);

    for (int i = 0; i < kFFTSize; ++i)
        data[(size_t) i] = left[(size_t) i];

    fft->performFrequencyOnlyForwardTransform (data.data());

    magnitudeDb.resize ((size_t) (kFFTSize / 2));

    for (size_t i = 0; i < magnitudeDb.size(); ++i)
    {
        const float mag = data[i];

        magnitudeDb[i] = mag > 1.0e-7f ? 20.0f * std::log10 (mag) : -100.0f;
    }
}

juce::Rectangle<float> FilterGraph::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f);
}

float FilterGraph::frequencyToX (float freq) const
{
    const auto area = plotArea();
    const float norm = std::log (juce::jlimit (kMinFreq, kMaxFreq, freq) / kMinFreq)
                     / std::log (kMaxFreq / kMinFreq);

    return area.getX() + norm * area.getWidth();
}

float FilterGraph::xToFrequency (float x) const
{
    const auto area = plotArea();
    const float norm = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / juce::jmax (1.0f, area.getWidth()));

    return kMinFreq * std::pow (kMaxFreq / kMinFreq, norm);
}

float FilterGraph::dbToY (float db) const
{
    const auto area = plotArea();
    const float norm = (juce::jlimit (kMinDb, kMaxDb, db) - kMinDb) / (kMaxDb - kMinDb);

    return area.getBottom() - norm * area.getHeight();
}

void FilterGraph::setFrequencyFromX (float x)
{
    const float freq = xToFrequency (x);

    if (auto* param = processor.apvts.getParameter (parameterIdForIndex (procParam (processorIndex, wob::ppFreq))))
    {
        const auto& table = wob::ParamTable::get();
        const auto& spec = table[(size_t) procParam (processorIndex, wob::ppFreq)];
        param->setValueNotifyingHost (spec.normalise (freq));
    }
}

void FilterGraph::mouseDown (const juce::MouseEvent& e)
{
    setFrequencyFromX ((float) e.position.x);
}

void FilterGraph::mouseDrag (const juce::MouseEvent& e)
{
    setFrequencyFromX ((float) e.position.x);
}

void FilterGraph::mouseDoubleClick (const juce::MouseEvent&)
{
    if (auto* param = processor.apvts.getParameter (parameterIdForIndex (procParam (processorIndex, wob::ppFreq))))
        param->setValueNotifyingHost (param->convertTo0to1 (800.0f));
}

void FilterGraph::paint (juce::Graphics& g)
{
    const auto area = plotArea();

    g.setColour (WobbleLookAndFeel::panelDark);
    g.fillRoundedRectangle (area, 4.0f);

    // Grid
    g.setColour (juce::Colour (0xff262c35));

    const float gridFreqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };

    for (float f : gridFreqs)
    {
        const float x = frequencyToX (f);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f })
        g.drawHorizontalLine ((int) dbToY (db), area.getX(), area.getRight());

    // Response curve
    if (! magnitudeDb.empty())
    {
        juce::Path path;
        bool started = false;

        const int bins = (int) magnitudeDb.size();

        for (int px = 0; px < (int) area.getWidth(); ++px)
        {
            const float x = area.getX() + (float) px;
            const float freq = xToFrequency (x);

            // The analysis ran at 48 kHz; above that, hold the last valid bin.
            const double bin = (double) freq * (double) kFFTSize / 48000.0;
            const int idx = juce::jlimit (0, bins - 1, (int) std::lround (bin));
            const float db = magnitudeDb[(size_t) idx];
            const float y = dbToY (db);

            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                path.lineTo (x, y);
            }
        }

        juce::Path filled (path);
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setGradientFill (juce::ColourGradient (WobbleLookAndFeel::curve.withAlpha (0.28f),
                                                 area.getX(), area.getY(),
                                                 WobbleLookAndFeel::curve.withAlpha (0.02f),
                                                 area.getX(), area.getBottom(), false));
        g.fillPath (filled);

        g.setColour (WobbleLookAndFeel::curve);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    // Frequency handle
    const float cutoff = readParam (processor, procParam (processorIndex, wob::ppFreq));
    const float x = frequencyToX (cutoff);

    if (showModulation)
    {
        const float range = readParam (processor, procParam (processorIndex, wob::ppFreqRange));
        const float octaves = range * 0.01f * 6.0f;
        const float lo = juce::jlimit (kMinFreq, kMaxFreq, cutoff * std::pow (2.0f, -std::fabs (octaves) * 0.5f));
        const float hi = juce::jlimit (kMinFreq, kMaxFreq, cutoff * std::pow (2.0f, std::fabs (octaves) * 0.5f));

        if (range != 0.0f)
        {
            g.setColour (WobbleLookAndFeel::accent.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (frequencyToX (lo), area.getY(),
                                                frequencyToX (hi) - frequencyToX (lo), area.getHeight()));
        }
    }

    g.setColour (WobbleLookAndFeel::accent.withAlpha (0.85f));
    g.drawVerticalLine ((int) x, area.getY(), area.getBottom());

    juce::Path handle;
    handle.addEllipse (x - 5.0f, area.getBottom() - 10.0f, 10.0f, 10.0f);
    g.setColour (WobbleLookAndFeel::accent);
    g.fillPath (handle);

    // Labels
    g.setColour (WobbleLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (cutoff >= 1000.0f ? juce::String (cutoff / 1000.0f, 2) + " kHz"
                                  : juce::String (cutoff, 0) + " Hz",
                juce::Rectangle<int> ((int) x + 6, (int) area.getY() + 4, 60, 12),
                juce::Justification::centredLeft, false);

    g.setColour (WobbleLookAndFeel::panel);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
}

//==============================================================================
LfoEditor::LfoEditor (MKWoplerAudioProcessor& processorToUse, int processorIndexToUse)
    : processor (processorToUse), processorIndex (processorIndexToUse)
{
    shapePoints.assign ((size_t) kDisplayPoints, 0.0f);
    rebuild();
}

void LfoEditor::rebuild()
{
    wob::Lfo lfo;
    lfo.prepare ((double) kDisplayPoints);
    lfo.setProgram (&processor.getLfoProgram (processorIndex));
    lfo.reset();

    lfo.update (1.0f, false, 0, 120.0, 0.0f, 0.0f, false,
                readParam (processor, procParam (processorIndex, wob::ppLShape)),
                readParam (processor, procParam (processorIndex, wob::ppLCustom)),
                readParam (processor, procParam (processorIndex, wob::ppLStep)),
                readParam (processor, procParam (processorIndex, wob::ppLSmooth)),
                readParam (processor, procParam (processorIndex, wob::ppLHarmonics)) > 0.5f);

    std::vector<float> left ((size_t) kDisplayPoints, 0.0f);
    std::vector<float> right ((size_t) kDisplayPoints, 0.0f);
    lfo.render (left.data(), right.data(), kDisplayPoints);

    shapePoints = left;
}

void LfoEditor::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (WobbleLookAndFeel::panelDark);
    g.fillRoundedRectangle (area, 4.0f);

    g.setColour (juce::Colour (0xff262c35));

    for (int i = 0; i <= 4; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    // Custom curve, drawn faintly so the composed shape stays readable.
    const float customAmount = readParam (processor, procParam (processorIndex, wob::ppLCustom));

    juce::Path custom;
    const auto& program = processor.getLfoProgram (processorIndex);

    for (int i = 0; i < wob::LfoProgram::kCurvePoints; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (wob::LfoProgram::kCurvePoints - 1);
        const float y = area.getBottom() - program.customShape[(size_t) i] * area.getHeight();

        if (i == 0)
            custom.startNewSubPath (x, y);
        else
            custom.lineTo (x, y);
    }

    g.setColour (WobbleLookAndFeel::accent.withAlpha (customAmount > 0.5f ? 0.5f : 0.18f));
    g.strokePath (custom, juce::PathStrokeType (1.2f));

    // Composed shape
    juce::Path shape;

    for (size_t i = 0; i < shapePoints.size(); ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (shapePoints.size() - 1);
        const float y = area.getCentreY() - shapePoints[i] * area.getHeight() * 0.46f;

        if (i == 0)
            shape.startNewSubPath (x, y);
        else
            shape.lineTo (x, y);
    }

    g.setColour (WobbleLookAndFeel::curve);
    g.strokePath (shape, juce::PathStrokeType (2.0f));

    g.setColour (WobbleLookAndFeel::panel);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
}

void LfoEditor::applyDrag (const juce::MouseEvent& e)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);

    const float normX = juce::jlimit (0.0f, 1.0f, ((float) e.position.x - area.getX()) / juce::jmax (1.0f, area.getWidth()));
    const float normY = juce::jlimit (0.0f, 1.0f, 1.0f - ((float) e.position.y - area.getY()) / juce::jmax (1.0f, area.getHeight()));

    const int point = juce::jlimit (0, wob::LfoProgram::kCurvePoints - 1,
                                    (int) std::lround (normX * (float) (wob::LfoProgram::kCurvePoints - 1)));

    // Interpolate between the previous and the current point so fast drags do
    // not leave gaps in the curve.
    const int from = lastPointIndex >= 0 ? lastPointIndex : point;
    const int lo = juce::jmin (from, point);
    const int hi = juce::jmax (from, point);

    for (int i = lo; i <= hi; ++i)
        processor.setCustomCurvePoint (processorIndex, i, normY);

    lastPointIndex = point;

    if (onCurveEdited)
        onCurveEdited();

    rebuild();
    repaint();
}

void LfoEditor::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    lastPointIndex = -1;
    applyDrag (e);
}

void LfoEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        applyDrag (e);
}

void LfoEditor::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    lastPointIndex = -1;
}

//==============================================================================
LevelMeter::LevelMeter (MKWoplerAudioProcessor& processorToUse, Mode modeToUse, juce::String labelText)
    : processor (processorToUse), mode (modeToUse), label (std::move (labelText))
{
    startTimerHz (25);
}

void LevelMeter::timerCallback()
{
    const float targetL = mode == Mode::Width ? processor.meterWidth.load() : processor.meterOut[0].load();
    const float targetR = mode == Mode::Width ? processor.meterWidth.load() : processor.meterOut[1].load();

    smoothed[0] = juce::jmax (targetL, smoothed[0] * 0.86f);
    smoothed[1] = juce::jmax (targetR, smoothed[1] * 0.86f);
    peakHold[0] = juce::jmax (targetL, peakHold[0] * 0.995f);
    peakHold[1] = juce::jmax (targetR, peakHold[1] * 0.995f);

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    const auto labelArea = area.removeFromBottom (11.0f);

    g.setColour (WobbleLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (9.5f)));
    g.drawText (label, labelArea, juce::Justification::centred, false);

    const float gap = 2.0f;
    const float barHeight = (area.getHeight() - gap) * 0.5f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float> (area.getX(), area.getY() + (float) ch * (barHeight + gap),
                                           area.getWidth(), barHeight);

        g.setColour (WobbleLookAndFeel::panelDark);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (WobbleLookAndFeel::panel.brighter (0.18f));
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.0f, 1.0f);

        const float level = juce::jlimit (0.0f, 1.5f, smoothed[ch]);
        const float norm = mode == Mode::Width ? juce::jlimit (0.0f, 1.0f, smoothed[ch])
                                               : juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (level, -60.0f) / 60.0f + 1.0f);

        auto filled = bar.withWidth (bar.getWidth() * norm);

        juce::ColourGradient gradient (WobbleLookAndFeel::curve.withAlpha (0.9f), bar.getX(), bar.getY(),
                                       level > 0.95f ? juce::Colour (0xffff5b5b) : WobbleLookAndFeel::accent,
                                       bar.getRight(), bar.getY(), false);

        g.setGradientFill (gradient);
        g.fillRoundedRectangle (filled, 2.0f);

        const float holdNorm = mode == Mode::Width ? juce::jlimit (0.0f, 1.0f, peakHold[ch])
                                                   : juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (peakHold[ch], -60.0f) / 60.0f + 1.0f);

        g.setColour (WobbleLookAndFeel::text.withAlpha (0.7f));
        g.fillRect (bar.getX() + bar.getWidth() * holdNorm - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    }
}

} // namespace wob::ui
