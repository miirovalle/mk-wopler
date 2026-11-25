#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Engine.h"
#include "dsp/Params.h"

//==============================================================================
class MKWoplerAudioProcessor final : public juce::AudioProcessor
{
public:
    MKWoplerAudioProcessor();
    ~MKWoplerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    wob::WobblerEngine& getEngine() noexcept { return engine; }
    const wob::LfoProgram& getLfoProgram (int processorIndex) const noexcept
    {
        return lfoPrograms[processorIndex == 1 ? 1 : 0];
    }

    void setCustomCurvePoint (int processorIndex, int pointIndex, float value);
    void setStepValue (int processorIndex, int stepIndex, float value);
    void setStepCount (int processorIndex, int count);
    void convertShapeToHarmonics (int processorIndex);

    int getOversamplingFactor() const noexcept { return pendingOversampling.load(); }
    void setOversamplingFactor (int factor);

    std::atomic<float> meterIn[2] { { 0.0f }, { 0.0f } };
    std::atomic<float> meterOut[2] { { 0.0f }, { 0.0f } };
    std::atomic<float> meterWidth { 0.0f };

    static constexpr int kMaxOversampling = 3;   // 1x, 2x, 4x

private:
    void updateEngineParameters();
    void buildParameterLayout (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    void configureDsp();

    wob::WobblerEngine engine;
    wob::LfoProgram lfoPrograms[wob::kNumProcessors];

    std::vector<std::atomic<float>*> rawParameters;
    bool wasPlaying = false;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::atomic<int> pendingOversampling { 1 };
    int activeOversampling = 1;

    juce::AudioBuffer<float> monoBuffer;
    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    int lastMidiNote = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MKWoplerAudioProcessor)
};
