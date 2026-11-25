#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wob
{

//==============================================================================
const char* const kNoteLengthNames[kNumNoteLengths] =
{
    "1/1 Straight", "1/1 Triplet", "1/1 Dotted",
    "1/2 Straight", "1/2 Triplet", "1/2 Dotted",
    "1/4 Straight", "1/4 Triplet", "1/4 Dotted",
    "1/8 Straight", "1/8 Triplet", "1/8 Dotted",
    "1/16 Straight", "1/16 Triplet", "1/16 Dotted",
    "1/32 Straight", "1/32 Triplet", "1/32 Dotted",
    "1/64 Straight", "1/64 Triplet", "1/64 Dotted",
    "1/128 Straight", "1/128 Triplet", "1/128 Dotted"
};

const char* const kShapeNames[kNumShapes] =
{
    "Exponential", "Triangle", "Sine Power 8", "Sine Power 4", "Sine Square", "Sine",
    "Harmonics", "More Harmonics", "Disharmonics", "Sine Square Root", "Sine 4 Root",
    "Rectangle", "Rect-Saw", "Saw", "Noise", "Mess"
};

const char* const kOversampleNames[3] = { "1x", "2x", "4x" };

//==============================================================================
// The 98 filter types, grouped by family exactly as documented in the manual.
const char* const kFilterTypeNames[kNumFilterTypes] =
{
    // Low-pass, 6 .. 120 dB/oct (13)
    "LP 6 dB", "LP 12 dB", "LP 18 dB", "LP 24 dB", "LP 30 dB", "LP 36 dB", "LP 48 dB",
    "LP 60 dB", "LP 72 dB", "LP 84 dB", "LP 96 dB", "LP 108 dB", "LP 120 dB",
    // High-pass (13)
    "HP 6 dB", "HP 12 dB", "HP 18 dB", "HP 24 dB", "HP 30 dB", "HP 36 dB", "HP 48 dB",
    "HP 60 dB", "HP 72 dB", "HP 84 dB", "HP 96 dB", "HP 108 dB", "HP 120 dB",
    // Band-pass (4)
    "BP 12 dB", "BP 24 dB", "BP 36 dB", "BP 48 dB",
    // Notch / band-reject (2)
    "Notch 12 dB", "Notch 24 dB",
    // Peak (2)
    "Peak 12 dB", "Peak 24 dB",
    // Low shelf (2)
    "Low Shelf 12 dB", "Low Shelf 24 dB",
    // High shelf (2)
    "High Shelf 12 dB", "High Shelf 24 dB",
    // Fade (3)
    "Fade LP-HP", "Fade LP-BP-HP", "Fade LP-Notch",
    // Harmonics (4)
    "Harmonics", "Harmonics Linear", "Harmonics Swap", "Harmonics Linear Swap",
    // Sub-X / Over-X / Band-X (4)
    "Sub-X", "Sub-X Soft", "Over-X", "Band-X",
    // Formant (6)
    "Formant A", "Formant E", "Formant I", "Formant O", "Formant U", "Formant A-E-I-O-U",
    // Comb (8)
    "Comb 1", "Comb 2", "Comb 3", "Comb 4", "Comb 5", "Comb 6", "Comb 7", "Comb 8",
    // Diffuser (8)
    "Diffuser 1", "Diffuser 2", "Diffuser 3", "Diffuser 4",
    "Diffuser 5", "Diffuser 6", "Diffuser 7", "Diffuser 8",
    // Polymorph (20)
    "Polymorph 1", "Polymorph 2", "Polymorph 3", "Polymorph 4", "Polymorph 5",
    "Polymorph 6", "Polymorph 7", "Polymorph 8", "Polymorph 9", "Polymorph 10",
    "Polymorph 11", "Polymorph 12", "Polymorph 13", "Polymorph 14", "Polymorph 15",
    "Polymorph 16", "Polymorph 17", "Polymorph 18", "Polymorph 19", "Polymorph 20",
    // Scream (7)
    "Scream 1", "Scream 2", "Scream 3", "Scream 4", "Scream 5", "Scream 6", "Scream 7"
};

//==============================================================================
static const char* const kDriveModeNames[] = { "Clean", "Soft", "Asymmetric", "Fold" };
static const char* const kFollowerEqNames[] = { "Off", "Low-pass", "High-pass", "Band-pass",
                                                "Low shelf", "High shelf", "Peak" };

//==============================================================================
float ParamSpec::normalise (float v) const noexcept
{
    if (maxv <= minv)
        return 0.0f;

    auto n = (clamp (v) - minv) / (maxv - minv);

    if (skew != 1.0f && skew > 0.0f)
        n = std::pow (n, 1.0f / skew);

    return n;
}

float ParamSpec::denormalise (float v) const noexcept
{
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);

    if (skew != 1.0f && skew > 0.0f)
        v = std::pow (v, skew);

    return minv + v * (maxv - minv);
}

//==============================================================================
static ParamSpec makeFloat (std::string id, std::string name, std::string unit,
                            float minv, float maxv, float defv, float skew = 1.0f)
{
    ParamSpec s;
    s.id = std::move (id);
    s.name = std::move (name);
    s.unit = std::move (unit);
    s.minv = minv;
    s.maxv = maxv;
    s.defv = defv;
    s.skew = skew;
    s.kind = ParamKind::Float;
    return s;
}

static ParamSpec makeBool (std::string id, std::string name, bool defv)
{
    ParamSpec s;
    s.id = std::move (id);
    s.name = std::move (name);
    s.minv = 0.0f;
    s.maxv = 1.0f;
    s.defv = defv ? 1.0f : 0.0f;
    s.kind = ParamKind::Bool;
    return s;
}

static ParamSpec makeChoice (std::string id, std::string name, int numChoices, int defv)
{
    ParamSpec s;
    s.id = std::move (id);
    s.name = std::move (name);
    s.minv = 0.0f;
    s.maxv = (float) (numChoices - 1);
    s.defv = (float) defv;
    s.kind = ParamKind::Choice;
    s.numChoices = numChoices;
    return s;
}

static ParamSpec makeInt (std::string id, std::string name, int minv, int maxv, int defv)
{
    ParamSpec s;
    s.id = std::move (id);
    s.name = std::move (name);
    s.minv = (float) minv;
    s.maxv = (float) maxv;
    s.defv = (float) defv;
    s.kind = ParamKind::Int;
    return s;
}

//==============================================================================
static int findTypeIndex (const char* name)
{
    for (int i = 0; i < kNumFilterTypes; ++i)
        if (std::strcmp (kFilterTypeNames[i], name) == 0)
            return i;

    return 0;
}

ParamTable::ParamTable()
{
    specs.reserve ((size_t) total());

    // ---- Globals --------------------------------------------------------
    specs.push_back (makeFloat ("glob.dryWet", "Dry/Wet", "%", 0.0f, 100.0f, 100.0f));
    specs.push_back (makeFloat ("glob.saturation", "Saturation", "%", 0.0f, 100.0f, 0.0f));
    specs.push_back (makeFloat ("glob.outputGain", "Output Gain", "dB", -24.0f, 24.0f, 0.0f));
    specs.push_back (makeBool  ("glob.clipping", "Clipping", false));
    specs.push_back (makeBool  ("glob.dcFilter", "DC Filter", false));
    specs.push_back (makeInt   ("glob.oversample", "Oversampling", 1, 4, 1));
    specs.push_back (makeBool  ("glob.multiband", "Multiband", false));
    specs.push_back (makeInt   ("glob.bandCount", "Band Count", 2, kNumBands, kNumBands));
    specs.push_back (makeFloat ("glob.xover1", "Crossover 1", "Hz", 20.0f, 20000.0f, 120.0f, 0.3f));
    specs.push_back (makeFloat ("glob.xover2", "Crossover 2", "Hz", 20.0f, 20000.0f, 400.0f, 0.3f));
    specs.push_back (makeFloat ("glob.xover3", "Crossover 3", "Hz", 20.0f, 20000.0f, 1200.0f, 0.3f));
    specs.push_back (makeFloat ("glob.xover4", "Crossover 4", "Hz", 20.0f, 20000.0f, 3500.0f, 0.3f));
    specs.push_back (makeFloat ("glob.xover5", "Crossover 5", "Hz", 20.0f, 20000.0f, 9000.0f, 0.3f));
    specs.push_back (makeFloat ("glob.keyTrack", "MIDI Follow", "%", 0.0f, 100.0f, 0.0f));

    const int defaultType = findTypeIndex ("LP 12 dB");
    const int defaultEqType = 0;
    const int defaultNoteLength = 9;   // 1/8 straight

    for (int p = 0; p < kNumProcessors; ++p)
    {
        const auto pfx = "proc" + std::to_string (p + 1) + ".";
        const auto nm = "F" + std::to_string (p + 1) + " ";

        specs.push_back (makeFloat  (pfx + "drive",     nm + "Drive",      "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "driveMode", nm + "Drive Mode", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "saturation",nm + "Saturation", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "outGain",   nm + "Output Gain","dB", -48.0f, 48.0f, 0.0f));
        specs.push_back (makeBool   (pfx + "clip",      nm + "Clip", false));
        specs.push_back (makeChoice (pfx + "type",      nm + "Type", kNumFilterTypes, defaultType));
        specs.push_back (makeFloat  (pfx + "freq",      nm + "Frequency", "Hz", 20.0f, 20000.0f, 800.0f, 0.3f));
        specs.push_back (makeFloat  (pfx + "freqRange", nm + "Frequency Range", "%", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "resonance", nm + "Resonance", "%", 0.0f, 100.0f, 40.0f));
        specs.push_back (makeFloat  (pfx + "resRange",  nm + "Resonance Range", "%", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "gain",      nm + "Gain", "dB", -48.0f, 48.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "gainRange", nm + "Gain Range", "%", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "character", nm + "Character", "%", 0.0f, 100.0f, 50.0f));
        specs.push_back (makeFloat  (pfx + "charRange", nm + "Character Range", "%", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "panorama",  nm + "Panorama", "st", -48.0f, 48.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "trim",      nm + "Trim", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "quality",   nm + "Quality", "%", 0.0f, 100.0f, 50.0f));

        specs.push_back (makeFloat  (pfx + "fDepth",    nm + "Follower Depth", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "fAttack",   nm + "Follower Attack", "ms", 0.0f, 10000.0f, 10.0f, 0.35f));
        specs.push_back (makeFloat  (pfx + "fRelease",  nm + "Follower Release", "ms", 0.0f, 10000.0f, 10.0f, 0.35f));
        specs.push_back (makeFloat  (pfx + "fRms",      nm + "Follower RMS", "ms", 0.0f, 100.0f, 1.0f, 0.5f));
        specs.push_back (makeFloat  (pfx + "fMaxLevel", nm + "Follower Max Level", "dB", -80.0f, 0.0f, 0.0f));
        specs.push_back (makeBool   (pfx + "fSidechain",nm + "Follower Side-chain", false));
        specs.push_back (makeChoice (pfx + "fEqType",   nm + "Follower Eq Type", 7, defaultEqType));
        specs.push_back (makeFloat  (pfx + "fEqFreq",   nm + "Follower Eq Freq", "Hz", 20.0f, 20000.0f, 1000.0f, 0.3f));
        specs.push_back (makeFloat  (pfx + "fEqGain",   nm + "Follower Eq Gain", "dB", -24.0f, 24.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "fEqQ",      nm + "Follower Eq Q", "", 0.1f, 10.0f, 0.707f, 0.5f));

        specs.push_back (makeFloat  (pfx + "lfoRate",   nm + "LFO Rate", "Hz", 0.01f, 100.0f, 4.0f, 0.35f));
        specs.push_back (makeFloat  (pfx + "lfoWidth",  nm + "LFO Width", "%", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "lfoOverride", nm + "LFO Override", "%", -2.0f, 100.0f, -2.0f));
        specs.push_back (makeBool   (pfx + "lfoInvert", nm + "LFO Invert", false));
        specs.push_back (makeBool   (pfx + "lfoSync",   nm + "LFO Sync", false));
        specs.push_back (makeChoice (pfx + "lfoNoteLength", nm + "LFO Note Length", kNumNoteLengths, defaultNoteLength));
        specs.push_back (makeFloat  (pfx + "lfoPhase",  nm + "LFO Phase", "%", 0.0f, 100.0f, 25.0f));
        specs.push_back (makeFloat  (pfx + "lfoShape",  nm + "LFO Shape", "", 0.0f, (float) (kNumShapes - 1), 5.0f));
        specs.push_back (makeFloat  (pfx + "lfoCustom", nm + "LFO Custom", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "lfoStep",   nm + "LFO Step", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat  (pfx + "lfoSmooth", nm + "LFO Smooth", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeBool   (pfx + "lfoHarmonics", nm + "LFO Harmonics Mode", false));
        specs.push_back (makeBool   (pfx + "lfoMidiReset", nm + "LFO MIDI Reset", false));
    }

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto pfx = "band" + std::to_string (b + 1) + ".";
        const auto nm = "Band " + std::to_string (b + 1) + " ";

        specs.push_back (makeBool  (pfx + "enable",  nm + "Enable", true));
        specs.push_back (makeBool  (pfx + "mute",    nm + "Mute", false));
        specs.push_back (makeBool  (pfx + "solo",    nm + "Solo", false));
        specs.push_back (makeFloat (pfx + "level",   nm + "Level", "dB", -48.0f, 24.0f, 0.0f));
        specs.push_back (makeFloat (pfx + "pan",     nm + "Pan", "", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeChoice (pfx + "type",   nm + "Type", kNumFilterTypes + 1, 0)); // 0 = inherit
        specs.push_back (makeFloat (pfx + "freq",    nm + "Frequency", "Hz", 0.0f, 20000.0f, 0.0f, 0.3f)); // 0 = inherit
        specs.push_back (makeFloat (pfx + "freqRange", nm + "Frequency Range", "%", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat (pfx + "resonance", nm + "Resonance", "%", -1.0f, 100.0f, -1.0f));     // <0 = inherit
        specs.push_back (makeFloat (pfx + "resRange", nm + "Resonance Range", "%", -100.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat (pfx + "gain",    nm + "Gain", "dB", -48.0f, 48.0f, 0.0f));
        specs.push_back (makeFloat (pfx + "character", nm + "Character", "%", -1.0f, 100.0f, -1.0f));     // <0 = inherit
        specs.push_back (makeFloat (pfx + "drive",   nm + "Drive", "%", -1.0f, 100.0f, -1.0f));           // <0 = inherit
        specs.push_back (makeFloat (pfx + "outGain", nm + "Output Gain", "dB", -48.0f, 48.0f, 0.0f));
        specs.push_back (makeFloat (pfx + "trim",    nm + "Trim", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat (pfx + "lfoPhaseOffset", nm + "LFO Phase Offset", "%", 0.0f, 100.0f, 0.0f));
        specs.push_back (makeFloat (pfx + "lfoRateMult", nm + "LFO Rate Mult", "", 0.125f, 8.0f, 1.0f, 0.5f));
        specs.push_back (makeFloat (pfx + "lfoWidth", nm + "LFO Width", "%", -100.0f, 100.0f, 0.0f));
    }
}

const ParamTable& ParamTable::get()
{
    static const ParamTable table;
    return table;
}

int ParamTable::indexOf (const std::string& id) const noexcept
{
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].id == id)
            return (int) i;

    return -1;
}

std::string ParamTable::processorPrefix (int proc) const
{
    return "proc" + std::to_string (proc + 1) + ".";
}

//==============================================================================
LfoProgram::LfoProgram()
{
    // Unit saw as a sensible default drawable custom shape.
    for (int i = 0; i < kCurvePoints; ++i)
        customShape[(size_t) i] = (float) i / (float) (kCurvePoints - 1);

    for (int i = 0; i < kMaxSteps; ++i)
        steps[(size_t) i] = 1.0f;

    for (int i = 0; i < kMaxHarmonics; ++i)
    {
        harmLevel[(size_t) i] = i == 0 ? 1.0f : 0.0f;
        harmPhase[(size_t) i] = 0.0f;
    }
}

//==============================================================================
float dbToGainF (float db) noexcept      { return std::pow (10.0f, db * 0.05f); }
float gainToDbF (float g) noexcept       { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

float onePoleCoeff (float ms, double sr) noexcept
{
    if (ms <= 0.0f)
        return 0.0f;

    return std::exp (-1.0f / (float) (ms * 0.001 * sr));
}

float noteLengthBeats (int index) noexcept
{
    index = index < 0 ? 0 : (index >= kNumNoteLengths ? kNumNoteLengths - 1 : index);

    const int duration = index / 3;   // 0 = 1/1 ... 7 = 1/128
    const int feel = index % 3;       // 0 = straight, 1 = triplet, 2 = dotted

    float beats = 4.0f / (float) (1 << duration);

    if (feel == 1)
        beats *= 2.0f / 3.0f;
    else if (feel == 2)
        beats *= 1.5f;

    return beats;
}

} // namespace wob
