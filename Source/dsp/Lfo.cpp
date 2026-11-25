#include "Lfo.h"

#include <cmath>

namespace wob
{

void Lfo::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Lfo::reset (double phase01) noexcept
{
    phase[0] = phase[1] = phase01;
    smoothed[0] = smoothed[1] = 0.0f;
}

void Lfo::update (float rate, bool sync, int noteLength, double bpm, float phase01,
                  float widthPercent, bool inv, float shapePosition, float customPercent,
                  float stepPercent, float smoothPercent, bool harmonicsMode) noexcept
{
    rate = clampf (rate, 0.001f, 400.0f);

    if (sync)
    {
        const double beats = noteLengthBeats (noteLength);
        const double bpmClamped = bpm > 1.0 ? bpm : 120.0;
        rateHz = (bpmClamped / 60.0) / std::max (1.0e-6, beats);
    }
    else
    {
        rateHz = (double) rate;
    }

    phaseInc = rateHz / sr;

    invert = inv;
    harmMode = harmonicsMode;
    shapePos = clampf (shapePosition, 0.0f, (float) (kNumShapes - 1));
    customAmt = clampf (customPercent, 0.0f, 100.0f) * 0.01f;
    stepAmt = clampf (stepPercent, 0.0f, 100.0f) * 0.01f;
    smoothAmt = clampf (smoothPercent, 0.0f, 100.0f) * 0.01f;

    phaseOffset = (double) clampf (phase01, 0.0f, 100.0f) * 0.01;
    widthOffset = (double) clampf (widthPercent, -100.0f, 100.0f) * 0.01;

    // Smoothing: up to ~30 % of one period at maximum.
    const double smoothSeconds = smoothAmt * 0.25 / std::max (0.01, rateHz);
    smoothCoeff = smoothSeconds <= 1.0e-6 ? 0.0f
                                          : (float) std::exp (-1.0 / (smoothSeconds * sr));
}

//==============================================================================
double Lfo::baseShape (int index, double p, unsigned cycle) noexcept
{
    auto sineExp = [p] (double exponent, double shift)
    {
        const double s = std::sin (kTwoPi * (p + shift));
        const double mag = std::pow (std::fabs (s), exponent);
        return s < 0.0 ? -mag : mag;
    };

    auto hash01 = [] (unsigned x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return (double) (x & 0xffffffu) / (double) 0xffffffu;
    };

    switch (index)
    {
        case 0:  // exponential
        {
            const double e = (std::exp (4.0 * p) - 1.0) / (std::exp (4.0) - 1.0);
            return e * 2.0 - 1.0;
        }
        case 1:  return 1.0 - 4.0 * std::fabs (p - 0.5);              // triangle
        case 2:  return sineExp (1.0 / 8.0, 0.0);                     // sine power 8
        case 3:  return sineExp (1.0 / 4.0, 0.0);                     // sine power 4
        case 4:  return sineExp (2.0, 0.0);                           // sine square
        case 5:  return sineExp (1.0, 0.0);                           // sine
        case 6:  // harmonics
        {
            double v = 0.0;
            for (int h = 1; h <= 4; ++h)
                v += std::sin (kTwoPi * p * h) / (double) h;
            return clampd (v * 0.62, -1.0, 1.0);
        }
        case 7:  // more harmonics
        {
            double v = 0.0;
            for (int h = 1; h <= 10; ++h)
                v += std::sin (kTwoPi * p * h) / (double) h;
            return clampd (v * 0.62, -1.0, 1.0);
        }
        case 8:  // disharmonics
        {
            static constexpr double ratios[4] = { 1.0, 2.7, 5.3, 8.1 };
            double v = 0.0;
            for (int h = 0; h < 4; ++h)
                v += std::sin (kTwoPi * p * ratios[h] + 0.37 * h) / (double) (h + 1);
            return clampd (v * 0.62, -1.0, 1.0);
        }
        case 9:  return sineExp (0.5, 0.0);                           // sine square root
        case 10: return sineExp (0.25, 0.25);                         // sine 4 root
        case 11: return std::sin (kTwoPi * p) >= 0.0 ? 1.0 : -1.0;    // rectangle
        case 12:                                                      // rect-saw
            return clampd (0.5 * (std::sin (kTwoPi * p) >= 0.0 ? 1.0 : -1.0) + 0.5 * (2.0 * p - 1.0), -1.0, 1.0);
        case 13: return 2.0 * p - 1.0;                                // saw
        case 14: return hash01 (cycle * 2654435761u) * 2.0 - 1.0;     // noise
        case 15:                                                      // mess
        {
            double v = 0.0;
            for (int h = 1; h <= 8; ++h)
                v += std::sin (kTwoPi * p * h + hash01 (h * 7919u + 13u) * 6.283) / (double) h;

            return clampd (v * 0.7, -1.0, 1.0);
        }
        default: return 0.0;
    }
}

double Lfo::evaluate (double p, unsigned cycle) const noexcept
{
    if (harmMode && program != nullptr)
    {
        double v = 0.0;
        double norm = 0.0;

        for (int h = 1; h <= LfoProgram::kMaxHarmonics; ++h)
        {
            const double level = program->harmLevel[(size_t) (h - 1)];
            norm += level;

            if (level > 0.0)
                v += level * std::sin (kTwoPi * p * (double) h
                                       + (double) program->harmPhase[(size_t) (h - 1)] * kTwoPi);
        }

        return norm > 1.0e-6 ? clampd (v / norm * 1.4, -1.0, 1.0) : 0.0;
    }

    const int i0 = (int) std::floor (shapePos);
    const int i1 = std::min (kNumShapes - 1, i0 + 1);
    const double frac = (double) shapePos - (double) i0;

    double v = baseShape (i0, p, cycle) * (1.0 - frac) + baseShape (i1, p, cycle) * frac;

    if (customAmt > 0.0f && program != nullptr)
    {
        const double pos = p * (double) (LfoProgram::kCurvePoints - 1);
        const int c0 = (int) std::floor (pos);
        const int c1 = std::min (LfoProgram::kCurvePoints - 1, c0 + 1);
        const double cf = pos - (double) c0;
        const double custom = (double) program->customShape[(size_t) c0] * (1.0 - cf)
                            + (double) program->customShape[(size_t) c1] * cf;

        v = v * (1.0 - (double) customAmt) + (custom * 2.0 - 1.0) * (double) customAmt;
    }

    if (stepAmt > 0.0f && program != nullptr)
    {
        const int steps = std::max (1, std::min (LfoProgram::kMaxSteps, program->stepCount));
        const int step = std::min (steps - 1, (int) (p * (double) steps));
        const double stepped = (double) program->steps[(size_t) step] * 2.0 - 1.0;

        v = v * (1.0 - (double) stepAmt) + stepped * (double) stepAmt;
    }

    return invert ? -v : v;
}

//==============================================================================
void Lfo::render (float* outL, float* outR, int numSamples) noexcept
{
    if (overrideValue >= 0.0f)
    {
        const float v = overrideValue * 2.0f - 1.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            outL[n] = v;
            outR[n] = v;
        }

        return;
    }

    for (int n = 0; n < numSamples; ++n)
    {
        double p0 = phase[0] + phaseOffset;
        p0 -= std::floor (p0);

        double p1 = phase[1] + phaseOffset + widthOffset;
        p1 -= std::floor (p1);

        float vL = (float) evaluate (p0, (unsigned) cycleCount[0]);
        float vR = (float) evaluate (p1, (unsigned) cycleCount[1]);

        if (smoothCoeff > 0.0f)
        {
            smoothed[0] = vL + smoothCoeff * (smoothed[0] - vL);
            smoothed[1] = vR + smoothCoeff * (smoothed[1] - vR);
            vL = smoothed[0];
            vR = smoothed[1];
        }

        outL[n] = vL;
        outR[n] = vR;

        phase[0] += phaseInc;
        phase[1] += phaseInc;

        if (phase[0] >= 1.0)
        {
            phase[0] -= 1.0;
            ++cycleCount[0];
        }

        if (phase[1] >= 1.0)
        {
            phase[1] -= 1.0;
            ++cycleCount[1];
        }
    }
}

} // namespace wob
