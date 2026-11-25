#include "Follower.h"
#include "Params.h"

#include <cmath>

namespace wob
{

void Follower::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Follower::reset() noexcept
{
    eq.reset();
    rmsAccum = 0.0f;
    envelope = 0.0f;
    value = 0.0f;
    eqNeedsUpdate = true;
}

void Follower::update (float attackMs, float releaseMs, float rmsMs, float maxLevelDb,
                       int newEqType, float newEqFreq, float newEqGainDb, float newEqQ) noexcept
{
    attackCoeff  = onePoleCoeff (clampf (attackMs, 0.0f, 10000.0f), sr);
    releaseCoeff = onePoleCoeff (clampf (releaseMs, 0.0f, 10000.0f), sr);
    rmsCoeff     = onePoleCoeff (clampf (rmsMs, 0.0f, 100.0f), sr);

    maxLevelLinear = dbToGainF (clampf (maxLevelDb, -80.0f, 0.0f));

    eqType = newEqType;
    eqFreq = newEqFreq;
    eqGainDb = newEqGainDb;
    eqQ = newEqQ;

    // 0 dB max level means "full scale", but an exact 1.0 divisor would make the
    // follower sit at zero for anything below clipping, so keep a small floor.
    maxLevelLinear = std::max (maxLevelLinear, 1.0e-4f);
    eqNeedsUpdate = true;
}

float Follower::process (const float* left, const float* right, int numSamples) noexcept
{
    if (eqNeedsUpdate)
    {
        switch (eqType)
        {
            case 1: eq.setLowPass  ((double) eqFreq, (double) eqQ, sr); break;
            case 2: eq.setHighPass ((double) eqFreq, (double) eqQ, sr); break;
            case 3: eq.setBandPass ((double) eqFreq, (double) eqQ, sr); break;
            case 4: eq.setLowShelf ((double) eqFreq, (double) eqQ, (double) eqGainDb, sr); break;
            case 5: eq.setHighShelf ((double) eqFreq, (double) eqQ, (double) eqGainDb, sr); break;
            case 6: eq.setBell ((double) eqFreq, (double) eqQ, (double) eqGainDb, sr); break;
            default: break;
        }

        eqNeedsUpdate = false;
    }

    for (int n = 0; n < numSamples; ++n)
    {
        const double inL = (double) left[n];
        const double inR = right != nullptr ? (double) right[n] : inL;

        double mono = 0.5 * (inL + inR);

        if (eqType != 0)
            mono = eq.process (0, mono);

        const float rectified = (float) std::fabs (mono);
        const float squared = rectified * rectified;

        rmsAccum = squared + rmsCoeff * (rmsAccum - squared);

        const float detected = std::sqrt (std::max (0.0f, rmsAccum));
        const float coeff = detected > envelope ? attackCoeff : releaseCoeff;

        envelope = detected + coeff * (envelope - detected);
    }

    value = clampf (envelope / maxLevelLinear, 0.0f, 1.0f);

    return value;
}

} // namespace wob
