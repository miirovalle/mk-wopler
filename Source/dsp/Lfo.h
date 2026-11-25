#pragma once

#include "DspUtil.h"
#include "Params.h"

namespace wob
{

// Multimode low-frequency oscillator: predefined shape interpolation, drawable
// custom curve, step sequencer, additive harmonics mode, channel width and host
// synchronisation. Mirrors the signal generator described in the manual.
class Lfo
{
public:
    void prepare (double sampleRate);
    void reset (double phase01 = 0.0) noexcept;

    void setProgram (const LfoProgram* newProgram) noexcept { program = newProgram; }

    void update (float rateHz, bool sync, int noteLength, double bpm, float phase01,
                 float widthPercent, bool invert, float shapePos, float customPercent,
                 float stepPercent, float smoothPercent, bool harmonicsMode) noexcept;

    void setOverride (float value01) noexcept { overrideValue = value01; }   // < 0 disables

    // Fills outL/outR with control values in [-1, 1].
    void render (float* outL, float* outR, int numSamples) noexcept;

    double getFrequency() const noexcept { return rateHz; }
    double getPhase (int ch) const noexcept { return phase[(size_t) ch]; }

private:
    static double baseShape (int index, double phase, unsigned cycle) noexcept;
    double evaluate (double phase, unsigned cycle) const noexcept;

    double sr = 44100.0;
    double rateHz = 4.0;
    double phase[2] = { 0.0, 0.0 };
    unsigned long cycleCount[2] = { 0, 0 };
    double phaseInc = 0.0;
    double phaseOffset = 0.0;
    double widthOffset = 0.0;

    bool invert = false;
    bool harmMode = false;
    float shapePos = 5.0f;
    float customAmt = 0.0f;
    float stepAmt = 0.0f;
    float smoothAmt = 0.0f;
    float overrideValue = -1.0f;
    float smoothCoeff = 0.0f;
    float smoothed[2] = { 0.0f, 0.0f };

    const LfoProgram* program = nullptr;
    LfoProgram fallback;
};

} // namespace wob
