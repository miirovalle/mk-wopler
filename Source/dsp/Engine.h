#pragma once

#include "DspUtil.h"
#include "Follower.h"
#include "Filters.h"
#include "Lfo.h"
#include "Params.h"

#include <vector>

namespace wob
{

//==============================================================================
// Serial Linkwitz-Riley splitter producing up to six bands.
class MultibandSplitter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    int process (const float* left, const float* right, int numSamples,
                 int numBands, float* const* bandLeft, float* const* bandRight,
                 const float* crossovers) noexcept;

private:
    std::array<std::array<Svf, 2>, kNumCrossovers> lowSplit;
    std::array<std::array<Svf, 2>, kNumCrossovers> highSplit;
    std::vector<float> restL, restR;
    double sr = 44100.0;
};

//==============================================================================
// One filter processor: input drive, modulated filter, output saturation, gain.
class ProcessorUnit
{
public:
    void prepare (double sampleRate, int blockSize);
    void reset() noexcept;

    void setProgram (const LfoProgram* newProgram) noexcept { lfo.setProgram (newProgram); }

    void process (float* left, float* right, int numSamples,
                  const ProcParams& p, const BandParams* band,
                  const float* sidechainLeft, const float* sidechainRight,
                  double frequencyScale, double bpm) noexcept;

    FilterInstance filter;
    Lfo lfo;
    Follower follower;

    float lastControl[2] = { 0.0f, 0.0f };

private:
    void applyDrive (float* left, float* right, int numSamples, float amount, float mode) noexcept;
    void applySaturation (float* left, float* right, int numSamples, float amount) noexcept;

    double sr = 44100.0;
    std::vector<float> ctrlL, ctrlR;
};

//==============================================================================
class WobblerEngine
{
public:
    static constexpr int kControlBlock = 32;

    void prepare (double sampleRate, int maximumBlockSize);
    void reset() noexcept;

    void setParams (const EngineParams& newParams) noexcept;
    const EngineParams& getParams() const noexcept { return params; }

    void setLfoProgram (int processorIndex, const LfoProgram& program) noexcept;
    const LfoProgram& getLfoProgram (int processorIndex) const noexcept;

    void setTempo (double newBpm) noexcept;
    void setTransportPlaying (bool isPlaying) noexcept { transportPlaying = isPlaying; }
    void setMidiNote (int note) noexcept;
    void resetLfoPhases() noexcept;

    // Optional external side-chain; call before process() with a block of the
    // same length, or with nullptr to follow the main input.
    void setSidechain (const float* left, const float* right) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    float inputPeak[2] = { 0.0f, 0.0f };
    float outputPeak[2] = { 0.0f, 0.0f };
    float widthMeter = 0.0f;

    double getSampleRate() const noexcept { return sampleRate; }

private:
    void processControlBlock (float* left, float* right, int numSamples) noexcept;
    void runChain (int bandIndex, float* left, float* right, int numSamples,
                   const BandParams* band) noexcept;

    double sampleRate = 44100.0;
    int maxBlock = 512;

    EngineParams params;
    LfoProgram programs[kNumProcessors];

    std::array<std::array<ProcessorUnit, kNumProcessors>, kNumBands> chains;

    MultibandSplitter splitter;

    std::vector<float> dryL, dryR, workL, workR;
    std::vector<float> bandL[kNumBands], bandR[kNumBands];    std::array<float*, kNumBands> bandLeftPtrs {};
    std::array<float*, kNumBands> bandRightPtrs {};

    const float* sidechainL = nullptr;
    const float* sidechainR = nullptr;

    double bpm = 120.0;
    bool transportPlaying = false;
    int midiNote = 60;

    OnePole chainDcL, chainDcR;
    Smoother dryWetSmooth, outGainSmooth;
    float widthAccum = 0.0f;
    int widthCount = 0;
    float peakDecay = 0.0f;
};

} // namespace wob
