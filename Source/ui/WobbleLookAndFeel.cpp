#include "WobbleLookAndFeel.h"

namespace wob::ui
{

const juce::Colour WobbleLookAndFeel::background { 0xff14161a };
const juce::Colour WobbleLookAndFeel::panel      { 0xff1e2229 };
const juce::Colour WobbleLookAndFeel::panelDark  { 0xff12151a };
const juce::Colour WobbleLookAndFeel::accent     { 0xffffb347 };
const juce::Colour WobbleLookAndFeel::accentDim  { 0xff8a5f1f };
const juce::Colour WobbleLookAndFeel::text       { 0xffe8ecf2 };
const juce::Colour WobbleLookAndFeel::textDim    { 0xff8b94a3 };
const juce::Colour WobbleLookAndFeel::curve      { 0xff4fd1c5 };

WobbleLookAndFeel::WobbleLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, panelDark);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, panel.brighter (0.25f));
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panelDark);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId, accent);
}

void WobbleLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStart, float rotaryEnd,
                                          juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStart + sliderPos * (rotaryEnd - rotaryStart);

    const float lineWidth = juce::jmax (2.0f, radius * 0.16f);
    const float arcRadius = radius - lineWidth * 0.6f;

    g.setColour (panelDark);
    g.fillEllipse (bounds.withSizeKeepingCentre (radius * 1.7f, radius * 1.7f));

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStart, rotaryEnd, true);
    g.setColour (panel.brighter (0.12f));
    g.strokePath (track, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStart, angle, true);
    g.setColour (slider.isEnabled() ? accent : accentDim);
    g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Pointer
    juce::Path pointer;
    const float pointerThickness = juce::jmax (2.0f, radius * 0.12f);
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -arcRadius * 0.55f,
                                 pointerThickness, arcRadius * 0.62f, pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (text);
    g.fillPath (pointer);
}

void WobbleLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = button.getToggleState();

    auto fill = on ? accent.withAlpha (0.85f) : panelDark;
    if (isHighlighted)
        fill = fill.brighter (0.12f);
    if (isDown)
        fill = fill.darker (0.15f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (on ? accent : panel.brighter (0.2f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    g.setColour (on ? juce::Colours::black : textDim);
    g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 13.0f, bounds.getHeight() * 0.5f))));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
}

void WobbleLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);

    g.setColour (panelDark);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (box.isMouseOver() ? accent.withAlpha (0.6f) : panel.brighter (0.22f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    // The text of a non-editable combo box is drawn here, so an override that
    // omits it leaves the control looking empty.
    g.setColour (box.findColour (juce::ComboBox::textColourId));
    g.setFont (getComboBoxFont (box));

    auto textArea = bounds.reduced (7.0f, 0.0f).withTrimmedRight (14.0f);
    g.drawText (box.getText(), textArea, juce::Justification::centredLeft, true);

    juce::Path arrow;
    const float cx = (float) width - 12.0f;
    const float cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 3.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (accent);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

juce::Font WobbleLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (11.5f));
}

juce::Font WobbleLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (11.5f));
}

} // namespace wob::ui
