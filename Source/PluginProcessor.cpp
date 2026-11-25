#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
juce::String makeParameterId (int index)
{
    return "p" + juce::String (index);
}

// Non-automatable oscillator programming lives here rather than in the
// parameter tree: curves and 256-partial spectra are far too large to expose as
// host parameters.
const juce::Identifier kLfoProgramTag { "LFOPROGRAM" };
const juce::Identifier kCurveTag { "CURVE" };
const juce::Identifier kStepsTag { "STEPS" };
const juce::Identifier kHarmLevelTag { "HARMLEVEL" };
const juce::Identifier kHarmPhaseTag { "HARMPHASE" };
const juce::Identifier kStepCountTag { "STEPCOUNT" };
} // namespace

//==============================================================================
MKWoplerAudioProcessor::MKWoplerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", [this]
      {
          juce::AudioProcessorValueTreeState::ParameterLayout layout;
          buildParameterLayout (layout);
          return layout;
      }())
{
    const auto& table = wob::ParamTable::get();

    rawParameters.resize (table.all().size(), nullptr);

    for (size_t i = 0; i < table.all().size(); ++i)
        rawParameters[i] = apvts.getRawParameterValue (makeParameterId ((int) i));

    for (auto& program : lfoPrograms)
        program = wob::LfoProgram();

    updateEngineParameters();
}

MKWoplerAudioProcessor::~MKWoplerAudioProcessor() = default;

//==============================================================================
void MKWoplerAudioProcessor::buildParameterLayout (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    const auto& table = wob::ParamTable::get();

    for (size_t i = 0; i < table.all().size(); ++i)
    {
        const auto& spec = table.all()[i];
        const auto id = makeParameterId ((int) i);
        const auto name = juce::String (spec.name);

        switch (spec.kind)
        {
            case wob::ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    juce::ParameterID { id, 1 }, name, spec.defv > 0.5f));
                break;

            case wob::ParamKind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    juce::ParameterID { id, 1 }, name,
                    (int) spec.minv, (int) spec.maxv, (int) spec.defv));
                break;

            case wob::ParamKind::Choice:
            {
                juce::StringArray choices;

                // The filter type list is the only very large choice set.
                if ((int) i >= 0 && spec.numChoices == wob::kNumFilterTypes)
                {
                    for (int t = 0; t < wob::kNumFilterTypes; ++t)
                        choices.add (wob::kFilterTypeNames[t]);
                }
                else if (spec.numChoices == wob::kNumNoteLengths)
                {
                    for (int t = 0; t < wob::kNumNoteLengths; ++t)
                        choices.add (wob::kNoteLengthNames[t]);
                }
                else if (spec.numChoices == 7 && spec.id.find ("fEqType") != std::string::npos)
                {
                    choices.addArray ({ "Off", "Low-pass", "High-pass", "Band-pass",
                                        "Low shelf", "High shelf", "Peak" });
                }
                else if (spec.numChoices == wob::kNumFilterTypes + 1)
                {
                    choices.add ("Inherit");

                    for (int t = 0; t < wob::kNumFilterTypes; ++t)
                        choices.add (wob::kFilterTypeNames[t]);
                }
                else
                {
                    for (int t = 0; t < spec.numChoices; ++t)
                        choices.add ("Option " + juce::String (t + 1));
                }

                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { id, 1 }, name, choices, (int) spec.defv));
                break;
            }

            case wob::ParamKind::Float:
            default:
            {
                juce::NormalisableRange<float> range (spec.minv, spec.maxv, 0.0f, spec.skew);
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    juce::ParameterID { id, 1 }, name, range, spec.defv));
                break;
            }
        }
    }
}

//==============================================================================
void MKWoplerAudioProcessor::setCustomCurvePoint (int processorIndex, int pointIndex, float value)
{
    processorIndex = processorIndex == 1 ? 1 : 0;

    if (pointIndex < 0 || pointIndex >= wob::LfoProgram::kCurvePoints)
        return;

    lfoPrograms[processorIndex].customShape[(size_t) pointIndex] = juce::jlimit (0.0f, 1.0f, value);
    engine.setLfoProgram (processorIndex, lfoPrograms[processorIndex]);
}

void MKWoplerAudioProcessor::setStepValue (int processorIndex, int stepIndex, float value)
{
    processorIndex = processorIndex == 1 ? 1 : 0;

    if (stepIndex < 0 || stepIndex >= wob::LfoProgram::kMaxSteps)
        return;

    lfoPrograms[processorIndex].steps[(size_t) stepIndex] = juce::jlimit (0.0f, 1.0f, value);
    engine.setLfoProgram (processorIndex, lfoPrograms[processorIndex]);
}

void MKWoplerAudioProcessor::setStepCount (int processorIndex, int count)
{
    processorIndex = processorIndex == 1 ? 1 : 0;
    lfoPrograms[processorIndex].stepCount = juce::jlimit (1, wob::LfoProgram::kMaxSteps, count);
    engine.setLfoProgram (processorIndex, lfoPrograms[processorIndex]);
}

void MKWoplerAudioProcessor::convertShapeToHarmonics (int processorIndex)
{
    processorIndex = processorIndex == 1 ? 1 : 0;

    // Analysis of the current shape into 256 partials, matching the manual's
    // "Convert" button: level and phase of each harmonic.
    auto& program = lfoPrograms[processorIndex];
    const auto* cursor = rawParameters[(size_t) wob::ParamTable::procIndex (processorIndex, wob::ppLShape)];
    const float shapePos = cursor != nullptr ? cursor->load() : 5.0f;

    wob::Lfo analyser;
    analyser.prepare (4096.0 * 4.0);
    analyser.setProgram (&program);
    analyser.reset();
    analyser.update (1.0f, false, 0, 120.0, 0.0f, 0.0f, false, shapePos, 0.0f, 0.0f, 0.0f, false);

    const int n = 4096;
    std::vector<float> buf ((size_t) n, 0.0f);
    std::vector<float> bufR ((size_t) n, 0.0f);
    analyser.render (buf.data(), bufR.data(), n);

    double magnitudes[wob::LfoProgram::kMaxHarmonics] {};
    double phases[wob::LfoProgram::kMaxHarmonics] {};
    double peak = 0.0;

    for (int h = 1; h <= wob::LfoProgram::kMaxHarmonics; ++h)
    {
        double re = 0.0, im = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double angle = 2.0 * wob::kPi * (double) h * (double) i / (double) n;
            re += (double) buf[(size_t) i] * std::cos (angle);
            im += (double) buf[(size_t) i] * std::sin (angle);
        }

        magnitudes[h - 1] = std::sqrt (re * re + im * im) * 2.0 / (double) n;
        phases[h - 1] = std::atan2 (im, re);
        peak = std::max (peak, magnitudes[h - 1]);
    }

    for (int h = 0; h < wob::LfoProgram::kMaxHarmonics; ++h)
    {
        program.harmLevel[(size_t) h] = peak > 1.0e-9 ? (float) (magnitudes[h] / peak) : 0.0f;

        double p = phases[h] / (2.0 * wob::kPi);
        p -= std::floor (p);
        program.harmPhase[(size_t) h] = (float) p;
    }

    engine.setLfoProgram (processorIndex, program);
}

//==============================================================================
void MKWoplerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (32, samplesPerBlock);

    pendingOversampling.store (juce::jlimit (1, 4, pendingOversampling.load()));
    activeOversampling = 0;          // force a rebuild
    configureDsp();

    monoBuffer.setSize (2, samplesPerBlock);

    updateEngineParameters();
    wasPlaying = false;
}

void MKWoplerAudioProcessor::releaseResources()
{
    oversampler.reset();
}

bool MKWoplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo() && mainOut != juce::AudioChannelSet::mono())
        return false;

    if (mainIn != juce::AudioChannelSet::stereo() && mainIn != juce::AudioChannelSet::mono())
        return false;

    // Mono in to stereo out is common on iPadOS hosts and is supported by the
    // mono path in processBlock; anything wider is not.
    return mainIn.size() <= mainOut.size();
}

void MKWoplerAudioProcessor::setOversamplingFactor (int factor)
{
    factor = juce::jlimit (1, 4, factor);

    if (factor == 3)
        factor = 4;

    pendingOversampling.store (factor);
}

void MKWoplerAudioProcessor::configureDsp()
{
    if (currentSampleRate <= 0.0)
        return;

    const int factor = juce::jlimit (1, 4, pendingOversampling.load());
    activeOversampling = factor;

    if (factor <= 1)
    {
        oversampler.reset();
    }
    else
    {
        const int order = factor >= 4 ? 2 : 1;

        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            2, order, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);

        oversampler->initProcessing ((size_t) juce::jmax (64, currentBlockSize));
    }

    setLatencySamples (oversampler != nullptr ? (int) std::lround (oversampler->getLatencyInSamples()) : 0);

    // The oscillators and filters must run at the oversampled rate, so the
    // engine is prepared with the effective rate rather than the host rate.
    const double effectiveRate = currentSampleRate * (double) factor;

    engine.prepare (effectiveRate, currentBlockSize * factor);

    for (int p = 0; p < wob::kNumProcessors; ++p)
        engine.setLfoProgram (p, lfoPrograms[p]);

    engine.reset();
}

//==============================================================================
void MKWoplerAudioProcessor::updateEngineParameters()
{
    const auto& table = wob::ParamTable::get();
    auto value = [this, &table] (int index)
    {
        auto* p = rawParameters[(size_t) index];
        return p != nullptr ? p->load() : table[(size_t) index].defv;
    };

    wob::EngineParams p;

    p.dryWet = value (wob::ParamTable::globalIndex (wob::gpDryWet));
    p.saturation = value (wob::ParamTable::globalIndex (wob::gpSaturation));
    p.outGainDb = value (wob::ParamTable::globalIndex (wob::gpOutputGain));
    p.clipping = value (wob::ParamTable::globalIndex (wob::gpClipping)) > 0.5f;
    p.dcFilter = value (wob::ParamTable::globalIndex (wob::gpDcFilter)) > 0.5f;
    p.multiband = value (wob::ParamTable::globalIndex (wob::gpMultiband)) > 0.5f;
    p.bandCount = (int) std::lround (value (wob::ParamTable::globalIndex (wob::gpBandCount)));
    p.keyTrack = value (wob::ParamTable::globalIndex (wob::gpKeyTrack));

    for (int i = 0; i < wob::kNumCrossovers; ++i)
        p.xover[i] = value (wob::ParamTable::globalIndex (wob::gpXover1 + i));

    for (int i = 0; i < wob::kNumProcessors; ++i)
    {
        auto& q = p.proc[i];
        auto v = [&] (int param) { return value (wob::ParamTable::procIndex (i, param)); };

        q.drive = v (wob::ppDrive);
        q.driveMode = v (wob::ppDriveMode);
        q.saturation = v (wob::ppSaturation);
        q.outGainDb = v (wob::ppOutGain);
        q.clip = v (wob::ppClip) > 0.5f;
        q.type = (int) std::lround (v (wob::ppType));
        q.freq = v (wob::ppFreq);
        q.freqRange = v (wob::ppFreqRange);
        q.resonance = v (wob::ppResonance);
        q.resRange = v (wob::ppResRange);
        q.gainDb = v (wob::ppGain);
        q.gainRange = v (wob::ppGainRange);
        q.character = v (wob::ppCharacter);
        q.charRange = v (wob::ppCharRange);
        q.panorama = v (wob::ppPanorama);
        q.trim = v (wob::ppTrim);
        q.quality = v (wob::ppQuality);

        q.fDepth = v (wob::ppFDepth);
        q.fAttackMs = v (wob::ppFAttack);
        q.fReleaseMs = v (wob::ppFRelease);
        q.fRmsMs = v (wob::ppFRms);
        q.fMaxLevelDb = v (wob::ppFMaxLevel);
        q.fSidechain = v (wob::ppFSidechain) > 0.5f;
        q.fEqType = (int) std::lround (v (wob::ppFEqType));
        q.fEqFreq = v (wob::ppFEqFreq);
        q.fEqGainDb = v (wob::ppFEqGain);
        q.fEqQ = v (wob::ppFEqQ);

        q.lRate = v (wob::ppLRate);
        q.lWidth = v (wob::ppLWidth);
        q.lOverride = v (wob::ppLOverride);
        q.lInvert = v (wob::ppLInvert) > 0.5f;
        q.lSync = v (wob::ppLSync) > 0.5f;
        q.lNoteLength = (int) std::lround (v (wob::ppLNoteLength));
        q.lPhase = v (wob::ppLPhase);
        q.lShape = v (wob::ppLShape);
        q.lCustom = v (wob::ppLCustom);
        q.lStep = v (wob::ppLStep);
        q.lSmooth = v (wob::ppLSmooth);
        q.lHarmonicMode = v (wob::ppLHarmonics) > 0.5f;
        q.lMidiReset = v (wob::ppLMidiReset) > 0.5f;
    }

    for (int b = 0; b < wob::kNumBands; ++b)
    {
        auto& q = p.band[b];
        auto v = [&] (int param) { return value (wob::ParamTable::bandIndex (b, param)); };

        q.enable = v (wob::bpEnable) > 0.5f;
        q.mute = v (wob::bpMute) > 0.5f;
        q.solo = v (wob::bpSolo) > 0.5f;
        q.levelDb = v (wob::bpLevel);
        q.pan = v (wob::bpPan);
        q.type = (int) std::lround (v (wob::bpType)) - 1;   // 0 == inherit
        q.freq = v (wob::bpFreq) > 0.0f ? v (wob::bpFreq) : -1.0f;
        q.freqRange = v (wob::bpFreqRange);
        q.resonance = v (wob::bpResonance);
        q.resRange = v (wob::bpResRange);
        q.gainDb = v (wob::bpGain);
        q.character = v (wob::bpCharacter);
        q.drive = v (wob::bpDrive);
        q.outGainDb = v (wob::bpOutGain);
        q.trim = v (wob::bpTrim);
        q.lPhaseOffset = v (wob::bpLPhaseOffset);
        q.lRateMult = v (wob::bpLRateMult);
        q.lWidth = v (wob::bpLWidth);
    }

    engine.setParams (p);
}

//==============================================================================
void MKWoplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // Oversampling is a user choice, not an automation target, so it is rebuilt
    // here at block boundaries when it changes. This allocates, which is why the
    // control is deliberately kept off the automation list.
    if (pendingOversampling.load() != activeOversampling)
        configureDsp();

    updateEngineParameters();

    if (auto* head = getPlayHead())
    {
        if (auto position = head->getPosition())
        {
            if (auto bpm = position->getBpm())
                engine.setTempo (*bpm);

            const bool playing = position->getIsPlaying();

            if (playing && ! wasPlaying)
                engine.resetLfoPhases();

            wasPlaying = playing;
            engine.setTransportPlaying (playing);
        }
    }

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            lastMidiNote = message.getNoteNumber();
    }

    engine.setMidiNote (lastMidiNote);

    // ---- side chain ------------------------------------------------------
    // Extra channels beyond the main bus belong to the side-chain bus when the
    // host has activated it.
    const int mainInChannels = juce::jmax (1, getMainBusNumInputChannels());

    if (buffer.getNumChannels() >= mainInChannels + 2)
        engine.setSidechain (buffer.getReadPointer (mainInChannels),
                             buffer.getReadPointer (mainInChannels + 1));
    else
        engine.setSidechain (nullptr, nullptr);

    // ---- oversampled processing -----------------------------------------
    const int mainChannels = juce::jlimit (1, 2, getMainBusNumInputChannels());
    const int totalChannels = buffer.getNumChannels();

    if (mainChannels >= 2 && totalChannels >= 2)
    {
        if (oversampler != nullptr)
        {
            // Restrict the block to the main bus: any extra channels belong to
            // the side-chain and must not be oversampled or processed.
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) numSamples);

            auto upBlock = oversampler->processSamplesUp (block);

            float* left = upBlock.getChannelPointer (0);
            float* right = upBlock.getChannelPointer (1);

            engine.process (left, right, (int) upBlock.getNumSamples());

            oversampler->processSamplesDown (block);
        }
        else
        {
            engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        }
    }
    else
    {
        // Mono in: run a silent right channel so the stereo filter paths and the
        // stereo metering stay meaningful.
        monoBuffer.setSize (2, numSamples, false, false, true);
        monoBuffer.copyFrom (0, 0, buffer, 0, 0, numSamples);
        monoBuffer.clear (1, 0, numSamples);

        engine.process (monoBuffer.getWritePointer (0), monoBuffer.getWritePointer (1), numSamples);

        buffer.copyFrom (0, 0, monoBuffer, 0, 0, numSamples);
    }

    meterIn[0].store (engine.inputPeak[0]);
    meterIn[1].store (engine.inputPeak[1]);
    meterOut[0].store (engine.outputPeak[0]);
    meterOut[1].store (engine.outputPeak[1]);
    meterWidth.store (engine.widthMeter);
}

//==============================================================================
void MKWoplerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    auto programs = juce::ValueTree ("LFOPROGRAMS");

    for (int p = 0; p < wob::kNumProcessors; ++p)
    {
        juce::ValueTree program (kLfoProgramTag);
        const auto& source = lfoPrograms[p];

        juce::String curve, steps, levels, phases;

        for (auto v : source.customShape)
            curve << juce::String (v, 6) << ',';

        for (auto v : source.steps)
            steps << juce::String (v, 6) << ',';

        for (auto v : source.harmLevel)
            levels << juce::String (v, 6) << ',';

        for (auto v : source.harmPhase)
            phases << juce::String (v, 6) << ',';

        program.setProperty (kCurveTag, curve, nullptr);
        program.setProperty (kStepsTag, steps, nullptr);
        program.setProperty (kHarmLevelTag, levels, nullptr);
        program.setProperty (kHarmPhaseTag, phases, nullptr);
        program.setProperty (kStepCountTag, source.stepCount, nullptr);

        programs.appendChild (program, nullptr);
    }

    state.appendChild (programs, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MKWoplerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    if (! state.isValid())
        return;

    apvts.replaceState (state);

    auto programs = state.getChildWithName ("LFOPROGRAMS");

    auto readList = [] (const juce::String& text, float* dest, int count)
    {
        juce::StringArray tokens;
        tokens.addTokens (text, ",", "");
        tokens.removeEmptyStrings();

        for (int i = 0; i < count && i < tokens.size(); ++i)
            dest[i] = tokens[i].getFloatValue();
    };

    for (int p = 0; p < wob::kNumProcessors && p < programs.getNumChildren(); ++p)
    {
        auto program = programs.getChild (p);
        auto& target = lfoPrograms[p];

        readList (program.getProperty (kCurveTag).toString(), target.customShape.data(),
                  wob::LfoProgram::kCurvePoints);
        readList (program.getProperty (kStepsTag).toString(), target.steps.data(),
                  wob::LfoProgram::kMaxSteps);
        readList (program.getProperty (kHarmLevelTag).toString(), target.harmLevel.data(),
                  wob::LfoProgram::kMaxHarmonics);
        readList (program.getProperty (kHarmPhaseTag).toString(), target.harmPhase.data(),
                  wob::LfoProgram::kMaxHarmonics);

        target.stepCount = (int) program.getProperty (kStepCountTag, 8);
        engine.setLfoProgram (p, target);
    }

    if (auto* param = rawParameters[(size_t) wob::ParamTable::globalIndex (wob::gpOversample)])
        setOversamplingFactor ((int) std::lround (param->load()));
}

//==============================================================================
juce::AudioProcessorEditor* MKWoplerAudioProcessor::createEditor()
{
    return new MKWoplerAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MKWoplerAudioProcessor();
}
