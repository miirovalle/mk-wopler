#pragma once

#include "DspUtil.h"
#include "Params.h"

#include <array>

namespace wob
{

enum class FilterFamily
{
    LowPass,
    HighPass,
    BandPass,
    Notch,
    Peak,
    LowShelf,
    HighShelf,
    Fade,
    Harmonics,
    SubX,
    OverX,
    BandX,
    Formant,
    Comb,
    Diffuse,
    Polymorph,
    Scream
};

struct FilterTypeInfo
{
    FilterFamily family = FilterFamily::LowPass;
    float orderDb = 12.0f;   // nominal slope in dB/octave (12 dB == 1 biquad)
    int variant = 0;         // per-family variant index
};

const FilterTypeInfo& filterTypeInfo (int index) noexcept;

// True when the "Character" parameter has any effect for this type.
bool filterTypeUsesCharacter (int index) noexcept;

// True when the "Gain" parameter has any effect for this type.
bool filterTypeUsesGain (int index) noexcept;

struct FilterSettings
{
    float freq = 800.0f;
    float resonance = 40.0f;   // %
    float gainDb = 0.0f;
    float character = 50.0f;   // %
    float quality = 50.0f;     // %
};

//==============================================================================
class FilterInstance
{
public:
    static constexpr int kMaxStages = 10;      // 120 dB/oct
    static constexpr int kMaxPartials = 32;
    static constexpr int kMaxFormants = 5;
    static constexpr int kMaxDiffusers = 8;
    static constexpr int kCombCapacity = 4096;
    static constexpr int kDiffuserCapacity = 2048;

    void prepare (double sampleRate);
    void reset() noexcept;

    void setType (int newType) noexcept;
    int getType() const noexcept { return type; }

    void setSettings (const FilterSettings& s) noexcept { setSettings (s, s); }
    void setSettings (const FilterSettings& leftSettings, const FilterSettings& rightSettings) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

private:
    void setupCascade (FilterFamily family, double cutoff, double qMul, double gainDb) noexcept;
    void processCascade (float* left, float* right, int numSamples, FilterFamily family) noexcept;

    void setupHarmonics (double fundamental, double qMul, double gainDb, double charAmount, bool linear, bool swap) noexcept;
    void processParallel (float* left, float* right, int numSamples,
                          double inputScale = 1.0, bool softSaturate = false) noexcept;

    void setupFormants (double cutoff, double charAmount, int variant) noexcept;
    void setupComb (double cutoff, double charAmount, int variant) noexcept;
    void setupDiffuse (double cutoff, double charAmount, int variant) noexcept;
    void setupPolymorph (double cutoff, double qMul, double gainDb, double charAmount, int variant) noexcept;
    void setupScream (double cutoff, double qMul, double gainDb, double charAmount, int variant) noexcept;

    double sr = 44100.0;
    int type = 0;
    FilterTypeInfo info;
    std::array<FilterSettings, 2> current {};
    int setupCh = 0;

    // Serial cascade (LP/HP/BP/Notch/Peak/shelf).
    std::array<Svf, kMaxStages> stages;
    int numStages = 0;
    OnePole onePole;

    // Parallel bank (harmonics / formants / polymorph / scream).
    std::array<Svf, kMaxPartials> bank;
    std::array<double, kMaxPartials> bankGain {};
    int bankSize = 0;

    // Fade family keeps two cascades.
    std::array<Svf, kMaxStages> stagesB;
    int numStagesB = 0;

    // Comb / diffuser.
    std::array<DelayLine, 4> combLines;
    std::array<double, 4> combTaps {};
    std::array<double, 4> combTapGain {};
    int numTaps = 1;
    double combFeedback = 0.0;
    OnePole combDamp;
    std::array<DelayLine, kMaxDiffusers> diffuserLines;
    std::array<AllPass1, kMaxDiffusers> diffusers;
    std::array<double, kMaxDiffusers> diffuserDelay {};
    std::array<double, kMaxDiffusers> diffuserGain {};
    int numDiffusers = 0;

    // Extra state for nonlinear feedback paths.
    std::array<double, 2> screamFeedback {};
    std::array<double, 2> subFeedback {};
    double mixA = 0.0, mixB = 0.0;
};

} // namespace wob
