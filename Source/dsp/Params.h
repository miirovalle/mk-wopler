// MKWOPLER - clean-room reimplementation of the MWobbler distorting-filter
// architecture, based on its published manual. JUCE-free so the DSP can be built
// and tested without a plugin framework.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace wob
{
constexpr int kNumProcessors = 2;
constexpr int kNumBands = 6;
constexpr int kNumCrossovers = kNumBands - 1;
constexpr int kNumFilterTypes = 98;
constexpr int kNumShapes = 16;

//==============================================================================
// Parameter description table. This is the single source of truth used both by
// the AUv3 parameter tree and by the offline test harness.

enum class ParamKind
{
    Float,   // continuous, host displays as float
    Choice,  // discrete index into a named list
    Bool,    // 0 / 1
    Int      // discrete integer value (e.g. oversampling factor)
};

struct ParamSpec
{
    std::string id;
    std::string name;
    std::string unit;
    float minv = 0.0f;
    float maxv = 1.0f;
    float defv = 0.0f;
    float skew = 1.0f;   // 1 = linear, <1 = finer control near min
    ParamKind kind = ParamKind::Float;
    int numChoices = 0;
    std::vector<std::string> choices;

    float normalise (float v) const noexcept;
    float denormalise (float v) const noexcept;
    float clamp (float v) const noexcept { return v < minv ? minv : (v > maxv ? maxv : v); }
};

//==============================================================================
// Globals block.
enum GlobalParam
{
    gpDryWet = 0,
    gpSaturation,
    gpOutputGain,
    gpClipping,
    gpDcFilter,
    gpOversample,
    gpMultiband,
    gpBandCount,
    gpXover1,
    gpXover2,
    gpXover3,
    gpXover4,
    gpXover5,
    gpKeyTrack,
    gpNumGlobalParams
};

// Per-filter-processor block (there are kNumProcessors of them).
enum ProcParam
{
    ppDrive = 0,
    ppDriveMode,
    ppSaturation,
    ppOutGain,
    ppClip,
    ppType,
    ppFreq,
    ppFreqRange,
    ppResonance,
    ppResRange,
    ppGain,
    ppGainRange,
    ppCharacter,
    ppCharRange,
    ppPanorama,
    ppTrim,
    ppQuality,
    ppFDepth,
    ppFAttack,
    ppFRelease,
    ppFRms,
    ppFMaxLevel,
    ppFSidechain,
    ppFEqType,
    ppFEqFreq,
    ppFEqGain,
    ppFEqQ,
    ppLRate,
    ppLWidth,
    ppLOverride,
    ppLInvert,
    ppLSync,
    ppLNoteLength,
    ppLPhase,
    ppLShape,
    ppLCustom,
    ppLStep,
    ppLSmooth,
    ppLHarmonics,
    ppLMidiReset,
    ppNumProcParams
};

// Per-band block (used when the multiband split is enabled).
enum BandParam
{
    bpEnable = 0,
    bpMute,
    bpSolo,
    bpLevel,
    bpPan,
    bpType,
    bpFreq,
    bpFreqRange,
    bpResonance,
    bpResRange,
    bpGain,
    bpCharacter,
    bpDrive,
    bpOutGain,
    bpTrim,
    bpLPhaseOffset,
    bpLRateMult,
    bpLWidth,
    bpNumBandParams
};

//==============================================================================
class ParamTable
{
public:
    static const ParamTable& get();

    const std::vector<ParamSpec>& all() const noexcept { return specs; }

    static constexpr int globalIndex (int g) noexcept { return g; }
    static constexpr int procIndex (int proc, int p) noexcept { return gpNumGlobalParams + proc * ppNumProcParams + p; }
    static constexpr int bandIndex (int b, int p) noexcept
    {
        return gpNumGlobalParams + kNumProcessors * ppNumProcParams + b * bpNumBandParams + p;
    }
    static constexpr int total() noexcept
    {
        return gpNumGlobalParams + kNumProcessors * ppNumProcParams + kNumBands * bpNumBandParams;
    }

    const ParamSpec& operator[] (int i) const noexcept { return specs[(size_t) i]; }

    int indexOf (const std::string& id) const noexcept;
    std::string processorPrefix (int proc) const;

private:
    ParamTable();
    std::vector<ParamSpec> specs;
};

//==============================================================================
// Plain value struct consumed by the engine. Populated by the host wrapper.
struct ProcParams
{
    float drive = 0.0f, driveMode = 0.0f, saturation = 0.0f, outGainDb = 0.0f;
    bool clip = false;
    int type = 0;
    float freq = 800.0f, freqRange = 0.0f;
    float resonance = 40.0f, resRange = 0.0f;
    float gainDb = 0.0f, gainRange = 0.0f;
    float character = 50.0f, charRange = 0.0f;
    float panorama = 0.0f, trim = 0.0f, quality = 50.0f;

    float fDepth = 0.0f, fAttackMs = 10.0f, fReleaseMs = 10.0f, fRmsMs = 1.0f;
    float fMaxLevelDb = 0.0f;
    bool fSidechain = false;
    int fEqType = 0;
    float fEqFreq = 1000.0f, fEqGainDb = 0.0f, fEqQ = 0.707f;

    float lRate = 4.0f, lWidth = 0.0f;
    float lOverride = -2.0f;          // < -1 means "off"
    bool lInvert = false, lSync = false, lHarmonicMode = false, lMidiReset = false;
    int lNoteLength = 4;
    float lPhase = 25.0f;
    float lShape = 5.0f, lCustom = 0.0f, lStep = 0.0f, lSmooth = 0.0f;
};

struct BandParams
{
    bool enable = true, mute = false, solo = false;
    float levelDb = 0.0f, pan = 0.0f;
    int type = -1;                    // -1 = inherit processor type
    float freq = -1.0f;               // < 0 = inherit
    float freqRange = 0.0f;
    float resonance = -1.0f, resRange = 0.0f;
    float gainDb = 0.0f, character = -1.0f, drive = -1.0f, outGainDb = 0.0f;
    float trim = 0.0f;
    float lPhaseOffset = 0.0f, lRateMult = 1.0f, lWidth = 0.0f;
};

struct EngineParams
{
    float dryWet = 100.0f, saturation = 0.0f, outGainDb = 0.0f;
    bool clipping = false, dcFilter = false, multiband = false;
    int bandCount = kNumBands;
    float xover[kNumCrossovers] = { 120.0f, 400.0f, 1200.0f, 3500.0f, 9000.0f };
    float keyTrack = 0.0f;
    ProcParams proc[kNumProcessors];
    BandParams band[kNumBands];
};

//==============================================================================
// Non-automatable oscillator programming (custom curve, step sequencer and
// harmonic spectrum). Kept as plain data so it can be saved into the AU state.
struct LfoProgram
{
    static constexpr int kCurvePoints = 64;
    static constexpr int kMaxSteps = 32;
    static constexpr int kMaxHarmonics = 256;

    std::array<float, kCurvePoints> customShape {};
    std::array<float, kMaxSteps> steps {};
    std::array<float, kMaxHarmonics> harmLevel {};
    std::array<float, kMaxHarmonics> harmPhase {};
    int stepCount = 8;

    LfoProgram();
};

// Note lengths available for host synchronisation (straight / triplet / dotted).
extern const char* const kNoteLengthNames[];
constexpr int kNumNoteLengths = 24;

//==============================================================================
extern const char* const kShapeNames[kNumShapes];
extern const char* const kFilterTypeNames[kNumFilterTypes];
extern const char* const kOversampleNames[3];   // 1x, 2x, 4x  (values 1,2,4)

float dbToGainF (float db) noexcept;
float gainToDbF (float g) noexcept;
float onePoleCoeff (float ms, double sr) noexcept;

// Length of the given note-length index in beats (quarter notes) at 4/4.
float noteLengthBeats (int index) noexcept;

} // namespace wob
