#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wob
{

namespace
{
inline float pickInherit (float bandValue, float procValue) noexcept
{
    return bandValue >= 0.0f ? bandValue : procValue;
}

inline double shaper (int index, double x) noexcept
{
    switch (index)
    {
        case 1:  return fastTanh (x);
        case 2:  return x >= 0.0 ? fastTanh (x) : fastTanh (1.6 * x) * 0.8;
        case 3:  return std::sin (clampd (x, -3.0, 3.0) * kPi * 0.5);
        default: return x;
    }
}

// Blends between the four drive characters listed for the Drive Mode control.
inline double driveShape (double mode01, double x) noexcept
{
    const double pos = clampd (mode01, 0.0, 1.0) * 3.0;
    const int i0 = (int) std::floor (pos);
    const int i1 = std::min (3, i0 + 1);
    const double f = pos - (double) i0;

    return shaper (i0, x) * (1.0 - f) + shaper (i1, x) * f;
}

inline double softSaturate (double x, double amount) noexcept
{
    const double shaped = fastTanh (1.4 * x + 0.18 * x * x * (x > 0.0 ? 1.0 : -1.0));
    return x * (1.0 - amount) + shaped * amount;
}
} // namespace

//==============================================================================
void MultibandSplitter::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    restL.assign (4096, 0.0f);
    restR.assign (4096, 0.0f);
    reset();

    for (auto& s : lowSplit)
        for (auto& f : s)
            f.setLowPass (1000.0, 0.7071, sr);

    for (auto& s : highSplit)
        for (auto& f : s)
            f.setHighPass (1000.0, 0.7071, sr);
}

void MultibandSplitter::reset() noexcept
{
    for (auto& s : lowSplit)
        for (auto& f : s)
            f.reset();

    for (auto& s : highSplit)
        for (auto& f : s)
            f.reset();
}

int MultibandSplitter::process (const float* left, const float* right, int numSamples,
                                int numBands, float* const* bandLeft, float* const* bandRight,
                                const float* crossovers) noexcept
{
    numBands = std::max (2, std::min (kNumBands, numBands));

    if ((int) restL.size() < numSamples)
    {
        restL.assign ((size_t) numSamples, 0.0f);
        restR.assign ((size_t) numSamples, 0.0f);
    }

    std::memcpy (restL.data(), left, sizeof (float) * (size_t) numSamples);
    std::memcpy (restR.data(), right, sizeof (float) * (size_t) numSamples);

    for (int i = 0; i < numBands - 1; ++i)
    {
        const double cutoff = clampd ((double) crossovers[i], 20.0, 20000.0);

        lowSplit[(size_t) i][0].setLowPass  (cutoff, 0.70710678, sr);
        lowSplit[(size_t) i][1].setLowPass  (cutoff, 0.70710678, sr);
        highSplit[(size_t) i][0].setHighPass (cutoff, 0.70710678, sr);
        highSplit[(size_t) i][1].setHighPass (cutoff, 0.70710678, sr);

        float* lo = bandLeft[i];
        float* loR = bandRight[i];

        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* src = ch == 0 ? restL.data() : restR.data();
                const double x = (double) src[n];

                const double low = lowSplit[(size_t) i][1].process (ch, lowSplit[(size_t) i][0].process (ch, x));
                const double high = highSplit[(size_t) i][1].process (ch, highSplit[(size_t) i][0].process (ch, x));

                if (ch == 0)
                {
                    lo[n] = (float) sanitise (low);
                    restL[(size_t) n] = (float) sanitise (high);
                }
                else
                {
                    loR[n] = (float) sanitise (low);
                    restR[(size_t) n] = (float) sanitise (high);
                }
            }
        }
    }

    std::memcpy (bandLeft[numBands - 1], restL.data(), sizeof (float) * (size_t) numSamples);
    std::memcpy (bandRight[numBands - 1], restR.data(), sizeof (float) * (size_t) numSamples);

    return numBands;
}

//==============================================================================
void ProcessorUnit::prepare (double sampleRate, int blockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    filter.prepare (sr);
    lfo.prepare (sr);
    follower.prepare (sr);

    ctrlL.assign ((size_t) std::max (blockSize, kNumBands), 0.0f);
    ctrlR.assign ((size_t) std::max (blockSize, kNumBands), 0.0f);

    reset();
}

void ProcessorUnit::reset() noexcept
{
    filter.reset();
    lfo.reset();
    follower.reset();
    lastControl[0] = lastControl[1] = 0.0f;
}

//==============================================================================
void ProcessorUnit::applyDrive (float* left, float* right, int numSamples, float amount, float mode) noexcept
{
    if (amount <= 0.001f)
        return;

    const double wet = clampd (amount * 0.01, 0.0, 1.0);
    const double gain = std::pow (10.0, (amount * 0.24) / 20.0);
    const double mode01 = clampd (mode * 0.01, 0.0, 1.0);

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* buf = ch == 0 ? left : right;
            const double x = (double) buf[n];
            const double shaped = driveShape (mode01, x * gain);
            buf[n] = (float) sanitise (x * (1.0 - wet) + shaped * wet);
        }
    }
}

void ProcessorUnit::applySaturation (float* left, float* right, int numSamples, float amount) noexcept
{
    if (amount <= 0.001f)
        return;

    const double wet = clampd (amount * 0.01, 0.0, 1.0) * 0.85;
    const double gain = std::pow (10.0, (amount * 0.18) / 20.0);

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* buf = ch == 0 ? left : right;
            const double x = (double) buf[n];
            buf[n] = (float) sanitise (softSaturate (x * gain, wet) / gain * 0.85 + x * 0.15);
        }
    }
}

//==============================================================================
void ProcessorUnit::process (float* left, float* right, int numSamples,
                             const ProcParams& p, const BandParams* band,
                             const float* sidechainLeft, const float* sidechainRight,
                             double frequencyScale, double bpm) noexcept
{
    if ((int) ctrlL.size() < numSamples)
    {
        ctrlL.assign ((size_t) numSamples, 0.0f);
        ctrlR.assign ((size_t) numSamples, 0.0f);
    }

    // ---- 1. input drive -------------------------------------------------
    applyDrive (left, right, numSamples, pickInherit (band != nullptr ? band->drive : -1.0f, p.drive), p.driveMode);

    // ---- 2. control sources --------------------------------------------
    const float rate = p.lRate * (band != nullptr ? band->lRateMult : 1.0f);
    const float width = p.lWidth + (band != nullptr ? band->lWidth : 0.0f);
    const float phase = p.lPhase + (band != nullptr ? band->lPhaseOffset : 0.0f);

    lfo.update (rate, p.lSync, p.lNoteLength, bpm, phase, width, p.lInvert,
                p.lShape, p.lCustom, p.lStep, p.lSmooth, p.lHarmonicMode);
    lfo.setOverride (p.lOverride);
    lfo.render (ctrlL.data(), ctrlR.data(), numSamples);

    const float lfoMidL = ctrlL[(size_t) (numSamples / 2)];
    const float lfoMidR = ctrlR[(size_t) (numSamples / 2)];

    float followerValue = 0.0f;

    if (p.fDepth > 0.01f)
    {
        follower.update (p.fAttackMs, p.fReleaseMs, p.fRmsMs, p.fMaxLevelDb,
                         p.fEqType, p.fEqFreq, p.fEqGainDb, p.fEqQ);

        const float* scL = (p.fSidechain && sidechainLeft != nullptr) ? sidechainLeft : left;
        const float* scR = (p.fSidechain && sidechainRight != nullptr) ? sidechainRight : right;
        followerValue = follower.process (scL, scR, numSamples);
    }

    const double depth = clampd (p.fDepth * 0.01, 0.0, 1.0);
    const double followerBipolar = (double) followerValue * 2.0 - 1.0;

    // ---- 3. modulated filter settings ----------------------------------
    const double trim = clampd (pickInherit (band != nullptr ? band->trim : -1.0f, p.trim) * 0.01, 0.0, 1.0);
    const double freqRange = (band != nullptr && band->freqRange != 0.0f) ? (double) band->freqRange : (double) p.freqRange;
    const double resRange = (band != nullptr && band->resRange != 0.0f) ? (double) band->resRange : (double) p.resRange;
    const double gainRange = (double) p.gainRange;
    const double charRange = (double) p.charRange;

    const double centreFreq = (double) pickInherit (band != nullptr ? band->freq : -1.0f, p.freq);
    const double baseRes = (double) pickInherit (band != nullptr ? band->resonance : -1.0f, p.resonance);
    const double baseGain = (double) pickInherit (band != nullptr ? band->gainDb : -999.0f, p.gainDb);
    const double baseChar = (double) pickInherit (band != nullptr ? band->character : -1.0f, p.character);

    FilterSettings leftSettings, rightSettings;

    for (int ch = 0; ch < 2; ++ch)
    {
        const double lfoValue = (double) (ch == 0 ? lfoMidL : lfoMidR);
        double control = (1.0 - depth) * lfoValue + depth * followerBipolar;

        // Trim raises the bottom of the control signal, exactly like cutting the
        // bottom off a saw wave.
        const double unipolar = clampd (control * 0.5 + 0.5, 0.0, 1.0);
        control = (trim + (1.0 - trim) * unipolar) * 2.0 - 1.0;

        const double panorama = (double) p.panorama * (ch == 0 ? -1.0 : 1.0);

        double freq = centreFreq * std::pow (2.0, freqRange * 0.01 * 6.0 * control)
                                 * std::pow (2.0, panorama / 12.0)
                                 * frequencyScale;

        freq = clampd (freq, 20.0, 20000.0);

        const double res = clampd (baseRes + resRange * 0.01 * 50.0 * control, 0.0, 100.0);
        const double gain = clampd (baseGain + gainRange * 0.01 * 24.0 * control, -48.0, 48.0);
        const double character = clampd (baseChar + charRange * 0.01 * 40.0 * control, 0.0, 100.0);

        auto& target = ch == 0 ? leftSettings : rightSettings;
        target.freq = (float) freq;
        target.resonance = (float) res;
        target.gainDb = (float) gain;
        target.character = (float) character;
        target.quality = p.quality;

        lastControl[ch] = (float) control;
    }

    const int type = band != nullptr && band->type >= 0 ? band->type : p.type;
    filter.setType (type);
    filter.setSettings (leftSettings, rightSettings);
    filter.process (left, right, numSamples);

    // ---- 4. output stage ------------------------------------------------
    const double outGain = dbToGainF ((float) (p.outGainDb + (band != nullptr ? band->outGainDb : 0.0f)));

    for (int n = 0; n < numSamples; ++n)
    {
        left[n] = (float) sanitise ((double) left[n] * outGain);
        right[n] = (float) sanitise ((double) right[n] * outGain);
    }

    applySaturation (left, right, numSamples, p.saturation);

    if (p.clip)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            left[n] = clampf (left[n], -1.0f, 1.0f);
            right[n] = clampf (right[n], -1.0f, 1.0f);
        }
    }
}

//==============================================================================
void WobblerEngine::prepare (double sampleRate_, int maximumBlockSize)
{
    sampleRate = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
    maxBlock = std::max (maximumBlockSize, kControlBlock);

    for (auto& chain : chains)
        for (auto& unit : chain)
            unit.prepare (sampleRate, kControlBlock);

    for (int p = 0; p < kNumProcessors; ++p)
        for (auto& chain : chains)
            chain[(size_t) p].setProgram (&programs[p]);

    splitter.prepare (sampleRate);

    dryL.assign ((size_t) kControlBlock, 0.0f);
    dryR.assign ((size_t) kControlBlock, 0.0f);
    workL.assign ((size_t) kControlBlock, 0.0f);
    workR.assign ((size_t) kControlBlock, 0.0f);

    for (int b = 0; b < kNumBands; ++b)
    {
        bandL[b].assign ((size_t) kControlBlock, 0.0f);
        bandR[b].assign ((size_t) kControlBlock, 0.0f);
        bandLeftPtrs[(size_t) b] = bandL[b].data();
        bandRightPtrs[(size_t) b] = bandR[b].data();
    }

    chainDcL.setCutoff (20.0, sampleRate);
    chainDcR.setCutoff (20.0, sampleRate);

    dryWetSmooth.setTimeMs (15.0f, sampleRate);
    outGainSmooth.setTimeMs (15.0f, sampleRate);
    dryWetSmooth.snap (params.dryWet);
    outGainSmooth.snap (dbToGainF (params.outGainDb));

    reset();
}

void WobblerEngine::reset() noexcept
{
    for (auto& chain : chains)
        for (auto& unit : chain)
            unit.reset();

    splitter.reset();
    chainDcL.reset();
    chainDcR.reset();

    // Snap the smoothers to their targets so a reset is instantaneous (this is
    // also what the host expects after a seek or a panic).
    dryWetSmooth.snap (params.dryWet);
    outGainSmooth.snap (dbToGainF (params.outGainDb));

    inputPeak[0] = inputPeak[1] = 0.0f;
    outputPeak[0] = outputPeak[1] = 0.0f;
    widthMeter = 0.0f;
    widthAccum = 0.0f;
    widthCount = 0;
}

void WobblerEngine::setParams (const EngineParams& newParams) noexcept
{
    params = newParams;
    outGainSmooth.setTarget (dbToGainF (params.outGainDb));
    dryWetSmooth.setTarget (params.dryWet);
}

void WobblerEngine::setLfoProgram (int processorIndex, const LfoProgram& program) noexcept
{
    if (processorIndex >= 0 && processorIndex < kNumProcessors)
        programs[processorIndex] = program;
}

const LfoProgram& WobblerEngine::getLfoProgram (int processorIndex) const noexcept
{
    return programs[processorIndex < 0 ? 0 : (processorIndex >= kNumProcessors ? kNumProcessors - 1 : processorIndex)];
}

void WobblerEngine::setTempo (double newBpm) noexcept { bpm = newBpm > 1.0 ? newBpm : 120.0; }
void WobblerEngine::setMidiNote (int note) noexcept  { midiNote = note; }
void WobblerEngine::setSidechain (const float* left, const float* right) noexcept
{
    sidechainL = left;
    sidechainR = right;
}

void WobblerEngine::resetLfoPhases() noexcept
{
    for (auto& chain : chains)
        for (auto& unit : chain)
            unit.lfo.reset();
}

//==============================================================================
void WobblerEngine::runChain (int bandIndex, float* left, float* right, int numSamples,
                              const BandParams* band) noexcept
{
    const double keyTrack = clampd (params.keyTrack, 0.0, 100.0) * 0.01;
    const double frequencyScale = std::pow (2.0, (double) (midiNote - 60) / 12.0 * keyTrack);

    for (int u = 0; u < kNumProcessors; ++u)
    {
        auto& unit = chains[(size_t) bandIndex][(size_t) u];
        unit.process (left, right, numSamples, params.proc[u], band,
                      sidechainL, sidechainR, frequencyScale, bpm);
    }
}

void WobblerEngine::process (float* left, float* right, int numSamples) noexcept
{
    int pos = 0;

    while (pos < numSamples)
    {
        const int chunk = std::min (kControlBlock, numSamples - pos);
        processControlBlock (left + pos, right + pos, chunk);
        pos += chunk;
    }

    // Decay the peak meters so they fall back when the input stops.
    peakDecay = (float) std::exp (-1.0 / (0.4 * sampleRate / 32.0));

    if (widthCount > 0)
    {
        widthMeter = widthAccum / (float) widthCount;
        widthAccum = 0.0f;
        widthCount = 0;
    }
}

void WobblerEngine::processControlBlock (float* left, float* right, int numSamples) noexcept
{
    const int count = numSamples;

    // ---- metering + dry copy -------------------------------------------
    for (int ch = 0; ch < 2; ++ch)
    {
        float* buf = ch == 0 ? left : right;
        float peak = inputPeak[ch];

        for (int n = 0; n < count; ++n)
        {
            if (! std::isfinite (buf[n]))
                buf[n] = 0.0f;

            const float a = std::fabs (buf[n]);

            if (a > peak)
                peak = a;
        }

        inputPeak[ch] = peak;
    }

    std::memcpy (dryL.data(), left, sizeof (float) * (size_t) count);
    std::memcpy (dryR.data(), right, sizeof (float) * (size_t) count);

    // ---- wet path -------------------------------------------------------
    const int bandCount = params.multiband ? std::max (2, std::min (kNumBands, params.bandCount)) : 1;

    if (params.multiband)
    {
        bool anySolo = false;

        for (int b = 0; b < bandCount; ++b)
            if (params.band[b].solo)
                anySolo = true;

        splitter.process (left, right, count, bandCount, bandLeftPtrs.data(), bandRightPtrs.data(), params.xover);

        std::memset (left, 0, sizeof (float) * (size_t) count);
        std::memset (right, 0, sizeof (float) * (size_t) count);

        for (int b = 0; b < bandCount; ++b)
        {
            const auto& band = params.band[b];

            if (! band.enable)
                continue;

            if (anySolo && ! band.solo)
                continue;

            std::memcpy (workL.data(), bandL[(size_t) b].data(), sizeof (float) * (size_t) count);
            std::memcpy (workR.data(), bandR[(size_t) b].data(), sizeof (float) * (size_t) count);

            runChain (b, workL.data(), workR.data(), count, &band);

            const double level = dbToGainF (band.levelDb);
            const double pan = clampd ((double) band.pan * 0.01, -1.0, 1.0);
            const double panL = std::sqrt (0.5 * (1.0 - pan));
            const double panR = std::sqrt (0.5 * (1.0 + pan));

            for (int n = 0; n < count; ++n)
            {
                left[n] += (float) sanitise ((double) workL[(size_t) n] * level * panL * 1.41421356);
                right[n] += (float) sanitise ((double) workR[(size_t) n] * level * panR * 1.41421356);
            }
        }
    }
    else
    {
        std::memcpy (workL.data(), left, sizeof (float) * (size_t) count);
        std::memcpy (workR.data(), right, sizeof (float) * (size_t) count);

        runChain (0, workL.data(), workR.data(), count, nullptr);

        std::memcpy (left, workL.data(), sizeof (float) * (size_t) count);
        std::memcpy (right, workR.data(), sizeof (float) * (size_t) count);
    }

    // ---- DC blocking on each filter output ------------------------------
    if (params.dcFilter)
    {
        chainDcL.setCutoff (20.0, sampleRate);
        chainDcR.setCutoff (20.0, sampleRate);

        for (int n = 0; n < count; ++n)
        {
            left[n] = (float) sanitise (chainDcL.highPass (0, (double) left[n]));
            right[n] = (float) sanitise (chainDcR.highPass (1, (double) right[n]));
        }
    }

    // ---- global saturation ----------------------------------------------
    if (params.saturation > 0.01f)
    {
        const double wet = clampd (params.saturation * 0.01, 0.0, 1.0) * 0.85;
        const double gain = std::pow (10.0, (params.saturation * 0.18) / 20.0);

        for (int n = 0; n < count; ++n)
        {
            left[n] = (float) sanitise (softSaturate ((double) left[n] * gain, wet) / gain * 0.9 + (double) left[n] * 0.1);
            right[n] = (float) sanitise (softSaturate ((double) right[n] * gain, wet) / gain * 0.9 + (double) right[n] * 0.1);
        }
    }

    // ---- output gain, clipper, dry/wet ----------------------------------
    outGainSmooth.setTarget (dbToGainF (params.outGainDb));
    dryWetSmooth.setTarget (params.dryWet);

    for (int n = 0; n < count; ++n)
    {
        const float gain = outGainSmooth.next();
        const float mix = dryWetSmooth.next() * 0.01f;

        float wl = left[n] * gain;
        float wr = right[n] * gain;

        if (params.clipping)
        {
            wl = clampf (wl, -1.0f, 1.0f);
            wr = clampf (wr, -1.0f, 1.0f);
        }

        left[n] = dryL[(size_t) n] * (1.0f - mix) + wl * mix;
        right[n] = dryR[(size_t) n] * (1.0f - mix) + wr * mix;
    }

    // ---- output metering ------------------------------------------------
    float mid = 0.0f, side = 0.0f;

    for (int n = 0; n < count; ++n)
    {
        const float a = std::fabs (left[n]);
        const float b = std::fabs (right[n]);

        if (a > outputPeak[0]) outputPeak[0] = a;
        if (b > outputPeak[1]) outputPeak[1] = b;

        mid += 0.5f * (left[n] + right[n]);
        side += 0.5f * (left[n] - right[n]);
    }

    if (count > 0)
    {
        mid /= (float) count;
        side /= (float) count;
        const float m = std::fabs (mid);
        const float w = m > 1.0e-6f ? std::fabs (side) / m : 0.0f;
        widthAccum += w;
        ++widthCount;
    }
}

} // namespace wob
