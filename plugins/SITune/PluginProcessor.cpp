#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
String fmtMs (float v, int) { return v < 0.5f ? String ("Hard") : String (roundToInt (v)) + " ms"; }
String fmtPct (float v, int) { return String (roundToInt (v)) + "%"; }
String fmtSt (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v)) + " st"; }
String fmtHz (float v, int) { return String (v, 1) + " Hz"; }
String fmtVib (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }

// interval patterns (semitones from the root)
const std::vector<std::vector<int>> kScales = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },   // Chromatic
    { 0, 2, 4, 5, 7, 9, 11 },                   // Major
    { 0, 2, 3, 5, 7, 8, 10 },                   // Minor
    { 0, 2, 3, 5, 7, 8, 11 },                   // Harmonic Minor
    { 0, 2, 4, 7, 9 },                          // Major Pentatonic
    { 0, 3, 5, 7, 10 },                         // Minor Pentatonic
    { 0, 3, 5, 6, 7, 10 },                      // Blues
    { 0, 2, 3, 5, 7, 9, 10 },                   // Dorian
    { 0, 1, 3, 5, 7, 8, 10 },                   // Phrygian
    { 0, 2, 4, 5, 7, 9, 10 },                   // Mixolydian
};

std::vector<si::FactoryPreset> factoryPresets()
{
    return {
        { "Init", {} },
        { "Auto-Tune Style",   { { "speed", 10 },  { "amount", 100 }, { "humanize", 10 }, { "stab", 0 }, { "flex", 4 }, { "natvib", 0 } } },
        { "Natural Pop",       { { "speed", 60 },  { "amount", 100 }, { "humanize", 40 }, { "stab", 1 }, { "flex", 30 } } },
        { "Hard Tune",         { { "speed", 0 },   { "amount", 100 }, { "humanize", 0 },  { "stab", 3 }, { "natvib", -12 } } },
        { "Trap Melodic",      { { "speed", 8 },   { "amount", 100 }, { "humanize", 0 },  { "stab", 2 } } },
        { "R&B Smooth",        { { "speed", 35 },  { "amount", 100 }, { "humanize", 25 }, { "stab", 2 }, { "flex", 20 }, { "natvib", 3 } } },
        { "Subtle Fix",        { { "speed", 120 }, { "amount", 70 },  { "humanize", 60 }, { "stab", 1 } } },
        { "Drill Glide",       { { "speed", 15 },  { "amount", 100 }, { "humanize", 10 }, { "stab", 1 } } },
        { "Robot Choir",       { { "speed", 0 },   { "amount", 100 }, { "stab", 3 },      { "lowlat", 0 } } },
        { "Octave Down",       { { "speed", 20 },  { "transpose", -12 } } },
        { "Chipmunk",          { { "speed", 20 },  { "transpose", 12 } } },
        { "Live Monitoring",   { { "speed", 25 },  { "amount", 100 }, { "humanize", 20 }, { "lowlat", 1 } } },
    };
}
} // namespace

const StringArray& SITuneProcessor::scaleNames()
{
    static const StringArray s { "Chromatic", "Major", "Minor", "Harm. Minor", "Major Pent.", "Minor Pent.", "Blues", "Dorian", "Phrygian", "Mixolydian", "Custom" };
    return s;
}

int SITuneProcessor::scaleMask (int key, int scale)
{
    int m = 0;
    for (int iv : kScales[(size_t) jlimit (0, (int) kScales.size() - 1, scale)]) m |= 1 << ((key + iv) % 12);
    return m;
}

int SITuneProcessor::currentMask() const
{
    const int scale = (int) apvts.getRawParameterValue ("scale")->load();
    if (scale < (int) kScales.size()) return scaleMask ((int) apvts.getRawParameterValue ("key")->load(), scale);
    int m = 0;
    for (int i = 0; i < 12; ++i)
        if (apvts.getRawParameterValue ("n" + String (i))->load() > 0.5f) m |= 1 << i;
    return m;
}

AudioProcessorValueTreeState::ParameterLayout SITuneProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, String (*fmt) (float, int))
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (fmt))); };
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
                                                         StringArray { "C", "C# / Db", "D", "D# / Eb", "E", "F", "F# / Gb", "G", "G# / Ab", "A", "A# / Bb", "B" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames(), 0));
    for (int i = 0; i < 12; ++i)
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "n" + String (i), 1 }, "Note " + String (i), true));
    NormalisableRange<float> sp (0.0f, 400.0f);
    sp.setSkewForCentre (40.0f);
    f ("speed", "Speed", sp, 10.0f, fmtMs);
    f ("amount", "Amount", { 0.0f, 100.0f }, 100.0f, fmtPct);
    f ("humanize", "Humanize", { 0.0f, 100.0f }, 10.0f, fmtPct);
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "stab", 1 }, "Note Stabilizer", StringArray { "None", "Short", "Mid", "Long" }, 0));
    f ("transpose", "Transpose", { -12.0f, 12.0f, 1.0f }, 0.0f, fmtSt);
    f ("ref", "Pitch Reference", { 430.0f, 450.0f, 0.1f }, 440.0f, fmtHz);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "lowlat", 1 }, "Zero Latency", true));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "inputType", 1 }, "Input Type",
                                                         StringArray { "Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass Instrument" }, 1));
    f ("tracking", "Tracking", { 0.0f, 100.0f }, 50.0f, fmtPct);
    f ("flex", "Flex Tune", { 0.0f, 100.0f }, 4.0f, fmtPct);
    f ("natvib", "Natural Vibrato", { -12.0f, 12.0f, 0.1f }, 0.0f, fmtVib);
    return { v.begin(), v.end() };
}

SITuneProcessor::SITuneProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SITune", createLayout()),
      presets (apvts, "SI Tune", factoryPresets(), { "key", "scale", "n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8", "n9", "ref", "inputType", "tracking" })
{
}

bool SITuneProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == AudioChannelSet::stereo() || out == AudioChannelSet::mono()) && l.getMainInputChannelSet() == out;
}

void SITuneProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    si::tune::TuneParams p;
    p.lowLatency = apvts.getRawParameterValue ("lowlat")->load() > 0.5f;
    engine.setParams (p);
    engine.prepare (sampleRate);
    lastLowLat = p.lowLatency;
    setLatencySamples (engine.latencySamples());
}

void SITuneProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    si::tune::TuneParams p;
    p.scaleMask = currentMask();
    p.speedMs = v ("speed");
    p.amount = v ("amount") * 0.01f;
    p.humanize = v ("humanize") * 0.01f;
    p.stabilizer = 0;   // like Auto-Tune Pro: Retune Speed alone decides how notes move
    p.transpose = v ("transpose");
    p.refHz = v ("ref");
    p.lowLatency = v ("lowlat") > 0.5f;
    p.inputType = (int) v ("inputType");
    p.tracking = v ("tracking") * 0.01f;
    p.flex = v ("flex") * 0.01f;
    p.vibratoDb = v ("natvib");
    engine.setParams (p);
    if (p.lowLatency != lastLowLat)
    {
        lastLowLat = p.lowLatency;
        setLatencySamples (engine.latencySamples());
    }

    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    if (buffer.getNumChannels() > 1) engine.process (l, buffer.getWritePointer (1), n);
    else
    {
        HeapBlock<float> tmp (n);
        FloatVectorOperations::copy (tmp, l, n);
        engine.process (l, tmp, n);
    }
}

AudioProcessorEditor* SITuneProcessor::createEditor() { return new SITuneEditor (*this); }

void SITuneProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SITuneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SITuneProcessor(); }
