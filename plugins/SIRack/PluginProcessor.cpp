#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sidechain.h"

using namespace juce;
using namespace si::rack;

namespace
{
String fmtDb (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }
String fmtPct (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }

struct M { int type; int mode; std::vector<float> real; };   // real knob values (stepped: the shown value)

si::FactoryPreset rack (const String& name, std::initializer_list<M> mods)
{
    si::FactoryPreset p { name, {} };
    int s = 0;
    for (auto& m : mods)
    {
        p.values.push_back ({ SIRackProcessor::pid (s, "type"), (float) m.type });
        p.values.push_back ({ SIRackProcessor::pid (s, "on"), 1.0f });
        p.values.push_back ({ SIRackProcessor::pid (s, "mode"), (float) m.mode });
        for (int k = 0; k < kParams; ++k)
        {
            const float norm = k < (int) m.real.size() ? knobNorm (m.type, k, m.real[(size_t) k]) : knobDefaultNorm (m.type, k);
            p.values.push_back ({ SIRackProcessor::pid (s, "p" + String (k + 1)), norm });
        }
        ++s;
    }
    return p;
}

std::vector<si::FactoryPreset> factoryPresets()
{
    return {
        { "Init", {} },
        rack ("Vocal Chain",       { { EQ73, 0, { 80, 110, -1, 3200, 2, 3 } }, { FET76, 0, { 18, 0, 5, 5, 0, 100 } }, { Opto2A, 0, { 25, 2, 0, 100 } },
                                     { DeEsser, 0, { 6500, -26, 8 } }, { Exciter, 0, { 30, 10, 0 } } }),
        rack ("Rap Vocal Aggro",   { { FET76, 1, { 26, -3, 6, 6, 1, 100 } }, { EQ73, 0, { 160, 110, 0, 3200, 3, 2 } }, { DeEsser, 0, { 7000, -28, 10 } },
                                     { Console, 1, { 40, 0 } }, { Exciter, 1, { 40, 0, -1 } } }),
        rack ("R&B Smooth Vocal",  { { EQP1A, 0, { 100, 2, 3, 10000, 4, 0 } }, { Opto2A, 0, { 35, 3, 10, 100 } }, { Tape, 1, { 20, 0, 0, 0 } }, { DeEsser, 0, { 6000, -28, 6 } } }),
        rack ("Drum Bus",          { { BusVCA, 0, { -14, 4, 10, -1, 3, 100 } }, { Console, 0, { 45, 0 } }, { Tape, 0, { 35, 0, 5, 0 } } }),
        rack ("Drum Smash",        { { FET76, 1, { 34, -10, 7, 7, 4, 45 } }, { Transient, 0, { 30, -20, 0, 100 } }, { Console, 1, { 50, -1 } } }),
        rack ("Mix Bus Glue",      { { EQP1A, 0, { 60, 2, 1.5f, 12000, 2, 0 } }, { BusVCA, 0, { -12, 2, 30, -1, 2, 100 } }, { Console, 2, { 20, 0 } }, { Limiter, 0, { 2, -1, 120 } } }),
        rack ("Bass Weight",       { { EQP1A, 0, { 60, 4, 3, 3000, 0, 0 } }, { FET76, 0, { 20, -2, 3, 4, 0, 100 } }, { Console, 0, { 40, 0 } }, { Width, 0, { 100, 120, 0 } } }),
        rack ("Master Lite",       { { EQP1A, 0, { 30, 1.5f, 1, 16000, 1.5f, 0 } }, { BusVCA, 0, { -8, 2, 30, -1, 1, 100 } }, { Tape, 1, { 15, 0, 0, 0 } },
                                     { Width, 0, { 110, 120, 0 } }, { Limiter, 1, { 4, -0.5f, 80 } } }),
        rack ("Podcast Voice",     { { Gate, 0, { -50, 30, 1, 60, 200 } }, { EQ73, 0, { 80, 220, -2, 3200, 2, 1 } }, { Opto2A, 0, { 40, 4, 0, 100 } },
                                     { DeEsser, 1, { 6500, -26, 6 } }, { Limiter, 0, { 3, -1, 100 } } }),
        rack ("Acoustic Guitar",   { { EQ73, 0, { 80, 220, -2, 4800, 1.5f, 3 } }, { Opto2A, 0, { 20, 2, 0, 100 } }, { Exciter, 0, { 25, 0, 0 } } }),
    };
}
} // namespace

AudioProcessorValueTreeState::ParameterLayout SIRackProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    StringArray names;
    for (int t = 0; t < kNumModules; ++t) names.add (moduleDef (t).name);
    for (int s = 0; s < kSlots; ++s)
    {
        const String n = "Slot " + String (s + 1) + " ";
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid (s, "type"), 1 }, n + "Module", names, 0));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid (s, "on"), 1 }, n + "On", true));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid (s, "solo"), 1 }, n + "Solo", false));
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid (s, "mode"), 1 }, n + "Mode", StringArray { "1", "2", "3", "4" }, 0));
        for (int k = 0; k < kParams; ++k)
            v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { pid (s, "p" + String (k + 1)), 1 }, n + "Knob " + String (k + 1),
                                                                NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                                AudioParameterFloatAttributes().withStringFromValueFunction (fmtPct)));
    }
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input", NormalisableRange<float> (-24.0f, 24.0f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (fmtDb)));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 24.0f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (fmtDb)));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "extsc", 1 }, "External Sidechain", false));
    return { v.begin(), v.end() };
}

SIRackProcessor::SIRackProcessor()
    : AudioProcessor (SI_SIDECHAIN_BUSES),
      apvts (*this, nullptr, "SIRack", createLayout()),
      presets (apvts, "SI Rack", factoryPresets(), { "tempo", "extsc" })
{
    for (int s = 0; s < kSlots; ++s)
        for (int t = 1; t < kNumModules; ++t) procs[(size_t) s][(size_t) t] = createProcessor (t);
    lastType.fill (-1);
}

bool SIRackProcessor::isBusesLayoutSupported (const BusesLayout& l) const { return si::sc::layoutOk (l); }

void SIRackProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& slot : procs)
        for (auto& p : slot) if (p) p->prepare (sampleRate);
    lastType.fill (-1);
}

void SIRackProcessor::loadModule (int slot, int type)
{
    auto set = [&] (const String& id, float real) { if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (real)); p->endChangeGesture(); } };
    set (pid (slot, "type"), (float) type);
    set (pid (slot, "on"), 1.0f);
    set (pid (slot, "solo"), 0.0f);
    set (pid (slot, "mode"), 0.0f);
    for (int k = 0; k < kParams; ++k) set (pid (slot, "p" + String (k + 1)), knobDefaultNorm (type, k));
}

void SIRackProcessor::swapSlots (int a, int b)
{
    if (a < 0 || b < 0 || a >= kSlots || b >= kSlots) return;
    StringArray ids { "type", "on", "solo", "mode" };
    for (int k = 0; k < kParams; ++k) ids.add ("p" + String (k + 1));
    for (auto& id : ids)
    {
        auto* pa = apvts.getParameter (pid (a, id));
        auto* pb = apvts.getParameter (pid (b, id));
        const float va = pa->getValue(), vb = pb->getValue();
        pa->beginChangeGesture(); pa->setValueNotifyingHost (vb); pa->endChangeGesture();
        pb->beginChangeGesture(); pb->setValueNotifyingHost (va); pb->endChangeGesture();
    }
}

void SIRackProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    HeapBlock<float> monoTmp;
    float* r;
    const bool stereoMain = getMainBusNumInputChannels() > 1;   // the sidechain channels are not audio to process
    if (stereoMain) r = buffer.getWritePointer (1);
    else { monoTmp.allocate ((size_t) n, false); FloatVectorOperations::copy (monoTmp, l, n); r = monoTmp; }

    const float inT = Decibels::decibelsToGain (apvts.getRawParameterValue ("input")->load());
    const float outT = Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());
    float ipk = 0, opk = 0;
    for (int i = 0; i < n; ++i)
    {
        inSm += 0.002f * (inT - inSm);
        l[i] *= inSm; r[i] *= inSm;
        ipk = jmax (ipk, std::abs (l[i]), std::abs (r[i]));
    }
    bool anySolo = false;
    for (int s = 0; s < kSlots; ++s) anySolo = anySolo || (slotType (s) > 0 && apvts.getRawParameterValue (pid (s, "solo"))->load() > 0.5f);
    const auto key = si::sc::get (*this, buffer, apvts.getRawParameterValue ("extsc")->load() > 0.5f);   // FET / Opto / Bus / Gate can listen to it
    for (int s = 0; s < kSlots; ++s)
    {
        const int type = slotType (s);
        if (type != lastType[(size_t) s])
        {
            if (type > 0) procs[(size_t) s][(size_t) type]->reset();
            lastType[(size_t) s] = type;
        }
        gr[(size_t) s] = 0.0f;
        if (type <= 0 || apvts.getRawParameterValue (pid (s, "on"))->load() < 0.5f) continue;
        if (anySolo && apvts.getRawParameterValue (pid (s, "solo"))->load() < 0.5f) continue;   // solo: hear only the soloed modules
        std::array<float, kParams> knobs;
        for (int k = 0; k < kParams; ++k) knobs[(size_t) k] = apvts.getRawParameterValue (pid (s, "p" + String (k + 1)))->load();
        auto& p = *procs[(size_t) s][(size_t) type];
        p.keyL = key.l;
        p.keyR = key.r;
        p.process (l, r, n, knobs, (int) apvts.getRawParameterValue (pid (s, "mode"))->load());
        gr[(size_t) s] = p.grDb;
    }
    for (int i = 0; i < n; ++i)
    {
        outSm += 0.002f * (outT - outSm);
        l[i] *= outSm; r[i] *= outSm;
        opk = jmax (opk, std::abs (l[i]), std::abs (r[i]));
    }
    if (! stereoMain) FloatVectorOperations::add (l, r, n), FloatVectorOperations::multiply (l, 0.5f, n);
    inLevel = jmax (ipk, inLevel.load() * 0.9f);
    outLevel = jmax (opk, outLevel.load() * 0.9f);
}

AudioProcessorEditor* SIRackProcessor::createEditor() { return new SIRackEditor (*this); }

void SIRackProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SIRackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SIRackProcessor(); }
