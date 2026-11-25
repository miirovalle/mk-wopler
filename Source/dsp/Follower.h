#pragma once

#include "DspUtil.h"

namespace wob
{

// Input level follower with attack / release ballistics, RMS averaging, a
// side-chain equaliser and an optional external side-chain input.
class Follower
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    void update (float attackMs, float releaseMs, float rmsMs, float maxLevelDb,
                 int eqType, float eqFreq, float eqGainDb, float eqQ) noexcept;

    // Processes a block and returns the current normalised control value (0..1).
    float process (const float* left, const float* right, int numSamples) noexcept;

    float getValue() const noexcept { return value; }

private:
    double sr = 44100.0;

    float attackCoeff = 0.0f, releaseCoeff = 0.0f, rmsCoeff = 0.0f;
    float maxLevelLinear = 1.0f;
    float rmsAccum = 0.0f;
    float envelope = 0.0f;
    float value = 0.0f;

    int eqType = 0;
    Svf eq;
    float eqGainDb = 0.0f;
    float eqFreq = 1000.0f, eqQ = 0.707f;
    bool eqNeedsUpdate = true;
};

} // namespace wob
