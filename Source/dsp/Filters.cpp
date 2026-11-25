#include "Filters.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wob
{

//==============================================================================
namespace
{
constexpr float kLowPassSlopes[]  = { 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 36.0f, 48.0f,
                                      60.0f, 72.0f, 84.0f, 96.0f, 108.0f, 120.0f };
constexpr float kQuadSlopes[] = { 12.0f, 24.0f, 36.0f, 48.0f };

inline double butterworthQ (int order, int k) noexcept
{
    const double theta = kPi * ((double) k - 0.5) / (double) order;
    return 1.0 / (2.0 * std::sin (theta));
}

// Resonance is a multiplicative Q factor: 0% leaves a maximally flat cascade,
// 100% drives every section to the edge of self-oscillation. The squared curve
// keeps the lower half of the control range gentle, which matches how the
// reference plugin behaves around its 40 % default.
inline double qMulFromResonance (double resonancePercent) noexcept
{
    const double r = clampd (resonancePercent, 0.0, 100.0) * 0.01;
    return std::pow (24.0, r * r);
}

struct Vowel
{
    double f[5];
    double g[5];
    double q[5];
};

const Vowel kVowels[5] =
{
    { { 730.0, 1090.0, 2440.0, 3400.0, 3500.0 }, { 1.00, 0.63, 0.40, 0.25, 0.16 }, { 9.0, 11.0, 14.0, 16.0, 18.0 } }, // A
    { { 530.0, 1840.0, 2480.0, 3500.0, 4000.0 }, { 1.00, 0.50, 0.35, 0.22, 0.14 }, { 9.0, 12.0, 14.0, 16.0, 18.0 } }, // E
    { { 270.0, 2290.0, 3010.0, 3500.0, 4000.0 }, { 1.00, 0.42, 0.30, 0.20, 0.13 }, { 8.0, 12.0, 14.0, 16.0, 18.0 } }, // I
    { { 570.0,  840.0, 2410.0, 3300.0, 3500.0 }, { 1.00, 0.68, 0.32, 0.20, 0.13 }, { 9.0, 10.0, 14.0, 16.0, 18.0 } }, // O
    { { 300.0,  870.0, 2240.0, 3300.0, 3400.0 }, { 1.00, 0.55, 0.28, 0.18, 0.12 }, { 8.0, 10.0, 14.0, 16.0, 18.0 } }  // U
};

// Deterministic pseudo random weights for the polymorph kernels.
inline double hashWeight (int variant, int slot) noexcept
{
    auto h = (unsigned) (variant * 2654435761u + (unsigned) slot * 40503u + 12345u);
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (double) (h % 1000u) / 999.0;
}
} // namespace

//==============================================================================
const FilterTypeInfo& filterTypeInfo (int index) noexcept
{
    static const std::array<FilterTypeInfo, kNumFilterTypes> table = []
    {
        std::array<FilterTypeInfo, kNumFilterTypes> a {};
        int i = 0;

        for (auto s : kLowPassSlopes)  a[(size_t) i++] = { FilterFamily::LowPass,  s, 0 };
        for (auto s : kLowPassSlopes)  a[(size_t) i++] = { FilterFamily::HighPass, s, 0 };

        for (int v = 0; v < 2; ++v) a[(size_t) i++] = { FilterFamily::BandPass, kQuadSlopes[v], 0 };
        a[(size_t) i++] = { FilterFamily::BandPass, 36.0f, 0 };
        a[(size_t) i++] = { FilterFamily::BandPass, 48.0f, 0 };

        a[(size_t) i++] = { FilterFamily::Notch, 12.0f, 0 };
        a[(size_t) i++] = { FilterFamily::Notch, 24.0f, 0 };

        a[(size_t) i++] = { FilterFamily::Peak, 12.0f, 0 };
        a[(size_t) i++] = { FilterFamily::Peak, 24.0f, 0 };

        a[(size_t) i++] = { FilterFamily::LowShelf,  12.0f, 0 };
        a[(size_t) i++] = { FilterFamily::LowShelf,  24.0f, 0 };
        a[(size_t) i++] = { FilterFamily::HighShelf, 12.0f, 0 };
        a[(size_t) i++] = { FilterFamily::HighShelf, 24.0f, 0 };

        a[(size_t) i++] = { FilterFamily::Fade, 24.0f, 0 };
        a[(size_t) i++] = { FilterFamily::Fade, 24.0f, 1 };
        a[(size_t) i++] = { FilterFamily::Fade, 24.0f, 2 };

        for (int v = 0; v < 4; ++v) a[(size_t) i++] = { FilterFamily::Harmonics, 12.0f, v };
        for (int v = 0; v < 4; ++v) a[(size_t) i++] = { FilterFamily::SubX, 24.0f, v };

        for (int v = 0; v < 6; ++v) a[(size_t) i++] = { FilterFamily::Formant, 12.0f, v };
        for (int v = 0; v < 8; ++v) a[(size_t) i++] = { FilterFamily::Comb, 12.0f, v };
        for (int v = 0; v < 8; ++v) a[(size_t) i++] = { FilterFamily::Diffuse, 12.0f, v };
        for (int v = 0; v < 20; ++v) a[(size_t) i++] = { FilterFamily::Polymorph, 12.0f, v };
        for (int v = 0; v < 7; ++v) a[(size_t) i++] = { FilterFamily::Scream, 12.0f, v };

        return a;
    }();

    index = index < 0 ? 0 : (index >= kNumFilterTypes ? kNumFilterTypes - 1 : index);
    return table[(size_t) index];
}

bool filterTypeUsesCharacter (int index) noexcept
{
    switch (filterTypeInfo (index).family)
    {
        case FilterFamily::LowPass:
        case FilterFamily::HighPass:
        case FilterFamily::BandPass:
        case FilterFamily::Notch:
        case FilterFamily::Peak:
        case FilterFamily::LowShelf:
        case FilterFamily::HighShelf:
            return false;
        default:
            return true;
    }
}

bool filterTypeUsesGain (int index) noexcept
{
    switch (filterTypeInfo (index).family)
    {
        case FilterFamily::LowPass:
        case FilterFamily::HighPass:
        case FilterFamily::BandPass:
        case FilterFamily::Notch:
        case FilterFamily::Fade:
            return false;
        default:
            return true;
    }
}

//==============================================================================
void FilterInstance::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (auto& line : combLines)
        line.prepare (kCombCapacity);

    for (auto& line : diffuserLines)
        line.prepare (kDiffuserCapacity);

    reset();
}

void FilterInstance::reset() noexcept
{
    for (auto& s : stages)  s.reset();
    for (auto& s : stagesB) s.reset();
    for (auto& s : bank)    s.reset();

    onePole.reset();

    for (auto& line : combLines) line.reset();
    for (auto& line : diffuserLines) line.reset();

    for (auto& d : diffusers) d.reset();

    screamFeedback = { 0.0, 0.0 };
    numStages = numStagesB = bankSize = numDiffusers = 0;
}

void FilterInstance::setType (int newType) noexcept
{
    newType = newType < 0 ? 0 : (newType >= kNumFilterTypes ? kNumFilterTypes - 1 : newType);

    if (newType == type)
        return;

    type = newType;
    info = filterTypeInfo (type);
    reset();
    setSettings (current[0], current[1]);
}

//==============================================================================
void FilterInstance::setSettings (const FilterSettings& leftSettings, const FilterSettings& rightSettings) noexcept
{
    current[0] = leftSettings;
    current[1] = rightSettings;

    for (setupCh = 0; setupCh < 2; ++setupCh)
    {
    const FilterSettings& s = current[(size_t) setupCh];
    const double cutoff = clampd (s.freq, 10.0, sr * 0.47);
    const double qMul = qMulFromResonance (s.resonance);
    const double charAmount = clampd (s.character * 0.01, 0.0, 1.0);
    const double quality = clampd (s.quality * 0.01, 0.0, 1.0);
    const int partials = std::max (2, std::min (kMaxPartials, 2 + (int) std::lround (quality * (kMaxPartials - 2))));

    switch (info.family)
    {
        case FilterFamily::LowPass:
        case FilterFamily::HighPass:
        case FilterFamily::BandPass:
        case FilterFamily::Notch:
        case FilterFamily::Peak:
        case FilterFamily::LowShelf:
        case FilterFamily::HighShelf:
            setupCascade (info.family, cutoff, qMul, (double) s.gainDb);
            break;

        case FilterFamily::Fade:
        {
            numStages = 2;
            numStagesB = 2;
            stages[0].setLowPassCh (setupCh, cutoff, 0.7071 * qMul, sr);
            stages[1].setLowPassCh (setupCh, cutoff, 0.7071, sr);
            stagesB[0].setHighPassCh (setupCh, cutoff, 0.7071 * qMul, sr);
            stagesB[1].setHighPassCh (setupCh, cutoff, 0.7071, sr);
            bank[0].setBandPassCh (setupCh, cutoff, 1.0 * qMul, sr);
            bank[1].setNotchCh (setupCh, cutoff, 0.7071 * qMul, sr);
            break;
        }

        case FilterFamily::Harmonics:
        {
            const bool linear = (info.variant == 1 || info.variant == 3);
            const bool swap = (info.variant >= 2);
            setupHarmonics (cutoff, qMul, (double) s.gainDb, charAmount, linear, swap);
            break;
        }

        case FilterFamily::SubX:
        {
            numStages = 2;

            if (info.variant == 2)       // Over-X
            {
                stages[0].setHighPassCh (setupCh, cutoff, 0.7071 * qMul, sr);
                stages[1].setHighPassCh (setupCh, cutoff, 0.7071, sr);
            }
            else if (info.variant == 3)  // Band-X
            {
                stages[0].setBandPassCh (setupCh, cutoff, 0.7071 * qMul, sr);
                stages[1].setBandPassCh (setupCh, cutoff, 0.7071, sr);
            }
            else
            {
                stages[0].setLowPassCh (setupCh, cutoff, 0.7071 * qMul, sr);
                stages[1].setLowPassCh (setupCh, cutoff, 0.7071, sr);
            }

            const int n = std::min (partials, 16);
            const double gainDb = (double) s.gainDb;

            for (int k = 0; k < n; ++k)
            {
                const double ratio = info.variant == 2 ? std::pow (2.0, k * 0.5)
                                   : info.variant == 3 ? 1.0 + 0.5 * k
                                   : std::pow (2.0, -0.5 * k);

                const double f = cutoff * ratio;

                if (f >= sr * 0.45)
                {
                    bank[k].setBellCh (setupCh, sr * 0.44, 1.0, 0.0, sr);
                    bankGain[(size_t) k] = 0.0;
                    continue;
                }

                const double weight = std::pow (charAmount, (double) k);
                bank[k].setBellCh (setupCh, f, 4.0 + 8.0 * charAmount, (9.0 + gainDb) * weight, sr);
                bankGain[(size_t) k] = 1.0;
            }

            bankSize = n;
            break;
        }

        case FilterFamily::Formant:
            setupFormants (cutoff, charAmount, info.variant);
            break;

        case FilterFamily::Comb:
            if (setupCh == 0)
                setupComb (cutoff, charAmount, info.variant);
            break;

        case FilterFamily::Diffuse:
            if (setupCh == 0)
                setupDiffuse (cutoff, charAmount, info.variant);
            break;

        case FilterFamily::Polymorph:
            setupPolymorph (cutoff, qMul, (double) s.gainDb, charAmount, info.variant);
            break;

        case FilterFamily::Scream:
            setupScream (cutoff, qMul, (double) s.gainDb, charAmount, info.variant);
            break;
    }
    }

    setupCh = 0;
}

//==============================================================================
void FilterInstance::setupCascade (FilterFamily family, double cutoff, double qMul, double gainDb) noexcept
{
    int slopeSteps = (int) std::lround (info.orderDb / 12.0f);

    if (info.orderDb <= 6.0f)
    {
        numStages = 0;
        onePole.setCutoffCh (setupCh, cutoff, sr);
        return;
    }

    numStages = std::max (1, std::min (kMaxStages, slopeSteps));
    const int order = numStages * 2;

    for (int k = 1; k <= numStages; ++k)
    {
        const double bwQ = butterworthQ (order, k);

        switch (family)
        {
            case FilterFamily::LowPass:
                stages[(size_t) (k - 1)].setLowPassCh (setupCh, cutoff, bwQ * qMul, sr);
                break;
            case FilterFamily::HighPass:
                stages[(size_t) (k - 1)].setHighPassCh (setupCh, cutoff, bwQ * qMul, sr);
                break;
            case FilterFamily::BandPass:
                stages[(size_t) (k - 1)].setBandPassCh (setupCh, cutoff, bwQ * qMul, sr);
                break;
            case FilterFamily::Notch:
                stages[(size_t) (k - 1)].setNotchCh (setupCh, cutoff, bwQ * qMul, sr);
                break;
            case FilterFamily::Peak:
                // A bell is a single section; further stages sharpen the band.
                stages[(size_t) (k - 1)].setBellCh (setupCh, cutoff, std::max (0.3, 1.0 * qMul), gainDb, sr);
                break;
            case FilterFamily::LowShelf:
                stages[(size_t) (k - 1)].setLowShelfCh (setupCh, cutoff, (k == 1 ? std::max (0.4, 0.7071 * qMul) : 0.7071), gainDb, sr);
                break;
            case FilterFamily::HighShelf:
                stages[(size_t) (k - 1)].setHighShelfCh (setupCh, cutoff, (k == 1 ? std::max (0.4, 0.7071 * qMul) : 0.7071), gainDb, sr);
                break;
            default:
                break;
        }
    }
}

void FilterInstance::setupHarmonics (double fundamental, double qMul, double gainDb,
                                     double charAmount, bool linear, bool swap) noexcept
{
    const int count = std::max (2, std::min (kMaxPartials,
                                             (int) std::lround (4.0 + current[(size_t) setupCh].quality * 0.28)));
    const double base = 9.0 + gainDb;
    const double q = 2.0 + qMul * 0.5;

    bankSize = count;

    for (int k = 0; k < count; ++k)
    {
        const double ratio = linear ? (double) (k + 1) : std::pow (2.0, (double) k);
        const double f = fundamental * ratio;

        if (f >= sr * 0.45)
        {
            bank[(size_t) k].setBellCh (setupCh, sr * 0.44, 1.0, 0.0, sr);
            bankGain[(size_t) k] = 0.0;
            continue;
        }

        // Character sets how quickly the upper harmonics fall away: 0% leaves
        // only the fundamental, 100% gives every harmonic the same gain.
        double g = base * std::pow (charAmount, (double) k);

        if (swap && (k % 2 == 1))
            g = -g;

        bank[(size_t) k].setBellCh (setupCh, f, q, g, sr);
        bankGain[(size_t) k] = 1.0;
    }
}

void FilterInstance::setupFormants (double cutoff, double charAmount, int variant) noexcept
{
    const double shift = clampd (cutoff / 800.0, 0.12, 6.0);
    double f[5] {}, g[5] {}, q[5] {};

    if (variant >= 5)
    {
        // A-E-I-O-U morph: character sweeps through the five vowels.
        const double pos = clampd (charAmount, 0.0, 1.0) * 4.0;
        const int v0 = std::min (4, (int) std::floor (pos));
        const int v1 = std::min (4, v0 + 1);
        const double t = pos - (double) v0;

        for (int k = 0; k < 5; ++k)
        {
            f[k] = kVowels[v0].f[k] + t * (kVowels[v1].f[k] - kVowels[v0].f[k]);
            g[k] = kVowels[v0].g[k] + t * (kVowels[v1].g[k] - kVowels[v0].g[k]);
            q[k] = kVowels[v0].q[k] + t * (kVowels[v1].q[k] - kVowels[v0].q[k]);
        }
    }
    else
    {
        for (int k = 0; k < 5; ++k)
        {
            f[k] = kVowels[variant].f[k];
            g[k] = kVowels[variant].g[k];
            q[k] = kVowels[variant].q[k];
        }

        // Character emphasises (or suppresses) the upper formants.
        for (int k = 1; k < 5; ++k)
            g[k] *= std::pow (charAmount, (double) k);
    }

    bankSize = kMaxFormants;

    for (int k = 0; k < kMaxFormants; ++k)
    {
        const double freq = clampd (f[k] * shift, 20.0, sr * 0.45);
        const double gainDb = current[(size_t) setupCh].gainDb + 18.0 * g[k];
        bank[(size_t) k].setBellCh (setupCh, freq, std::max (0.5, q[k]), gainDb, sr);
        bankGain[(size_t) k] = 1.0;
    }
}

void FilterInstance::setupComb (double cutoff, double charAmount, int variant) noexcept
{
    const double delay = clampd (sr / std::max (cutoff, 20.0), 2.0, (double) kCombCapacity - 4.0);

    const double baseFeedback = clampd (charAmount * 0.94, 0.0, 0.94);

    // Each variant is a different comb kernel; all are kept stable by clamping
    // the loop gain below one and by damping the feedback path.
    static constexpr int kTapCount[8] = { 1, 1, 2, 2, 3, 3, 2, 3 };
    static constexpr double kTapMul[8][3] =
    {
        { 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 1.0, 2.0, 0.0 }, { 1.0, 1.5, 0.0 },
        { 1.0, 2.0, 3.0 }, { 1.0, 1.34, 1.78 }, { 1.0, 2.5, 0.0 }, { 1.0, 1.5, 2.5 }
    };

    numTaps = kTapCount[variant];

    for (int k = 0; k < numTaps; ++k)
    {
        combTaps[(size_t) k] = std::min ((double) kCombCapacity - 4.0, delay * kTapMul[variant][k]);
        combTapGain[(size_t) k] = std::pow (baseFeedback, (double) (k + 1)) * (k == 0 ? 1.0 : 0.6);
    }

    combFeedback = baseFeedback * (variant == 1 ? -1.0 : 1.0);

    double dampHz = cutoff * 2.0;

    if (variant >= 6)
        dampHz *= 0.5;

    combDamp.setCutoff (dampHz, sr);
}

void FilterInstance::setupDiffuse (double cutoff, double charAmount, int variant) noexcept
{
    static constexpr int kPrimes[8] = { 1009, 1319, 1499, 1709, 1931, 2213, 2437, 2711 };

    numDiffusers = 2 + variant;   // 2 .. 9, clamped below
    numDiffusers = std::min (kMaxDiffusers, numDiffusers);

    const double scale = clampd (sr / std::max (cutoff, 20.0), 3.0, 900.0);
    const double g = clampd (charAmount * 0.85, 0.0, 0.85);

    for (int k = 0; k < numDiffusers; ++k)
    {
        diffuserDelay[(size_t) k] = clampd (scale * ((double) kPrimes[k] / 1009.0) * (1.0 + 0.1 * (double) variant),
                                            4.0, (double) kDiffuserCapacity - 4.0);
        diffuserGain[(size_t) k] = g * (0.6 + 0.4 * ((double) (k + 1) / (double) numDiffusers));
    }
}

void FilterInstance::setupPolymorph (double cutoff, double qMul, double gainDb,
                                     double charAmount, int variant) noexcept
{
    // Two anchor structures per variant, morphed by Character. This keeps the
    // "structure changes with character" behaviour while remaining predictable.
    double wa[4], wb[4];

    for (int k = 0; k < 4; ++k)
    {
        wa[k] = hashWeight (variant, k);
        wb[k] = hashWeight (variant + 101, k);
    }

    const double c = clampd (charAmount, 0.0, 1.0);
    const double w[4] =
    {
        (1.0 - c) * wa[0] + c * wb[0],
        (1.0 - c) * wa[1] + c * wb[1],
        (1.0 - c) * wa[2] + c * wb[2],
        (1.0 - c) * wa[3] + c * wb[3]
    };

    const double sum = std::max (1.0e-6, w[0] + w[1] + w[2] + w[3]);
    const double spread = 0.35 + 2.2 * charAmount;

    bankSize = 4;
    bank[0].setLowPassCh  (setupCh, cutoff, std::max (0.4, qMul), sr);
    bank[1].setHighPassCh (setupCh, cutoff, std::max (0.4, qMul), sr);
    bank[2].setBandPassCh (setupCh, cutoff * spread, std::max (0.6, qMul), sr);
    bank[3].setBellCh     (setupCh, cutoff * (1.0 / spread), std::max (0.6, qMul), gainDb + 6.0, sr);

    for (int k = 0; k < 4; ++k)
        bankGain[(size_t) k] = w[k] / sum * 2.0;
}

void FilterInstance::setupScream (double cutoff, double qMul, double gainDb,
                                  double charAmount, int variant) noexcept
{
    const int sections = 1 + variant;   // 1 .. 7
    bankSize = std::min (kMaxPartials, sections);

    for (int k = 0; k < bankSize; ++k)
    {
        const double ratio = 1.0 + 0.35 * (double) k;
        const double f = clampd (cutoff * ratio, 20.0, sr * 0.45);
        const double q = clampd (3.0 + 9.0 * qMul * 0.2, 0.5, 30.0);
        const double g = (12.0 + gainDb) * std::pow (0.75 + 0.5 * charAmount, (double) k);

        bank[(size_t) k].setBellCh (setupCh, f, q, g, sr);
        bankGain[(size_t) k] = 1.0;
    }
}

//==============================================================================
void FilterInstance::process (float* left, float* right, int numSamples) noexcept
{
    switch (info.family)
    {
        case FilterFamily::LowPass:
        case FilterFamily::HighPass:
        case FilterFamily::BandPass:
        case FilterFamily::Notch:
        case FilterFamily::Peak:
        case FilterFamily::LowShelf:
        case FilterFamily::HighShelf:
            processCascade (left, right, numSamples, info.family);
            break;

        case FilterFamily::Fade:
        {
            const double c = clampd (current[0].character * 0.01, 0.0, 1.0);

            for (int n = 0; n < numSamples; ++n)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    double x = (double) (ch == 0 ? left[n] : right[n]);

                    double lp = x;
                    for (int s = 0; s < numStages; ++s)
                        lp = stages[(size_t) s].process (ch, lp);

                    double hp = x;
                    for (int s = 0; s < numStagesB; ++s)
                        hp = stagesB[(size_t) s].process (ch, hp);

                    double y;

                    if (info.variant == 1)
                    {
                        const double bp = bank[0].process (ch, x);
                        const double wLP = std::max (0.0, 1.0 - 2.0 * c);
                        const double wBP = 1.0 - std::fabs (2.0 * c - 1.0);
                        const double wHP = std::max (0.0, 2.0 * c - 1.0);
                        y = wLP * lp + wBP * bp + wHP * hp;
                    }
                    else if (info.variant == 2)
                    {
                        const double nt = bank[1].process (ch, x);
                        y = (1.0 - c) * lp + c * nt;
                    }
                    else
                    {
                        y = (1.0 - c) * lp + c * hp;
                    }

                    if (ch == 0) left[n] = (float) sanitise (y);
                    else         right[n] = (float) sanitise (y);
                }
            }

            break;
        }

        case FilterFamily::Harmonics:
            processParallel (left, right, numSamples);
            break;

        case FilterFamily::SubX:
            // The cascaded section provides the filter "body" and the harmonic
            // bank adds the character-controlled partials on top.
            processCascade (left, right, numSamples, FilterFamily::LowPass);
            processParallel (left, right, numSamples, 1.0, info.variant == 1);
            break;

        case FilterFamily::Formant:
        case FilterFamily::Polymorph:
        case FilterFamily::Scream:
            processParallel (left, right, numSamples);
            break;

        case FilterFamily::Comb:
        {
            for (int n = 0; n < numSamples; ++n)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double x = (double) (ch == 0 ? left[n] : right[n]);

                    double fb = 0.0;

                    for (int t = 0; t < numTaps; ++t)
                        fb += combTapGain[(size_t) t] * combLines[(size_t) t].read (ch, combTaps[(size_t) t]);

                    fb = combDamp.lowPass (ch, fb);

                    const double y = x + combFeedback * fb;
                    combLines[0].write (ch, y);

                    for (int t = 1; t < numTaps; ++t)
                        combLines[(size_t) t].write (ch, y);

                    if (ch == 0) left[n] = (float) sanitise (y);
                    else         right[n] = (float) sanitise (y);
                }

                for (auto& line : combLines)
                    line.advance();
            }

            break;
        }

        case FilterFamily::Diffuse:
        {
            for (int n = 0; n < numSamples; ++n)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    double y = (double) (ch == 0 ? left[n] : right[n]);

                    for (int k = 0; k < numDiffusers; ++k)
                        y = diffusers[(size_t) k].process (ch, y, diffuserGain[(size_t) k],
                                                           diffuserDelay[(size_t) k], diffuserLines[(size_t) k]);

                    if (ch == 0) left[n] = (float) sanitise (y);
                    else         right[n] = (float) sanitise (y);
                }

                for (int k = 0; k < numDiffusers; ++k)
                    diffuserLines[(size_t) k].advance();
            }

            break;
        }
    }
}

void FilterInstance::processCascade (float* left, float* right, int numSamples, FilterFamily family) noexcept
{
    const bool useOnePole = (info.orderDb <= 6.0f) && numStages == 0;

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            double x = (double) (ch == 0 ? left[n] : right[n]);
            double y = x;

            if (useOnePole)
                y = (family == FilterFamily::HighPass) ? onePole.highPass (ch, x) : onePole.lowPass (ch, x);
            else
                for (int s = 0; s < numStages; ++s)
                    y = stages[(size_t) s].process (ch, y);

            if (ch == 0) left[n] = (float) sanitise (y);
            else         right[n] = (float) sanitise (y);
        }
    }
}

void FilterInstance::processParallel (float* left, float* right, int numSamples,
                                      double inputScale, bool softSaturate) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            double x = (double) (ch == 0 ? left[n] : right[n]);
            double acc = 0.0;

            for (int k = 0; k < bankSize; ++k)
                acc += bankGain[(size_t) k] * bank[(size_t) k].process (ch, x);

            if (softSaturate)
                acc = softClip (acc);

            if (inputScale != 1.0)
                x *= inputScale;

            const double y = x + acc;

            if (ch == 0) left[n] = (float) sanitise (y);
            else         right[n] = (float) sanitise (y);
        }
    }
}

} // namespace wob
