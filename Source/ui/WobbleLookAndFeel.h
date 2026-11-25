#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace wob::ui
{

// Dark, high-contrast palette inspired by the hardware-ish look of the
// reference plugin: dark slate panels, amber highlighting for modulated values.
class WobbleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WobbleLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

    static const juce::Colour background;
    static const juce::Colour panel;
    static const juce::Colour panelDark;
    static const juce::Colour accent;
    static const juce::Colour accentDim;
    static const juce::Colour text;
    static const juce::Colour textDim;
    static const juce::Colour curve;
};

} // namespace wob::ui
