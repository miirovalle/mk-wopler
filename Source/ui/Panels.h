#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "WobbleLookAndFeel.h"
#include "dsp/Filters.h"

class MKWoplerAudioProcessor;

namespace wob::ui
{

//==============================================================================
// Magnitude response of the currently selected filter type, with a draggable
// frequency handle.
class FilterGraph final : public juce::Component,
                          private juce::Timer
{
public:
    FilterGraph (MKWoplerAudioProcessor& processorToUse, int processorIndexToUse);
    ~FilterGraph() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void refresh();
    void setShowModulation (bool shouldShow) { showModulation = shouldShow; repaint(); }

private:
    void timerCallback() override;
    void recompute();
    void setFrequencyFromX (float x);

    juce::Rectangle<float> plotArea() const;
    float frequencyToX (float freq) const;
    float xToFrequency (float x) const;
    float dbToY (float db) const;

    MKWoplerAudioProcessor& processor;
    int processorIndex = 0;

    static constexpr int kFFTOrder = 11;
    static constexpr int kFFTSize = 1 << kFFTOrder;

    std::vector<float> magnitudeDb;
    std::unique_ptr<juce::dsp::FFT> fft;

    float lastType = -1.0f, lastFreq = -1.0f, lastRes = -1.0f, lastGain = -1.0f,
          lastCharacter = -1.0f, lastQuality = -1.0f;
    bool showModulation = true;
    bool dirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterGraph)
};

//==============================================================================
// Displays the composed oscillator shape and lets the user draw the custom
// curve that is blended in by the "Custom" amount.
class LfoEditor final : public juce::Component
{
public:
    LfoEditor (MKWoplerAudioProcessor& processorToUse, int processorIndexToUse);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void refresh() { rebuild(); repaint(); }

    std::function<void()> onCurveEdited;

private:
    void rebuild();
    void applyDrag (const juce::MouseEvent&);

    MKWoplerAudioProcessor& processor;
    int processorIndex = 0;

    static constexpr int kDisplayPoints = 256;
    std::vector<float> shapePoints;
    bool dragging = false;
    int lastPointIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoEditor)
};

//==============================================================================
class LevelMeter final : public juce::Component,
                         private juce::Timer
{
public:
    enum class Mode { Peak, Width };

    LevelMeter (MKWoplerAudioProcessor& processorToUse, Mode modeToUse, juce::String labelText);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    MKWoplerAudioProcessor& processor;
    Mode mode;
    juce::String label;
    float smoothed[2] { 0.0f, 0.0f };
    float peakHold[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

//==============================================================================
// Small helpers shared by the panels.
juce::String parameterIdForIndex (int index);
int globalParam (int which);
int procParam (int processorIndex, int which);
int bandParam (int bandIndex, int which);

} // namespace wob::ui
