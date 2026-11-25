#include "PluginEditor.h"

#include <cmath>

namespace wob::ui
{

//==============================================================================
Knob::Knob (MKWoplerAudioProcessor& processor, int parameterIndex, juce::String labelText)
    : label (std::move (labelText))
{
    const auto& spec = wob::ParamTable::get()[(size_t) parameterIndex];
    const auto unit = juce::String (spec.unit);
    const bool isShapeSelector = spec.id.find ("lfoShape") != std::string::npos;

    // Several per-band parameters use their minimum value to mean "inherit from
    // the processor", which needs to be spelled out rather than shown as a
    // number.
    const bool isBandParameter = spec.id.rfind ("band", 0) == 0;
    const bool endsWithDotFreq = spec.id.size() > 5
                                 && spec.id.compare (spec.id.size() - 5, 5, ".freq") == 0;
    const bool inheritable = isBandParameter && (spec.minv < 0.0f || endsWithDotFreq);
    const float inheritValue = spec.minv;

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 14);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    slider.setNumDecimalPlacesToDisplay (2);

    slider.formatter = [unit, isShapeSelector, inheritable, inheritValue] (double value)
    {
        if (inheritable && value <= (double) inheritValue + 1.0e-4)
            return juce::String ("Inherit");

        if (isShapeSelector)
        {
            const int index = juce::jlimit (0, wob::kNumShapes - 1, juce::roundToInt (value));
            return juce::String (wob::kShapeNames[index]);
        }

        if (unit == "Hz")
            return value >= 1000.0 ? juce::String (value / 1000.0, 2) + " kHz"
                                   : juce::String (juce::roundToInt (value)) + " Hz";

        int decimals = 2;

        if (std::fabs (value) >= 100.0)
            decimals = 1;
        if (std::fabs (value) >= 1000.0)
            decimals = 0;

        auto text = juce::String (value, decimals);

        if (unit.isEmpty())
            return text;

        return text + " " + unit;
    };

    slider.updateText();

    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, parameterIdForIndex (parameterIndex), slider);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (WobbleLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (label, getLocalBounds().removeFromTop (13), juce::Justification::centred, false);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (13));
}

//==============================================================================
Toggle::Toggle (MKWoplerAudioProcessor& processor, int parameterIndex, juce::String labelText)
{
    button.setButtonText (labelText);
    addAndMakeVisible (button);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, parameterIdForIndex (parameterIndex), button);
}

void Toggle::resized() { button.setBounds (getLocalBounds()); }

//==============================================================================
Choice::Choice (MKWoplerAudioProcessor& processor, int parameterIndex, juce::String labelText)
    : label (std::move (labelText))
{
    const auto id = parameterIdForIndex (parameterIndex);

    // ComboBoxParameterAttachment expects the box to be populated already, so the
    // choice list has to be copied in before the attachment is created.
    if (auto* param = processor.apvts.getParameter (id))
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
            combo.addItemList (choice->choices, 1);
        else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*> (param))
            combo.addItemList ({ "Off", "On" }, 1);
    }

    combo.setTextWhenNoChoicesAvailable ("-");
    addAndMakeVisible (combo);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, id, combo);
}

void Choice::paint (juce::Graphics& g)
{
    g.setColour (WobbleLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (label, getLocalBounds().removeFromTop (12), juce::Justification::centredLeft, false);
}

void Choice::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (13);
    combo.setBounds (area);
}

//==============================================================================
FilterPage::FilterPage (MKWoplerAudioProcessor& processor, int processorIndex)
    : proc (processor), index (processorIndex),
      graph (processor, processorIndex),
      lfoEditor (processor, processorIndex)
{
    title.setText (processorIndex == 0 ? "FILTER 1" : "FILTER 2", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, WobbleLookAndFeel::accent);
    addAndMakeVisible (title);

    addAndMakeVisible (graph);
    addAndMakeVisible (lfoEditor);

    typeBox = std::make_unique<Choice> (processor, procParam (index, wob::ppType), "TYPE");
    addAndMakeVisible (typeBox.get());

    const int knobParams[] =
    {
        // Filter section (8)
        wob::ppFreq, wob::ppFreqRange, wob::ppResonance, wob::ppResRange,
        wob::ppGain, wob::ppGainRange, wob::ppCharacter, wob::ppCharRange,
        // Drive/output section (7)
        wob::ppDrive, wob::ppDriveMode, wob::ppSaturation, wob::ppOutGain,
        wob::ppPanorama, wob::ppTrim, wob::ppQuality,
        // Oscillator section (8)
        wob::ppLRate, wob::ppLWidth, wob::ppLPhase, wob::ppLShape,
        wob::ppLCustom, wob::ppLStep, wob::ppLSmooth, wob::ppLOverride,
        // Follower section (8)
        wob::ppFDepth, wob::ppFAttack, wob::ppFRelease, wob::ppFRms,
        wob::ppFMaxLevel, wob::ppFEqFreq, wob::ppFEqGain, wob::ppFEqQ
    };

    for (int p : knobParams)
    {
        const auto& spec = wob::ParamTable::get()[(size_t) procParam (index, p)];
        auto* knob = new Knob (processor, procParam (index, p), juce::String (spec.name).replace ("F1 ", "").replace ("F2 ", ""));
        knobs.add (knob);
        addAndMakeVisible (knob);
    }

    const int toggleParams[] = { wob::ppClip, wob::ppLInvert, wob::ppLSync,
                                 wob::ppLHarmonics, wob::ppLMidiReset, wob::ppFSidechain };
    const char* const toggleNames[] = { "CLIP", "INVERT", "SYNC", "HARMONIC", "MIDI RST", "SIDE-CHAIN" };

    for (int i = 0; i < (int) (sizeof (toggleParams) / sizeof (toggleParams[0])); ++i)
    {
        auto* toggle = new Toggle (processor, procParam (index, toggleParams[i]), toggleNames[i]);
        toggles.add (toggle);
        addAndMakeVisible (toggle);
    }

    noteLengthBox = std::make_unique<Choice> (processor, procParam (index, wob::ppLNoteLength), "SYNC LENGTH");
    addAndMakeVisible (noteLengthBox.get());

    followerEqBox = std::make_unique<Choice> (processor, procParam (index, wob::ppFEqType), "FOLLOWER EQ");
    addAndMakeVisible (followerEqBox.get());

    filterGroup.setText ("Filter");
    driveGroup.setText ("Drive / Output");
    lfoGroup.setText ("Oscillator");
    followerGroup.setText ("Follower");

    for (auto* group : { &filterGroup, &driveGroup, &lfoGroup, &followerGroup })
    {
        group->setColour (juce::GroupComponent::outlineColourId, WobbleLookAndFeel::panel.brighter (0.2f));
        group->setColour (juce::GroupComponent::textColourId, WobbleLookAndFeel::textDim);
        addAndMakeVisible (group);
    }

    lfoEditor.onCurveEdited = [this] { graph.refresh(); };
}

void FilterPage::refresh()
{
    graph.refresh();
    lfoEditor.refresh();
}

void FilterPage::paint (juce::Graphics& g)
{
    g.setColour (WobbleLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void FilterPage::resized()
{
    auto area = getLocalBounds().reduced (8);

    auto header = area.removeFromTop (22);
    title.setBounds (header.removeFromLeft (90));
    typeBox->setBounds (header.removeFromLeft (230).withTrimmedTop (-4));

    area.removeFromTop (4);

    // Response graph and oscillator display share the top third.
    const int graphHeight = juce::jlimit (96, 180, (int) ((float) area.getHeight() * 0.30f));
    auto topRow = area.removeFromTop (graphHeight);
    graph.setBounds (topRow.removeFromLeft (topRow.getWidth() / 2 - 4));
    topRow.removeFromLeft (8);
    lfoEditor.setBounds (topRow);

    area.removeFromTop (6);

    // Four equal sections fill the rest; the group titles need 14 px of inset.
    const int gap = 6;
    const int sectionHeight = juce::jmax (66, (area.getHeight() - 3 * gap) / 4);

    auto layoutKnobs = [] (juce::OwnedArray<Knob>& knobs, int first, int count,
                           juce::Rectangle<int> row)
    {
        const int width = juce::jmax (1, row.getWidth() / juce::jmax (1, count));

        for (int i = 0; i < count; ++i)
            if (first + i < knobs.size())
                knobs[first + i]->setBounds (row.removeFromLeft (width).reduced (1));
    };

    filterGroup.setBounds (area.removeFromTop (sectionHeight));
    layoutKnobs (knobs, 0, 8, filterGroup.getBounds().reduced (7).withTrimmedTop (13));

    area.removeFromTop (gap);
    driveGroup.setBounds (area.removeFromTop (sectionHeight));
    {
        auto inner = driveGroup.getBounds().reduced (7).withTrimmedTop (13);
        auto toggleArea = inner.removeFromRight (62).removeFromTop (17);
        toggles[0]->setBounds (toggleArea);            // Clip
        layoutKnobs (knobs, 8, 7, inner);
    }

    area.removeFromTop (gap);
    lfoGroup.setBounds (area.removeFromTop (sectionHeight));
    {
        auto inner = lfoGroup.getBounds().reduced (7).withTrimmedTop (13);
        auto extras = inner.removeFromRight (146);
        layoutKnobs (knobs, 15, 8, inner);

        // Four LFO switches in a 2x2 grid, then the sync length selector.
        const int cellW = extras.getWidth() / 2;
        const int cellH = 17;
        const juce::Rectangle<int> grid (extras.getX(), extras.getY(), extras.getWidth(), cellH * 2);

        for (int i = 0; i < 4 && 1 + i < toggles.size(); ++i)
        {
            const auto cell = juce::Rectangle<int> (grid.getX() + (i % 2) * cellW,
                                                    grid.getY() + (i / 2) * cellH,
                                                    cellW, cellH).reduced (1, 1);
            toggles[1 + i]->setBounds (cell);
        }

        noteLengthBox->setBounds (extras.getX(), grid.getBottom() + 4, extras.getWidth(), 34);
    }

    area.removeFromTop (gap);
    followerGroup.setBounds (area.removeFromTop (sectionHeight));
    {
        auto inner = followerGroup.getBounds().reduced (7).withTrimmedTop (13);
        auto extras = inner.removeFromRight (146);
        layoutKnobs (knobs, 23, 8, inner);

        followerEqBox->setBounds (extras.removeFromTop (34));
        extras.removeFromTop (2);

        if (toggles.size() > 5)
            toggles[5]->setBounds (extras.removeFromTop (17));   // Side-chain
    }
}

//==============================================================================
EasyPage::EasyPage (MKWoplerAudioProcessor& processor)
    : proc (processor),
      multibandToggle (processor, globalParam (wob::gpMultiband), "MULTIBAND"),
      meterIn (processor, LevelMeter::Mode::Peak, "IN"),
      meterOut (processor, LevelMeter::Mode::Peak, "OUT"),
      meterWidth (processor, LevelMeter::Mode::Width, "WIDTH")
{
    const int easyParams[] =
    {
        procParam (0, wob::ppFreq),
        procParam (0, wob::ppFreqRange),
        procParam (0, wob::ppResonance),
        procParam (0, wob::ppCharacter),
        procParam (0, wob::ppLRate),
        procParam (0, wob::ppLShape),
        procParam (1, wob::ppDrive),
        globalParam (wob::gpDryWet),
        globalParam (wob::gpOutputGain)
    };

    for (int p : easyParams)
    {
        const auto& spec = wob::ParamTable::get()[(size_t) p];
        auto* knob = new Knob (processor, p,
                               juce::String (spec.name).replace ("F1 ", "").replace ("F2 ", "")
                                                      .replace ("LFO ", "").replace ("Glob ", ""));
        knobs.add (knob);
        addAndMakeVisible (knob);
    }

    addAndMakeVisible (multibandToggle);
    addAndMakeVisible (meterIn);
    addAndMakeVisible (meterOut);
    addAndMakeVisible (meterWidth);
}

void EasyPage::paint (juce::Graphics& g)
{
    g.setColour (WobbleLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (WobbleLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Easy screen: the essential wobble controls from filter 1, plus the global mix.",
                getLocalBounds().reduced (16).removeFromTop (16), juce::Justification::topLeft, false);
}

void EasyPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (20);

    auto meters = area.removeFromRight (86).reduced (2);
    meterIn.setBounds (meters.removeFromTop (meters.getHeight() / 3 - 2));
    meters.removeFromTop (4);
    meterOut.setBounds (meters.removeFromTop (meters.getHeight() / 2 - 2));
    meters.removeFromTop (4);
    meterWidth.setBounds (meters);

    area.removeFromRight (8);

    auto bottomRow = area.removeFromBottom (26);
    multibandToggle.setBounds (bottomRow.removeFromLeft (110).reduced (1));

    const int columns = 3;
    const int rows = (knobs.size() + columns - 1) / columns;
    const int knobWidth = area.getWidth() / columns;
    const int knobHeight = juce::jmax (70, area.getHeight() / juce::jmax (1, rows));

    for (int i = 0; i < knobs.size(); ++i)
    {
        const int col = i % columns;
        const int row = i / columns;
        knobs[i]->setBounds (area.getX() + col * knobWidth,
                             area.getY() + row * knobHeight,
                             knobWidth, knobHeight);
    }
}

//==============================================================================
OutputPage::OutputPage (MKWoplerAudioProcessor& processor)
    : proc (processor),
      meterIn (processor, LevelMeter::Mode::Peak, "IN"),
      meterOut (processor, LevelMeter::Mode::Peak, "OUT"),
      meterWidth (processor, LevelMeter::Mode::Width, "WIDTH")
{
    const int knobParams[] =
    {
        globalParam (wob::gpDryWet),
        globalParam (wob::gpSaturation),
        globalParam (wob::gpOutputGain),
        globalParam (wob::gpKeyTrack)
    };

    for (int p : knobParams)
    {
        const auto& spec = wob::ParamTable::get()[(size_t) p];
        auto* knob = new Knob (processor, p, juce::String (spec.name));
        knobs.add (knob);
        addAndMakeVisible (knob);
    }

    const int toggleParams[] =
    {
        globalParam (wob::gpClipping),
        globalParam (wob::gpDcFilter),
        globalParam (wob::gpMultiband)
    };

    for (int p : toggleParams)
    {
        const auto& spec = wob::ParamTable::get()[(size_t) p];
        auto* toggle = new Toggle (processor, p, juce::String (spec.name));
        toggles.add (toggle);
        addAndMakeVisible (toggle);
    }

    addAndMakeVisible (meterIn);
    addAndMakeVisible (meterOut);
    addAndMakeVisible (meterWidth);
}

void OutputPage::paint (juce::Graphics& g)
{
    g.setColour (WobbleLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (WobbleLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawFittedText ("Global saturation and gain run after both filter processors.\n"
                      "Oversampling is set from the host's parameter list (1x / 2x / 4x).\n"
                      "MIDI Follow scales the filter frequency with the last played note.",
                      getLocalBounds().reduced (16).removeFromBottom (60),
                      juce::Justification::bottomLeft, 4);
}

void OutputPage::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromBottom (56);            // space for the description text

    auto meters = area.removeFromRight (86).reduced (2);
    meterIn.setBounds (meters.removeFromTop (meters.getHeight() / 3 - 2));
    meters.removeFromTop (5);
    meterOut.setBounds (meters.removeFromTop (meters.getHeight() / 2 - 2));
    meters.removeFromTop (5);
    meterWidth.setBounds (meters);

    area.removeFromRight (12);

    const int knobAreaHeight = juce::jlimit (110, 170, area.getHeight() / 2);
    auto knobRow = area.removeFromTop (knobAreaHeight);
    const int knobWidth = juce::jmax (84, knobRow.getWidth() / juce::jmax (1, knobs.size()));

    for (auto* knob : knobs)
        knob->setBounds (knobRow.removeFromLeft (knobWidth).reduced (4));

    auto toggleRow = area.removeFromTop (22);

    for (auto* toggle : toggles)
        toggleRow.removeFromLeft (8), toggle->setBounds (toggleRow.removeFromLeft (120).reduced (1));
}

//==============================================================================
BandsPage::BandStrip::BandStrip (MKWoplerAudioProcessor& processor, int bandIndex)
    : index (bandIndex)
{
    name.setText ("BAND " + juce::String (bandIndex + 1), juce::dontSendNotification);
    name.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    name.setJustificationType (juce::Justification::centred);
    name.setColour (juce::Label::textColourId, WobbleLookAndFeel::accent);
    addAndMakeVisible (name);

    const int toggleParams[] = { wob::bpEnable, wob::bpMute, wob::bpSolo };

    for (int p : toggleParams)
    {
        static const char* names[] = { "ON", "MUTE", "SOLO" };
        auto* toggle = new Toggle (processor, bandParam (index, p), names[p]);
        toggles.add (toggle);
        addAndMakeVisible (toggle);
    }

    const int knobParams[] =
    {
        wob::bpLevel, wob::bpFreq, wob::bpFreqRange, wob::bpResonance,
        wob::bpResRange, wob::bpGain, wob::bpCharacter, wob::bpDrive,
        wob::bpOutGain, wob::bpTrim, wob::bpPan, wob::bpLPhaseOffset,
        wob::bpLRateMult, wob::bpLWidth
    };

    for (int p : knobParams)
    {
        const auto& spec = wob::ParamTable::get()[(size_t) bandParam (index, p)];
        auto* knob = new Knob (processor, bandParam (index, p),
                               juce::String (spec.name).replace ("Band " + juce::String (index + 1) + " ", ""));
        knobs.add (knob);
        addAndMakeVisible (knob);
    }
}

void BandsPage::BandStrip::paint (juce::Graphics& g)
{
    g.setColour (WobbleLookAndFeel::panelDark);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (WobbleLookAndFeel::panel.brighter (0.15f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void BandsPage::BandStrip::resized()
{
    auto area = getLocalBounds().reduced (4);
    name.setBounds (area.removeFromTop (16));

    auto toggleRow = area.removeFromTop (18);
    const int toggleWidth = toggleRow.getWidth() / juce::jmax (1, toggles.size());

    for (auto* toggle : toggles)
        toggle->setBounds (toggleRow.removeFromLeft (toggleWidth).reduced (1));

    area.removeFromTop (4);

    const int columns = 2;
    const int rows = (knobs.size() + columns - 1) / columns;
    const int knobWidth = area.getWidth() / columns;
    const int knobHeight = juce::jmax (46, area.getHeight() / juce::jmax (1, rows));

    for (int i = 0; i < knobs.size(); ++i)
    {
        const int col = i % columns;
        const int row = i / columns;
        knobs[i]->setBounds (area.getX() + col * knobWidth,
                             area.getY() + row * knobHeight,
                             knobWidth, knobHeight);
    }
}

BandsPage::BandsPage (MKWoplerAudioProcessor& processor)
    : proc (processor),
      multibandToggle (processor, globalParam (wob::gpMultiband), "MULTIBAND")
{
    for (int b = 0; b < wob::kNumBands; ++b)
    {
        auto* strip = new BandStrip (processor, b);
        strips.add (strip);
        addAndMakeVisible (strip);
    }

    for (int i = 0; i < wob::kNumCrossovers; ++i)
    {
        auto* knob = new Knob (processor, globalParam (wob::gpXover1 + i),
                               "XOVER " + juce::String (i + 1));
        crossovers.add (knob);
        addAndMakeVisible (knob);
    }

    addAndMakeVisible (multibandToggle);
}

void BandsPage::paint (juce::Graphics& g)
{
    g.setColour (WobbleLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void BandsPage::resized()
{
    auto area = getLocalBounds().reduced (8);

    auto header = area.removeFromTop (24);
    multibandToggle.setBounds (header.removeFromLeft (120).reduced (1));

    auto crossoverRow = area.removeFromTop (66);
    const int crossoverWidth = crossoverRow.getWidth() / juce::jmax (1, crossovers.size());

    for (auto* knob : crossovers)
        knob->setBounds (crossoverRow.removeFromLeft (crossoverWidth).reduced (2));

    area.removeFromTop (4);

    const int stripWidth = area.getWidth() / juce::jmax (1, strips.size());

    for (auto* strip : strips)
        strip->setBounds (area.removeFromLeft (stripWidth).reduced (2));
}

} // namespace wob::ui

//==============================================================================
MKWoplerAudioProcessorEditor::MKWoplerAudioProcessorEditor (MKWoplerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    titleLabel.setText ("MK WOPLER", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, wob::ui::WobbleLookAndFeel::accent);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText (juce::String ("dual distorting wobble filter  -  AUv3 / iPadOS 17  -  v")
                               + JucePlugin_VersionString,
                           juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
    subtitleLabel.setColour (juce::Label::textColourId, wob::ui::WobbleLookAndFeel::textDim);
    addAndMakeVisible (subtitleLabel);

    latencyLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    latencyLabel.setColour (juce::Label::textColourId, wob::ui::WobbleLookAndFeel::textDim);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);

    pages.add (new wob::ui::EasyPage (processor));
    pages.add (new wob::ui::FilterPage (processor, 0));
    pages.add (new wob::ui::FilterPage (processor, 1));
    pages.add (new wob::ui::OutputPage (processor));
    pages.add (new wob::ui::BandsPage (processor));

    const char* names[] = { "EASY", "FILTER 1", "FILTER 2", "OUTPUT", "BANDS" };

    for (int i = 0; i < pages.size(); ++i)
    {
        addChildComponent (pages[i]);

        auto* button = new juce::TextButton (names[i]);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (9001);
        button->onClick = [this, i] { showPage (i); };
        pageButtons.add (button);
        addAndMakeVisible (button);
    }

    pageButtons[0]->setToggleState (true, juce::dontSendNotification);
    showPage (0);

    setResizable (true, false);
    setResizeLimits (860, 600, 1700, 1100);
    setSize (940, 680);
}

MKWoplerAudioProcessorEditor::~MKWoplerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void MKWoplerAudioProcessorEditor::showPage (int pageIndex)
{
    currentPage = pageIndex;

    for (int i = 0; i < pages.size(); ++i)
        pages[i]->setVisible (i == pageIndex);

    if (auto* filterPage = dynamic_cast<wob::ui::FilterPage*> (pages[pageIndex]))
        filterPage->refresh();

    resized();
}

void MKWoplerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (wob::ui::WobbleLookAndFeel::background);

    juce::ColourGradient gradient (wob::ui::WobbleLookAndFeel::accent.withAlpha (0.07f), 0.0f, 0.0f,
                                   juce::Colours::transparentBlack, 0.0f, 90.0f, false);
    g.setGradientFill (gradient);
    g.fillRect (getLocalBounds().removeFromTop (90));
}

void MKWoplerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto header = area.removeFromTop (46);
    auto titleArea = header.removeFromLeft (230);
    titleLabel.setBounds (titleArea.removeFromTop (26));
    subtitleLabel.setBounds (titleArea);

    latencyLabel.setBounds (header.removeFromRight (150).removeFromBottom (18));
    latencyLabel.setText (processor.getOversamplingFactor() > 1
                              ? juce::String (processor.getOversamplingFactor()) + "x oversampled, latency "
                                    + juce::String (processor.getLatencySamples()) + " smp"
                              : "no oversampling, zero latency",
                          juce::dontSendNotification);

    header.removeFromRight (6);

    const int buttonWidth = 92;
    auto buttonRow = header.removeFromBottom (26);

    for (auto* button : pageButtons)
        button->setBounds (buttonRow.removeFromLeft (buttonWidth).reduced (2, 0));

    area.removeFromTop (4);

    for (auto* page : pages)
        page->setBounds (area);
}
