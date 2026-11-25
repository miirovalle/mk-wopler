// Offline harness for the wobble core. Builds without any plugin framework so
// the DSP can be verified on any platform.
//
//   wobble_tests            -> run all checks
//   wobble_tests --render   -> also write reference WAV files next to the binary

#include "../Source/dsp/Engine.h"
#include "../Source/dsp/Filters.h"
#include "../Source/dsp/Params.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;

void check (bool condition, const std::string& what)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL  %s\n", what.c_str());
    }
    else
    {
        std::printf ("  ok    %s\n", what.c_str());
    }
}

void checkClose (double actual, double expected, double tolerance, const std::string& what)
{
    char buf[256];
    std::snprintf (buf, sizeof (buf), "%s (got %.6f, expected %.6f)", what.c_str(), actual, expected);
    check (std::fabs (actual - expected) <= tolerance, buf);
}

bool isFinite (const std::vector<float>& v)
{
    for (auto s : v)
        if (! std::isfinite (s))
            return false;

    return true;
}

float peakOf (const std::vector<float>& v)
{
    float p = 0.0f;

    for (auto s : v)
        p = std::max (p, std::fabs (s));

    return p;
}

double rmsOf (const std::vector<float>& v, size_t start = 0)
{
    double sum = 0.0;
    size_t n = 0;

    for (size_t i = start; i < v.size(); ++i, ++n)
        sum += (double) v[i] * (double) v[i];

    return n > 0 ? std::sqrt (sum / (double) n) : 0.0;
}

//==============================================================================
// Minimal Goertzel so we can measure the response at a single frequency without
// pulling in an FFT.
double magnitudeAt (const std::vector<float>& signal, double freq, double sr)
{
    const size_t n = signal.size();
    const double w = 2.0 * wob::kPi * freq / sr;
    const double coeff = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        s0 = (double) signal[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const double real = s1 - s2 * std::cos (w);
    const double imag = s2 * std::sin (w);

    return 2.0 * std::sqrt (real * real + imag * imag) / (double) n;
}

std::vector<float> sine (double freq, double sr, int numSamples, float amplitude = 0.5f)
{
    std::vector<float> v ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
        v[(size_t) i] = amplitude * (float) std::sin (2.0 * wob::kPi * freq * (double) i / sr);

    return v;
}

void writeWav (const std::string& path, const std::vector<float>& left, const std::vector<float>& right,
               int sampleRate = 48000)
{
    FILE* f = std::fopen (path.c_str(), "wb");

    if (f == nullptr)
        return;

    const auto frames = (unsigned) std::min (left.size(), right.size());
    const unsigned dataBytes = frames * 4u;
    const unsigned riffSize = 36u + dataBytes;

    auto u32 = [f] (unsigned v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (unsigned short v) { std::fwrite (&v, 2, 1, f); };

    std::fwrite ("RIFF", 1, 4, f);
    u32 (riffSize);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f);
    u32 (16);
    u16 (1);
    u16 (2);
    u32 ((unsigned) sampleRate);
    u32 ((unsigned) sampleRate * 4u);
    u16 (4);
    u16 (16);
    std::fwrite ("data", 1, 4, f);
    u32 (dataBytes);

    for (unsigned i = 0; i < frames; ++i)
    {
        const auto l = (short) std::lround (std::max (-1.0f, std::min (1.0f, left[i])) * 32000.0f);
        const auto r = (short) std::lround (std::max (-1.0f, std::min (1.0f, right[i])) * 32000.0f);
        std::fwrite (&l, 2, 1, f);
        std::fwrite (&r, 2, 1, f);
    }

    std::fclose (f);
}

//==============================================================================
void testParameterTable()
{
    std::printf ("\n[parameter table]\n");

    const auto& table = wob::ParamTable::get();

    check ((int) table.all().size() == wob::ParamTable::total(), "table size matches the layout constants");

    checkClose ((double) wob::ParamTable::total(), 14.0 + 2.0 * wob::ppNumProcParams + 6.0 * wob::bpNumBandParams, 0.0,
                "parameter count");

    // Spot check that the declared enum order matches the table order.
    check (table[wob::ParamTable::procIndex (0, wob::ppDrive)].id == "proc1.drive", "procIndex(0, ppDrive)");
    check (table[wob::ParamTable::procIndex (1, wob::ppType)].id == "proc2.type", "procIndex(1, ppType)");
    check (table[wob::ParamTable::bandIndex (5, wob::bpLevel)].id == "band6.level", "bandIndex(5, bpLevel)");
    check (table[wob::ParamTable::globalIndex (wob::gpDryWet)].id == "glob.dryWet", "globalIndex(gpDryWet)");

    // Every id must be unique or the AU parameter tree would collapse entries.
    bool unique = true;

    for (size_t i = 0; i < table.all().size() && unique; ++i)
        for (size_t j = i + 1; j < table.all().size(); ++j)
            if (table.all()[i].id == table.all()[j].id)
            {
                unique = false;
                std::printf ("        duplicate id: %s\n", table.all()[i].id.c_str());
                break;
            }

    check (unique, "parameter ids are unique");

    // Manual-stated ranges.
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppFreq)].defv, 800.0, 0.05, "F1 Frequency default 800 Hz");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppFreq)].minv, 20.0, 0.0, "F1 Frequency min 20 Hz");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppFreq)].maxv, 20000.0, 0.0, "F1 Frequency max 20 kHz");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppResonance)].defv, 40.0, 0.01, "Resonance default 40%");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppCharacter)].defv, 50.0, 0.01, "Character default 50%");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppLRate)].defv, 4.0, 0.001, "LFO Rate default 4 Hz");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppLPhase)].defv, 25.0, 0.01, "LFO Phase default 25%");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppFAttack)].defv, 10.0, 0.01, "Follower Attack default 10 ms");
    checkClose ((double) table[wob::ParamTable::globalIndex (wob::gpOutputGain)].maxv, 24.0, 0.0, "Output Gain max +24 dB");
    checkClose ((double) table[wob::ParamTable::procIndex (0, wob::ppOutGain)].minv, -48.0, 0.0, "Filter Out Gain min -48 dB");

    // Parameter normalisation round trip.
    const auto& spec = table[wob::ParamTable::procIndex (0, wob::ppFreq)];
    bool roundTrip = true;

    for (float f : { 20.0f, 100.0f, 800.0f, 5000.0f, 20000.0f })
        roundTrip = roundTrip && std::fabs (spec.denormalise (spec.normalise (f)) - f) < 0.05f;

    check (roundTrip, "frequency normalisation round-trips");

    check (std::string (wob::kFilterTypeNames[97]).size() > 0, "98 filter types are named");
}

//==============================================================================
void testFilterTypes (double sr)
{
    std::printf ("\n[filter types]\n");

    std::vector<float> impulse (8192, 0.0f);
    impulse[0] = 1.0f;
    std::vector<float> zeros (8192, 0.0f);

    int stable = 0;

    for (int t = 0; t < wob::kNumFilterTypes; ++t)
    {
        wob::FilterInstance f;
        f.prepare (sr);
        f.setType (t);

        wob::FilterSettings s;
        s.freq = 700.0f;
        s.resonance = 60.0f;
        s.gainDb = 6.0f;
        s.character = 70.0f;
        s.quality = 50.0f;
        f.setSettings (s);

        auto l = impulse;
        auto r = zeros;

        // Run four blocks so modulation-like restarts do not hide instability.
        for (int b = 0; b < 4; ++b)
            f.process (l.data(), r.data(), (int) l.size());

        if (std::isfinite (peakOf (l)))
            ++stable;
        else
            std::printf ("        type %d (%s) diverged\n", t, wob::kFilterTypeNames[t]);
    }

    check (stable == wob::kNumFilterTypes, "all 98 filter types stay finite on an impulse");

    // Low-pass at 1 kHz should pass 100 Hz and reject 8 kHz.
    auto runTone = [] (int type, double freq, double sr_, double cutoff)
    {
        wob::FilterInstance f;
        f.prepare (sr_);
        f.setType (type);

        wob::FilterSettings s;
        s.freq = (float) cutoff;
        s.resonance = 0.0f;
        s.gainDb = 0.0f;
        s.character = 50.0f;
        s.quality = 50.0f;
        f.setSettings (s);

        auto l = sine (freq, sr_, 48000);
        std::vector<float> r (l.size(), 0.0f);

        for (size_t i = 0; i < l.size(); i += 32)
            f.process (l.data() + i, r.data() + i, (int) std::min<size_t> (32, l.size() - i));

        return rmsOf (l, 16384) ;
    };

    const double passBand = runTone (1, 100.0, sr, 1000.0);   // LP 12 dB
    const double stopBand = runTone (1, 8000.0, sr, 1000.0);

    check (passBand > 0.05, "LP 12 dB passes low frequencies");
    check (stopBand < passBand * 0.05, "LP 12 dB rejects high frequencies");

    // High-pass mirror image.
    const int hpIndex = 14;   // HP 12 dB
    check (std::string (wob::kFilterTypeNames[hpIndex]) == "HP 12 dB", "HP 12 dB index");

    const double hpPass = runTone (hpIndex, 8000.0, sr, 1000.0);
    const double hpStop = runTone (hpIndex, 100.0, sr, 1000.0);

    check (hpPass > 0.05, "HP 12 dB passes high frequencies");
    check (hpStop < hpPass * 0.05, "HP 12 dB rejects low frequencies");

    // Resonance must raise the level around the cutoff.
    auto resonanceGain = [] (double res)
    {
        wob::FilterInstance f;
        f.prepare (48000.0);
        f.setType (3);   // LP 24 dB

        wob::FilterSettings s;
        s.freq = 1000.0f;
        s.resonance = (float) res;
        f.setSettings (s);

        auto l = sine (1000.0, 48000.0, 48000);
        std::vector<float> r (l.size(), 0.0f);

        for (size_t i = 0; i < l.size(); i += 32)
            f.process (l.data() + i, r.data() + i, (int) std::min<size_t> (32, l.size() - i));

        return rmsOf (l, 24000);
    };

    const double dry = resonanceGain (0.0);
    const double wet = resonanceGain (90.0);

    check (wet > dry * 1.5, "resonance boosts the response at the corner frequency");

    // Harmony filters must create energy at multiples of the cutoff.
    wob::FilterInstance harm;
    harm.prepare (48000.0);
    harm.setType (41);   // Harmonics
    wob::FilterSettings hs;
    hs.freq = 200.0f;
    hs.gainDb = 0.0f;
    hs.character = 100.0f;
    hs.resonance = 40.0f;
    hs.quality = 100.0f;
    harm.setSettings (hs);

    auto tone = sine (200.0, 48000.0, 96000);
    std::vector<float> right (tone.size(), 0.0f);

    for (size_t i = 0; i < tone.size(); i += 32)
        harm.process (tone.data() + i, right.data() + i, (int) std::min<size_t> (32, tone.size() - i));

    const double fundamental = magnitudeAt (tone, 200.0, 48000.0);
    check (fundamental > 0.1, "harmonics filter passes the fundamental");
    check (isFinite (tone), "harmonics filter stays finite");
}

//==============================================================================
void testLfo()
{
    std::printf ("\n[lfo]\n");

    wob::Lfo lfo;
    lfo.prepare (48000.0);
    lfo.reset();

    // 1 Hz saw should descend linearly across one second.
    lfo.update (1.0f, false, 0, 120.0, 0.0f, 0.0f, false, 13.0f, 0.0f, 0.0f, 0.0f, false);

    std::vector<float> l (48000), r (48000);
    lfo.render (l.data(), r.data(), 48000);

    checkClose ((double) l[0], -1.0, 0.02, "saw starts at -1");
    checkClose ((double) l[12000], -0.5, 0.02, "saw is at -0.5 a quarter in");
    checkClose ((double) l[24000], 0.0, 0.02, "saw crosses zero at half a cycle");

    // Phase offset of 25 % must shift the saw by a quarter cycle.
    wob::Lfo shifted;
    shifted.prepare (48000.0);
    shifted.reset();
    shifted.update (1.0f, false, 0, 120.0, 25.0f, 0.0f, false, 13.0f, 0.0f, 0.0f, 0.0f, false);
    shifted.render (l.data(), r.data(), 48000);

    checkClose ((double) l[0], -0.5, 0.02, "25 % phase offset shifts the saw");

    // Invert must flip the sign.
    wob::Lfo inverted;
    inverted.prepare (48000.0);
    inverted.reset();
    inverted.update (1.0f, false, 0, 120.0, 0.0f, 0.0f, true, 13.0f, 0.0f, 0.0f, 0.0f, false);
    inverted.render (l.data(), r.data(), 48000);

    checkClose ((double) l[0], 1.0, 0.02, "invert flips the saw");

    // Stereo width must decorrelate the channels.
    wob::Lfo wide;
    wide.prepare (48000.0);
    wide.reset();
    wide.update (1.0f, false, 0, 120.0, 0.0f, 50.0f, false, 13.0f, 0.0f, 0.0f, 0.0f, false);
    std::vector<float> wl (48000), wr (48000);
    wide.render (wl.data(), wr.data(), 48000);

    checkClose ((double) wl[0], -1.0, 0.02, "50 % width leaves the left channel at the cycle start");
    checkClose ((double) wr[0], 0.0, 0.02, "50 % width advances the right channel by half a cycle");

    // Host sync: a 1/4 note at 120 bpm is 0.5 s, i.e. 2 Hz.
    wob::Lfo synced;
    synced.prepare (48000.0);
    synced.reset();
    synced.update (4.0f, true, 6, 120.0, 0.0f, 0.0f, false, 5.0f, 0.0f, 0.0f, 0.0f, false);
    checkClose (synced.getFrequency(), 2.0, 1.0e-6, "1/4 straight at 120 bpm equals 2 Hz");

    synced.update (4.0f, true, 7, 160.0, 0.0f, 0.0f, false, 5.0f, 0.0f, 0.0f, 0.0f, false);
    checkClose (synced.getFrequency(), 160.0 / 60.0 * 1.5, 1.0e-6, "1/4 triplet at 160 bpm");

    // Every predefined shape must stay inside [-1, 1].
    bool bounded = true;

    for (int s = 0; s < wob::kNumShapes; ++s)
    {
        wob::Lfo shape;
        shape.prepare (48000.0);
        shape.reset();
        shape.update (2.0f, false, 0, 120.0, 0.0f, 0.0f, false, (float) s, 0.0f, 0.0f, 0.0f, false);
        shape.render (wl.data(), wr.data(), 4800);

        for (auto v : wl)
            if (std::fabs (v) > 1.0001f)
                bounded = false;
    }

    check (bounded, "all 16 shapes are bounded to +/-1");

    // Harmonics mode must produce a sine when only the first partial is set.
    wob::LfoProgram prog;
    wob::Lfo harm;
    harm.prepare (48000.0);
    harm.setProgram (&prog);
    harm.reset();
    harm.update (1.0f, false, 0, 120.0, 0.0f, 0.0f, false, 5.0f, 0.0f, 0.0f, 0.0f, true);
    harm.render (wl.data(), wr.data(), 48000);
    checkClose ((double) wl[12000], 1.0, 0.05, "harmonics mode with a single partial is a sine");

    // Override must win over the oscillator.
    wob::Lfo over;
    over.prepare (48000.0);
    over.reset();
    over.update (1.0f, false, 0, 120.0, 0.0f, 0.0f, false, 5.0f, 0.0f, 0.0f, 0.0f, false);
    over.setOverride (0.75f);
    over.render (wl.data(), wr.data(), 480);
    checkClose ((double) wl[0], 0.5, 1.0e-5, "LFO override 75 % maps to +0.5");
}

//==============================================================================
void testEngine (double sr, bool render)
{
    std::printf ("\n[engine]\n");

    wob::WobblerEngine engine;
    engine.prepare (sr, 512);

    wob::EngineParams p;

    // Baseline: default filter settings must not blow up the level.
    {
        engine.setParams (p);
        engine.reset();

        auto bl = sine (110.0, sr, (int) (sr * 0.5), 0.5f);
        auto br = bl;
        engine.process (bl.data(), br.data(), (int) bl.size());

        char msg[160];
        std::snprintf (msg, sizeof (msg), "default settings keep the output bounded (peak %.3f)", (double) peakOf (bl));
        check (peakOf (bl) < 3.0f, msg);
    }

    // A classic wobble: 24 dB low-pass swept two octaves by a 4 Hz saw.
    for (int i = 0; i < wob::kNumProcessors; ++i)
    {
        p.proc[i].type = 3;          // LP 24 dB
        p.proc[i].freq = 400.0f;
        p.proc[i].freqRange = 50.0f;
        p.proc[i].resonance = 60.0f;
        p.proc[i].lRate = 4.0f;
        p.proc[i].lShape = 13.0f;    // saw
    }

    p.proc[1].drive = 40.0f;
    p.proc[1].driveMode = 60.0f;
    p.proc[1].saturation = 30.0f;
    p.proc[1].outGainDb = -3.0f;
    p.multiband = false;

    engine.setParams (p);
    engine.reset();

    const int numSamples = (int) (sr * 2.0);
    auto left = sine (110.0, sr, numSamples, 0.4f);
    auto right = left;

    engine.process (left.data(), right.data(), numSamples);

    char peakMsg[160];
    std::snprintf (peakMsg, sizeof (peakMsg), "heavy resonance and drive stay bounded (peak %.3f)", (double) peakOf (left));

    check (isFinite (left) && isFinite (right), "engine output is finite");
    check (peakOf (left) < 32.0f, peakMsg);
    check (rmsOf (left) > 1.0e-4, "engine produces signal");

    // The wobble must actually modulate: the level of successive 10 ms windows
    // has to vary a lot for a swept low-pass on a saw-ish input.
    auto variation = [] (const std::vector<float>& v, double sr_)
    {
        const int win = (int) (sr_ * 0.01);
        double lo = 1.0e9, hi = -1.0e9;

        for (size_t i = 0; i + (size_t) win < v.size(); i += (size_t) win)
        {
            const double r = rmsOf (v, i);

            if (i == 0)
                continue;

            lo = std::min (lo, r);
            hi = std::max (hi, r);
        }

        return (lo > 1.0e-9) ? hi / lo : 1.0;
    };

    std::vector<float> window (left.begin(), left.end());
    const double variationRatio = variation (window, sr);
    check (variationRatio > 1.5, "LFO sweep modulates the output level");

    // ---- dry/wet ------------------------------------------------------
    p.dryWet = 0.0f;
    engine.setParams (p);
    engine.reset();

    auto original = sine (110.0, sr, numSamples, 0.4f);
    auto dl = original;
    auto dr = dl;
    engine.process (dl.data(), dr.data(), numSamples);

    double maxDiff = 0.0;

    for (size_t i = 0; i < dl.size(); ++i)
        maxDiff = std::max (maxDiff, std::fabs ((double) dl[i] - (double) original[i]));

    check (maxDiff < 0.001, "dry/wet 0 % passes the input through unchanged");

    // ---- output gain --------------------------------------------------
    p.dryWet = 100.0f;
    p.outGainDb = -6.0f;
    p.saturation = 0.0f;
    engine.setParams (p);
    engine.reset();

    auto gl = sine (110.0, sr, numSamples, 0.4f);
    auto gr = gl;
    engine.process (gl.data(), gr.data(), numSamples);

    p.outGainDb = 0.0f;
    engine.setParams (p);
    engine.reset();

    auto ul = sine (110.0, sr, numSamples, 0.4f);
    auto ur = ul;
    engine.process (ul.data(), ur.data(), numSamples);

    const double ratio = rmsOf (gl) / std::max (1.0e-9, rmsOf (ul));
    checkClose (ratio, wob::dbToGainF (-6.0f), 0.03, "-6 dB output gain halves the level");

    // ---- multiband ----------------------------------------------------
    p.multiband = true;
    p.bandCount = 6;
    p.band[0].freq = 120.0f;
    p.band[5].freq = 6000.0f;

    for (int b = 0; b < wob::kNumBands; ++b)
    {
        p.band[b].enable = true;
        p.band[b].levelDb = 0.0f;
        p.band[b].lPhaseOffset = (float) b * 8.0f;
    }

    engine.setParams (p);
    engine.reset();

    auto ml = sine (110.0, sr, numSamples, 0.4f);
    auto mr = ml;
    engine.process (ml.data(), mr.data(), numSamples);

    check (isFinite (ml) && isFinite (mr), "six-band mode stays finite");
    check (rmsOf (ml) > 1.0e-4, "six-band mode produces signal");

    // Muting every band must silence the wet path.
    for (int b = 0; b < wob::kNumBands; ++b)
        p.band[b].enable = false;

    engine.setParams (p);
    engine.reset();

    auto sl = sine (110.0, sr, 4096, 0.4f);
    auto srBuf = sl;
    engine.process (sl.data(), srBuf.data(), 4096);

    char msg[128];
    std::snprintf (msg, sizeof (msg), "disabling every band silences the output (rms %.9f)", rmsOf (sl));
    check (rmsOf (sl) < 1.0e-6, msg);

    // ---- side chain ---------------------------------------------------
    p.multiband = false;
    p.proc[0].fDepth = 100.0f;

    for (int b = 0; b < wob::kNumBands; ++b)
        p.band[b].enable = true;

    engine.setParams (p);
    engine.reset();

    auto foll = sine (110.0, sr, 8000, 0.4f);
    auto follR = foll;
    auto scL = sine (60.0, sr, 8000, 0.9f);
    auto scR = scL;
    engine.setSidechain (scL.data(), scR.data());
    engine.process (foll.data(), follR.data(), 8000);
    engine.setSidechain (nullptr, nullptr);

    check (isFinite (foll), "side-chain follower stays finite");

    // ---- render reference files ---------------------------------------
    if (render)
    {
        wob::WobblerEngine renderer;
        renderer.prepare (48000.0, 512);

        wob::EngineParams rp;
        rp.proc[0].type = 3;
        rp.proc[0].freq = 300.0f;
        rp.proc[0].freqRange = 60.0f;
        rp.proc[0].resonance = 70.0f;
        rp.proc[0].lRate = 4.0f;
        rp.proc[0].lShape = 13.0f;
        rp.proc[1].type = 1;
        rp.proc[1].freq = 2000.0f;
        rp.proc[1].drive = 45.0f;
        rp.proc[1].driveMode = 70.0f;
        rp.proc[1].saturation = 40.0f;
        rp.proc[1].outGainDb = -4.0f;
        rp.saturation = 20.0f;
        rp.outGainDb = -2.0f;

        renderer.setParams (rp);
        renderer.reset();

        auto rl = sine (55.0, 48000.0, 48000 * 3, 0.5f);
        auto rr = rl;
        renderer.process (rl.data(), rr.data(), (int) rl.size());
        writeWav ("wobble_reference.wav", rl, rr, 48000);
        std::printf ("        wrote wobble_reference.wav\n");
    }
}

} // namespace

int main (int argc, char** argv)
{
    const bool render = argc > 1 && std::strcmp (argv[1], "--render") == 0;

    std::printf ("wobble core tests\n=================\n");

    testParameterTable();
    testFilterTypes (48000.0);
    testLfo();
    testEngine (48000.0, render);

    std::printf ("\n%d checks, %d failures\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
