#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sidechain.h"
#include "Dial.h"

using namespace juce;

//============================================================================== plugin
namespace
{
String fmtAtt (float v, int) { return si76::attText (v); }
String fmtPct (float v, int) { return String (roundToInt (v)) + "%"; }
String fmtKnob (float v, int) { return String (v, 1); }

std::vector<si::FactoryPreset> factoryPresets()
{
    // input / output written as gain in dB below, converted to 1176 dial positions at the end
    std::vector<si::FactoryPreset> list {
        { "Init", {} },
        { "Vocal Grab",       { { "input", 22 }, { "output", -4 }, { "attack", 3 }, { "release", 5 }, { "ratio", 0 }, { "color", 30 } } },
        { "All Good Vocal",         { { "model", 0 }, { "input", 12 }, { "output", 0 }, { "attack", 5 }, { "release", 6 }, { "ratio", 0 }, { "color", 30 }, { "mix", 100 } } },   // UAD: input 36, output 12
        { "All Good Vocal (Black)", { { "model", 1 }, { "input", 12 }, { "output", 0 }, { "attack", 5 }, { "release", 6 }, { "ratio", 0 }, { "color", 40 }, { "mix", 100 } } },
        { "Black Rap Vocal",  { { "model", 1 }, { "input", 28 }, { "output", -7 }, { "attack", 6 }, { "release", 6 }, { "ratio", 1 }, { "color", 60 }, { "schpf", 2 } } },
        { "Blue Smooth Vocal",{ { "model", 0 }, { "input", 20 }, { "output", -3 }, { "attack", 3 }, { "release", 4 }, { "ratio", 0 }, { "color", 25 } } },
        { "Rap Vocal Aggro",  { { "model", 1 }, { "input", 30 }, { "output", -8 }, { "attack", 5 }, { "release", 6 }, { "ratio", 1 }, { "color", 50 } } },
        { "Drum Smash (All)", { { "model", 1 }, { "input", 34 }, { "output", -10 }, { "attack", 7 }, { "release", 7 }, { "ratio", 4 }, { "mix", 45 }, { "color", 70 } } },
        { "Snare Crack",      { { "input", 24 }, { "output", -5 }, { "attack", 1 }, { "release", 7 }, { "ratio", 1 }, { "color", 40 } } },
        { "Bass Control",     { { "input", 18 }, { "output", -3 }, { "attack", 2 }, { "release", 3 }, { "ratio", 0 }, { "color", 40 } } },
        { "808 Thump",        { { "model", 1 }, { "input", 20 }, { "output", -4 }, { "attack", 2 }, { "release", 4 }, { "ratio", 2 }, { "color", 60 } } },
        { "Parallel Room",    { { "input", 36 }, { "output", -8 }, { "attack", 7 }, { "release", 7 }, { "ratio", 4 }, { "mix", 30 }, { "color", 60 } } },
        { "Gentle Leveler",   { { "input", 12 }, { "output", -1 }, { "attack", 2 }, { "release", 2 }, { "ratio", 0 }, { "color", 20 } } },
        { "Mix Bus Glue",     { { "input", 10 }, { "output", -1 }, { "attack", 1 }, { "release", 6 }, { "ratio", 0 }, { "mix", 50 }, { "schpf", 2 } } },
        { "Acoustic Guitar",  { { "input", 16 }, { "output", -2 }, { "attack", 3 }, { "release", 5 }, { "ratio", 0 }, { "color", 25 } } },
    };
    for (auto& p : list)
        for (auto& [id, val] : p.values)
        {
            if (id == "input") val = si76::inputPosForDb (val);
            else if (id == "output") val = si76::outputPosForDb (val);
        }
    return list;
}
} // namespace

AudioProcessorValueTreeState::ParameterLayout SI76Processor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, String (*fmt) (float, int))
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (fmt))); };
    f ("input", "Input", { 0.0f, 1.0f }, si76::attToPos (30.0f), fmtAtt);     // 1176 dial: inf .. 0
    f ("output", "Output", { 0.0f, 1.0f }, si76::attToPos (12.0f), fmtAtt);
    f ("attack", "Attack", { 1.0f, 7.0f }, 3.0f, fmtKnob);
    f ("release", "Release", { 1.0f, 7.0f }, 5.0f, fmtKnob);
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "ratio", 1 }, "Ratio", StringArray { "4", "8", "12", "20", "All" }, 0));
    f ("mix", "Mix", { 0.0f, 100.0f }, 100.0f, fmtPct);
    f ("color", "Color", { 0.0f, 100.0f }, 35.0f, fmtPct);
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "schpf", 1 }, "SC HPF", StringArray { "Off", "60 Hz", "100 Hz", "150 Hz", "250 Hz" }, 0));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "link", 1 }, "Stereo Link", true));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "meter", 1 }, "Meter", StringArray { "GR", "+4", "+8" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "model", 1 }, "Model", StringArray { "Blue", "Black" }, 0));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "extsc", 1 }, "External Sidechain", false));
    return { v.begin(), v.end() };
}

SI76Processor::SI76Processor()
    : AudioProcessor (SI_SIDECHAIN_BUSES),
      apvts (*this, nullptr, "SI76", createLayout()),
      presets (apvts, "SI 76", factoryPresets(), { "meter", "extsc" })
{
}

bool SI76Processor::isBusesLayoutSupported (const BusesLayout& l) const { return si::sc::layoutOk (l); }

void SI76Processor::prepareToPlay (double sampleRate, int) { comp.prepare (sampleRate); }

void SI76Processor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    static const float kHpf[] = { 0.0f, 60.0f, 100.0f, 150.0f, 250.0f };
    comp.inputDb = si76::inputDb (v ("input"));
    comp.outputDb = si76::outputDb (v ("output"));
    comp.attackKnob = v ("attack");
    comp.releaseKnob = v ("release");
    comp.ratio = (int) v ("ratio");
    comp.mix = v ("mix") * 0.01f;
    comp.color = v ("color") * 0.01f;
    comp.scHpf = kHpf[jlimit (0, 4, (int) v ("schpf"))];
    comp.link = v ("link") > 0.5f;
    comp.model = (int) v ("model");
    const auto key = si::sc::get (*this, buffer, v ("extsc") > 0.5f);
    comp.keyL = key.l;
    comp.keyR = key.r;
    const int chs = jmin (buffer.getNumChannels(), getMainBusNumInputChannels());
    const float gr = comp.process (buffer.getWritePointer (0), chs > 1 ? buffer.getWritePointer (1) : nullptr, buffer.getNumSamples(), chs);
    grDb = gr;
    inLevel = comp.inPeak;
    outLevel = comp.outPeak;
}

AudioProcessorEditor* SI76Processor::createEditor() { return new SI76Editor (*this); }

void SI76Processor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SI76Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SI76Processor(); }
