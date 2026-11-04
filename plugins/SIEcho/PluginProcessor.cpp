#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sidechain.h"

using namespace juce;
using namespace si::echo;

namespace
{
String fmtPct (float v, int) { return String (roundToInt (v)) + "%"; }
String fmtBi (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + "%"; }
String fmtHz (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; }
String fmtMs (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (roundToInt (v)) + " ms"; }
String fmtRate (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; }
String fmtDb (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }

NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi, [] (float a, float b, float t) { return a * std::pow (b / a, t); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); }, nullptr };
}

struct Note { const char* n; double beats; };
const Note kNotes[] = { { "1/64", 0.0625 }, { "1/32T", 1.0 / 12 }, { "1/32", 0.125 }, { "1/16T", 1.0 / 6 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
                        { "1/8T", 1.0 / 3 }, { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3 }, { "1/4", 1.0 }, { "1/4D", 1.5 },
                        { "1/2T", 4.0 / 3 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1 Bar", 4.0 }, { "2 Bars", 8.0 } };
enum { N16 = 4, N16D = 5, N8T = 6, N8 = 7, N8D = 8, N4T = 9, N4 = 10, N4D = 11, N2 = 13 };
enum { SINGLE, DUAL, PINGPONG, RHYTHM };
enum { CLEAN, STUDIO, MASTER, CHEAP, SPACE, MEMMAN, DM2, ANALOG, LOFI, PHONE, AM, TRANS, AMBIENT, DIFFTAPE, BRIGHT, DUB };

std::vector<si::FactoryPreset> factoryPresets()
{
    return {
        { "Init", {} },
        { "Vocal Slapback",       { { "mode", SINGLE }, { "sync", 0 }, { "msL", 95 }, { "feedback", 5 }, { "mix", 22 }, { "style", STUDIO }, { "lowcut", 200 }, { "highcut", 7000 } } },
        { "Trap Vocal 1/4 Duck",  { { "mode", PINGPONG }, { "noteL", N4 }, { "feedback", 35 }, { "mix", 24 }, { "style", STUDIO }, { "lowcut", 300 }, { "highcut", 6000 }, { "ducking", 60 } } },
        { "Rap Throw 1/8D",       { { "mode", PINGPONG }, { "noteL", N8D }, { "feedback", 45 }, { "mix", 30 }, { "style", SPACE }, { "lowcut", 250 }, { "ducking", 50 } } },
        { "Dub Space",            { { "mode", SINGLE }, { "noteL", N8D }, { "feedback", 78 }, { "mix", 35 }, { "style", DUB }, { "drive", 30 } } },
        { "Space Echo Throw",     { { "mode", SINGLE }, { "noteL", N4D }, { "feedback", 55 }, { "mix", 30 }, { "style", SPACE } } },
        { "Memory Man Chorus",    { { "mode", PINGPONG }, { "noteL", N8 }, { "feedback", 40 }, { "mix", 28 }, { "style", MEMMAN } } },
        { "Lo-Fi 12 Bit",         { { "mode", SINGLE }, { "noteL", N8 }, { "feedback", 45 }, { "mix", 30 }, { "style", LOFI } } },
        { "Telephone Delay",      { { "mode", SINGLE }, { "noteL", N4 }, { "feedback", 40 }, { "mix", 30 }, { "style", PHONE } } },
        { "Ambient Wash",         { { "mode", PINGPONG }, { "noteL", N2 }, { "feedback", 70 }, { "mix", 35 }, { "style", AMBIENT }, { "diffusion", 60 }, { "wobble", 20 } } },
        { "Rhythm Gallop",        { { "mode", RHYTHM }, { "noteL", N16 }, { "pattern", 1 }, { "feedback", 30 }, { "mix", 30 }, { "style", ANALOG } } },
        { "Stutter Rhythm",       { { "mode", RHYTHM }, { "noteL", N16 }, { "pattern", 6 }, { "feedback", 10 }, { "mix", 35 }, { "style", BRIGHT } } },
        { "Wide Doubler",         { { "mode", DUAL }, { "sync", 0 }, { "msL", 18 }, { "msR", 27 }, { "feedback", 0 }, { "mix", 30 }, { "style", CLEAN } } },
        { "Pre-Chorus Ping 1/8",  { { "mode", PINGPONG }, { "noteL", N8 }, { "feedback", 38 }, { "mix", 25 }, { "style", MASTER }, { "lowcut", 250 }, { "highcut", 8000 } } },
        { "Cheap Tape Wobble",    { { "mode", SINGLE }, { "noteL", N4T }, { "feedback", 50 }, { "mix", 30 }, { "style", CHEAP }, { "wobble", 40 } } },
        { "Freeze Pad",           { { "mode", PINGPONG }, { "noteL", N4 }, { "feedback", 80 }, { "mix", 40 }, { "style", AMBIENT }, { "diffusion", 70 } } },
        // from a mix analysis of "Waima - O WLOS" (wide, washed vocal; repeats sit in the sides around a dotted quarter); use on a send
        { "Waima Main Throw (Send)",  { { "mode", DUAL }, { "noteL", N4D }, { "noteR", N2 }, { "feedback", 30 }, { "mix", 100 }, { "analog", 1 },
                                        { "lowcut", 400 }, { "highcut", 4500 }, { "modDepth", 15 }, { "modRate", 0.6f }, { "ducking", 50 },
                                        { "width", 140 }, { "diffusion", 20 } } },
        { "Waima Adlib Ping (Send)",  { { "mode", PINGPONG }, { "noteL", N8D }, { "feedback", 40 }, { "mix", 100 }, { "analog", 2 },
                                        { "lowcut", 500 }, { "highcut", 5000 }, { "modDepth", 20 }, { "ducking", 30 }, { "width", 150 },
                                        { "diffusion", 30 } } },
        // matched to a Waves H-Delay recording: Ping-Pong 1/4, low feedback, HiPass ~230 Hz, LoPass ~1.3 kHz, Analog off
        { "H-Delay Ping-Pong (Send)", { { "mode", PINGPONG }, { "noteL", N4 }, { "feedback", 10 }, { "mix", 100 }, { "analog", 0 },
                                        { "style", CLEAN }, { "lowcut", 230 }, { "highcut", 1300 }, { "drive", 0 }, { "width", 100 } } },
    };
}
} // namespace

const StringArray& SIEchoProcessor::noteNames()
{
    static StringArray s = [] { StringArray a; for (auto& n : kNotes) a.add (n.n); return a; }();
    return s;
}
double SIEchoProcessor::noteBeats (int i) { return kNotes[jlimit (0, (int) std::size (kNotes) - 1, i)].beats; }

AudioProcessorValueTreeState::ParameterLayout SIEchoProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, String (*fmt) (float, int))
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (fmt))); };
    auto c = [&] (const char* id, const char* name, StringArray ch, int def) { v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, ch, def)); };
    StringArray styles, pats;
    for (int s = 0; s < kNumStyles; ++s) styles.add (styleName (s));
    for (int p = 0; p < kNumPatterns; ++p) pats.add (patternName (p));
    c ("mode", "Mode", { "Single", "Dual", "Ping-Pong", "Rhythm" }, 0);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sync", true));
    c ("noteL", "Time", noteNames(), N4);
    c ("noteR", "Time R", noteNames(), N8D);
    f ("msL", "Time ms", logRange (1.0f, 4000.0f), 350.0f, fmtMs);
    f ("msR", "Time R ms", logRange (1.0f, 4000.0f), 520.0f, fmtMs);
    f ("feedback", "Feedback", { 0.0f, 110.0f }, 35.0f, fmtPct);
    f ("mix", "Mix", { 0.0f, 100.0f }, 30.0f, fmtPct);
    f ("drive", "Saturation", { 0.0f, 100.0f }, 0.0f, fmtPct);
    f ("lowcut", "Low Cut", logRange (20.0f, 2000.0f), 80.0f, fmtHz);
    f ("highcut", "High Cut", logRange (1000.0f, 20000.0f), 9000.0f, fmtHz);
    c ("style", "Style", styles, 0);   // Analog Off = clean digital, like H-Delay
    f ("wobble", "Wobble", { 0.0f, 100.0f }, 0.0f, fmtPct);
    f ("diffusion", "Diffusion", { 0.0f, 100.0f }, 0.0f, fmtPct);
    f ("width", "Width", { 0.0f, 150.0f }, 100.0f, fmtPct);
    f ("ducking", "Ducking", { 0.0f, 100.0f }, 0.0f, fmtPct);
    f ("groove", "Groove", { -100.0f, 100.0f }, 0.0f, fmtBi);
    c ("pattern", "Rhythm", pats, 0);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));
    // H-Delay style section
    c ("analog", "Analog", { "Off", "1", "2", "3", "4" }, 0);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "lofi", 1 }, "LoFi", false));
    f ("modDepth", "Mod Depth", { 0.0f, 100.0f }, 0.0f, fmtPct);
    f ("modRate", "Mod Rate", logRange (0.1f, 10.0f), 1.0f, fmtRate);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "phaseL", 1 }, "Phase L", false));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "phaseR", 1 }, "Phase R", false));
    f ("output", "Output", { -24.0f, 12.0f }, 0.0f, fmtDb);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "extsc", 1 }, "External Sidechain", false));
    return { v.begin(), v.end() };
}

SIEchoProcessor::SIEchoProcessor()
    : AudioProcessor (SI_SIDECHAIN_BUSES),
      apvts (*this, nullptr, "SIEcho", createLayout()),
      presets (apvts, "SI Echo", factoryPresets(), { "freeze", "extsc" })
{
}

bool SIEchoProcessor::isBusesLayoutSupported (const BusesLayout& l) const { return si::sc::layoutOk (l); }

void SIEchoProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);
}

EchoParams SIEchoProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    EchoParams p;
    p.mode = (Mode) (int) v ("mode");
    const double beat = 60.0 / jmax (20.0, (double) bpm.load());
    const bool sync = v ("sync") > 0.5f;
    p.timeL = jlimit (0.001, 8.0, sync ? noteBeats ((int) v ("noteL")) * beat : v ("msL") * 0.001);
    p.timeR = jlimit (0.001, 8.0, sync ? noteBeats ((int) v ("noteR")) * beat : v ("msR") * 0.001);
    if (p.mode == Mode::Rhythm) p.timeL = jmin (p.timeL, 1.0);   // 8 steps must fit the buffer
    p.feedback = v ("feedback") * 0.01f;
    p.mix = v ("mix") * 0.01f;
    p.drive = v ("drive") * 0.01f;
    p.lowCut = v ("lowcut");
    p.highCut = v ("highcut");
    p.style = (int) v ("style");
    p.wobble = v ("wobble") * 0.01f;
    p.diffusion = v ("diffusion") * 0.01f;
    p.width = v ("width") * 0.01f;
    p.ducking = v ("ducking") * 0.01f;
    p.groove = v ("groove") * 0.01f;
    p.patternIdx = (int) v ("pattern");
    p.freeze = v ("freeze") > 0.5f;
    p.analog = (int) v ("analog");
    p.lofi = v ("lofi") > 0.5f;
    p.modDepth = v ("modDepth") * 0.01f;
    p.modRate = v ("modRate");
    p.phaseL = v ("phaseL") > 0.5f;
    p.phaseR = v ("phaseR") > 0.5f;
    p.outputDb = v ("output");
    return p;
}

void SIEchoProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    bool host = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) { bpm = (float) *b; host = true; }
    hostBpm = host;
    engine.setParams (readParams());
    const auto key = si::sc::get (*this, buffer, apvts.getRawParameterValue ("extsc")->load() > 0.5f);
    engine.keyL = key.l;
    engine.keyR = key.r;
    float* l = buffer.getWritePointer (0);
    if (getMainBusNumInputChannels() > 1) engine.process (l, buffer.getWritePointer (1), buffer.getNumSamples());
    else
    {
        HeapBlock<float> tmp (buffer.getNumSamples());
        FloatVectorOperations::copy (tmp, l, buffer.getNumSamples());
        engine.process (l, tmp, buffer.getNumSamples());
    }
}

AudioProcessorEditor* SIEchoProcessor::createEditor() { return new SIEchoEditor (*this); }

void SIEchoProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SIEchoProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SIEchoProcessor(); }
