#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/WobbleLookAndFeel.h"

//==============================================================================
// Small reusable controls bound to a parameter index.
namespace wob::ui
{

// A slider that formats its value through a per-parameter formatter, so the
// text box shows "800 Hz" instead of a raw long double.
class KnobSlider final : public juce::Slider
{
public:
    std::function<juce::String (double)> formatter;

    juce::String getTextFromValue (double value) override
    {
        return formatter != nullptr ? formatter (value) + getTextValueSuffix()
                                    : juce::Slider::getTextFromValue (value);
    }

    double getValueFromText (const juce::String& text) override
    {
        auto t = text.trimStart();

        if (t.endsWith (getTextValueSuffix()))
            t = t.substring (0, t.length() - getTextValueSuffix().length());

        return t.retainCharacters ("0123456789.-").getDoubleValue();
    }
};

class Knob final : public juce::Component
{
public:
    Knob (MKWoplerAudioProcessor& processor, int parameterIndex, juce::String labelText);

    void resized() override;
    void paint (juce::Graphics&) override;

    KnobSlider slider;

private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

class Toggle final : public juce::Component
{
public:
    Toggle (MKWoplerAudioProcessor& processor, int parameterIndex, juce::String labelText);

    void resized() override;

    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toggle)
};

class Choice final : public juce::Component
{
public:
    Choice (MKWoplerAudioProcessor& processor, int parameterIndex, juce::String labelText);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::ComboBox combo;

private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Choice)
};

//==============================================================================
// One filter processor page: response graph, oscillator display and the
// parameters that belong to that processor.
class FilterPage final : public juce::Component
{
public:
    FilterPage (MKWoplerAudioProcessor& processor, int processorIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    MKWoplerAudioProcessor& proc;
    int index = 0;

    FilterGraph graph;
    LfoEditor lfoEditor;

    juce::Label title;
    std::unique_ptr<Choice> typeBox;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Toggle> toggles;
    std::unique_ptr<Choice> noteLengthBox;
    std::unique_ptr<Choice> followerEqBox;

    juce::GroupComponent filterGroup, driveGroup, lfoGroup, followerGroup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterPage)
};

//==============================================================================
class EasyPage final : public juce::Component
{
public:
    explicit EasyPage (MKWoplerAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MKWoplerAudioProcessor& proc;

    juce::OwnedArray<Knob> knobs;
    Toggle multibandToggle;
    LevelMeter meterIn, meterOut, meterWidth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyPage)
};

//==============================================================================
class OutputPage final : public juce::Component
{
public:
    explicit OutputPage (MKWoplerAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MKWoplerAudioProcessor& proc;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Toggle> toggles;
    LevelMeter meterIn, meterOut, meterWidth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputPage)
};

//==============================================================================
class BandsPage final : public juce::Component
{
public:
    explicit BandsPage (MKWoplerAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct BandStrip : public juce::Component
    {
        BandStrip (MKWoplerAudioProcessor& processor, int bandIndex);
        void paint (juce::Graphics&) override;
        void resized() override;

        juce::OwnedArray<Knob> knobs;
        juce::OwnedArray<Toggle> toggles;
        juce::Label name;
        int index = 0;
    };

    MKWoplerAudioProcessor& proc;
    juce::OwnedArray<BandStrip> strips;
    juce::OwnedArray<Knob> crossovers;
    Toggle multibandToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandsPage)
};

} // namespace wob::ui

//==============================================================================
class MKWoplerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MKWoplerAudioProcessorEditor (MKWoplerAudioProcessor&);
    ~MKWoplerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showPage (int pageIndex);

    MKWoplerAudioProcessor& processor;
    wob::ui::WobbleLookAndFeel lookAndFeel;

    juce::OwnedArray<juce::Component> pages;
    juce::OwnedArray<juce::TextButton> pageButtons;
    juce::Label titleLabel, subtitleLabel;
    juce::Label latencyLabel;
    int currentPage = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MKWoplerAudioProcessorEditor)
};
