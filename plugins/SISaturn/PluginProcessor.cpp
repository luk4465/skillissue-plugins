#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sidechain.h"

using namespace juce;
using namespace si::sat;

namespace
{
String fmtDb (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }
String fmtPct (float v, int) { return String (roundToInt (v)) + "%"; }
String fmtBi (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + "%"; }
String fmtPan (float v, int) { return std::abs (v) < 0.5f ? String ("C") : (v < 0 ? "L" : "R") + String (roundToInt (std::abs (v))); }
String fmtHz (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; }
String fmtMs (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; }
String fmtRate (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; }

NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi, [] (float a, float b, float t) { return a * std::pow (b / a, t); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); }, nullptr };
}

const char* kBandTargets[10] = { "drive", "mix", "fb", "fbfreq", "dyn", "bass", "mid", "treble", "pres", "level" };
const char* kBandTargetNames[10] = { "Drive", "Mix", "Feedback", "Fb Freq", "Dynamics", "Bass", "Mid", "Treble", "Presence", "Level" };
int target (int band, int p) { return 1 + band * 10 + p; }   // index into targetIds()

enum Style { CLEAN_TUBE, WARM_TUBE, SAT_TUBE, HEAVY_TUBE, BROKEN_TUBE, SUBTLE_TAPE, WARM_TAPE, SAT_TAPE, HEAVY_TAPE,
             GENTLE_SAT, WARM_SAT, HEAVY_SAT, CLEAN_AMP, WARM_AMP, CRUNCHY_AMP, BREAKUP_AMP, SMOOTH_LEAD, HEAVY_LEAD, US_AMP, UK_AMP,
             GENTLE_TRANS, WARM_TRANS, LUSH_TRANS, SMUDGE, BREAKDOWN, FOLDBACK, RECTIFY, DESTROY };

struct Bd { int style; float drive; float mix = 100, fb = 0, fbfreq = 1000, dyn = 0, bass = 0, mid = 0, treble = 0, pres = 0, level = 0; };
struct Md { int type; float rate = 1; int sync = 0; int shape = 0; float atk = 5, dec = 200, sus = 50, rel = 300, thr = -30; int input = 0; };
struct Sl { int src, tgt; float amt; };

si::FactoryPreset make (const String& name, std::initializer_list<float> xo, std::initializer_list<Bd> bands,
                        std::initializer_list<Md> mods = {}, std::initializer_list<Sl> slots = {})
{
    si::FactoryPreset p { name, { { "bands", (float) (bands.size() - 1) } } };
    int i = 0;
    for (float f : xo) p.values.push_back ({ "xo" + String (++i), f });
    i = 0;
    for (auto& b : bands)
    {
        auto v = [&] (const char* id, float x) { p.values.push_back ({ SISaturnProcessor::bid (i, id), x }); };
        v ("style", (float) b.style); v ("drive", b.drive / 0.36f); v ("mix", b.mix); v ("fb", b.fb); v ("fbfreq", b.fbfreq); v ("dyn", b.dyn);
        v ("bass", b.bass); v ("mid", b.mid); v ("treble", b.treble); v ("pres", b.pres); v ("level", b.level);
        ++i;
    }
    i = 0;
    for (auto& m : mods)
    {
        auto v = [&] (const char* id, float x) { p.values.push_back ({ SISaturnProcessor::mid (i, id), x }); };
        v ("type", (float) m.type); v ("rate", m.rate); v ("sync", (float) m.sync); v ("shape", (float) m.shape);
        v ("atk", m.atk); v ("dec", m.dec); v ("sus", m.sus); v ("rel", m.rel); v ("thr", m.thr); v ("input", (float) m.input);
        ++i;
    }
    i = 0;
    for (auto& s : slots)
    {
        p.values.push_back ({ SISaturnProcessor::sid (i, "src"), (float) s.src });
        p.values.push_back ({ SISaturnProcessor::sid (i, "tgt"), (float) s.tgt });
        p.values.push_back ({ SISaturnProcessor::sid (i, "amt"), s.amt });
        ++i;
    }
    return p;
}

enum { M_LFO = 1, M_EG = 2, M_EF = 3, M_TR = 4, M_SLIDER = 5 };

std::vector<si::FactoryPreset> factoryPresets()
{
    return {
        { "Init", {} },
        make ("Vocal Warmth",        {}, { { WARM_TUBE, 8, 70, 0, 1000, 0, 0, 0, 0, 1.5f } }),
        make ("Clean Vocal Tape",    {}, { { SUBTLE_TAPE, 6, 100, 0, 1000, 0, 0, 0, -1 } }),
        make ("Rap Vocal Grit",      { 2500 }, { { WARM_TAPE, 6 }, { SAT_TUBE, 12, 60, 0, 1000, 0, 0, 0, 0, 2 } }),
        make ("808 Distortion",      { 150 }, { { GENTLE_SAT, 4 }, { CRUNCHY_AMP, 14, 70, 0, 1000, 0, -3 } }),
        make ("808 Growl (Follower)",{ 120 }, { { GENTLE_SAT, 3 }, { HEAVY_SAT, 10, 80 } }, { { M_EF, 1, 0, 0, 5, 200, 50, 150 } },
                                     { { 1, target (1, 0), 40 } }),
        make ("Drum Bus Crunch",     { 120, 3000 }, { { WARM_TRANS, 6 }, { SAT_TAPE, 10 }, { WARM_SAT, 8, 100, 0, 1000, 0, 0, 0, 1 } }),
        make ("Transient Smash",     {}, { { HEAVY_TUBE, 12, 50 } }, { { M_TR, 1, 0, 0, 1, 200, 50, 60 } }, { { 1, target (0, 0), 50 } }),
        make ("Master Sparkle",      { 200, 6000 }, { { GENTLE_TRANS, 2 }, { SUBTLE_TAPE, 3 }, { CLEAN_TUBE, 6, 40, 0, 1000, 0, 0, 0, 1.5f } }),
        make ("Wobble Drive (LFO)",  {}, { { HEAVY_TAPE, 10 } }, { { M_LFO, 1, 3, 0 } }, { { 1, target (0, 0), 60 } }),
        make ("Lo-Fi Destroy",       {}, { { DESTROY, 12, 70 } }),
        make ("Telephone Amp",       {}, { { CRUNCHY_AMP, 18, 100, 0, 1000, 0, -12, 4, -8 } }),
        make ("Smudge Pad",          {}, { { SMUDGE, 10, 60 } }),
        make ("Breakdown Bass",      {}, { { BREAKDOWN, 12, 60 } }),
        make ("Feedback Ring",       {}, { { WARM_TUBE, 10, 100, 60, 900 } }),
        make ("Parallel Pump",       {}, { { HEAVY_SAT, 18, 40, 0, 1000, 60 } }),
        make ("Gated Fuzz",          {}, { { HEAVY_LEAD, 20, 100, 0, 1000, -60 } }),
        make ("British Guitar",      {}, { { UK_AMP, 16 } }),
        make ("Air Exciter",         { 5000 }, { { CLEAN_TUBE, 0, 0 }, { CLEAN_TUBE, 14, 45, 0, 1000, 0, 0, 0, 2 } }),
    };
}
} // namespace

const StringArray& SISaturnProcessor::targetIds()
{
    static StringArray ids = []
    {
        StringArray s { "" };
        for (int b = 0; b < kMaxBands; ++b) for (auto* t : kBandTargets) s.add (bid (b, t));
        for (int i = 1; i < kMaxBands; ++i) s.add ("xo" + String (i));
        s.addArray ({ "input", "output", "mix" });
        return s;
    }();
    return ids;
}

const StringArray& SISaturnProcessor::targetNames()
{
    static StringArray names = []
    {
        StringArray s { "None" };
        for (int b = 0; b < kMaxBands; ++b) for (auto* t : kBandTargetNames) s.add ("B" + String (b + 1) + " " + t);
        for (int i = 1; i < kMaxBands; ++i) s.add ("Crossover " + String (i));
        s.addArray ({ "Input", "Output", "Mix" });
        return s;
    }();
    return names;
}

AudioProcessorValueTreeState::ParameterLayout SISaturnProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto f = [&] (const String& id, const String& name, NormalisableRange<float> r, float def, String (*fmt) (float, int))
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (fmt))); };
    auto c = [&] (const String& id, const String& name, StringArray ch, int def) { v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, ch, def)); };
    auto b = [&] (const String& id, const String& name, bool def) { v.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    StringArray styles;
    for (int s = 0; s < kNumStyles; ++s) styles.add (styleName (s));
    for (int i = 0; i < kMaxBands; ++i)
    {
        const String n = "B" + String (i + 1) + " ";
        b (bid (i, "on"), n + "On", true);
        b (bid (i, "mute"), n + "Mute", false);
        b (bid (i, "solo"), n + "Solo", false);
        c (bid (i, "style"), n + "Style", styles, 6);
        f (bid (i, "drive"), n + "Drive", { 0.0f, 100.0f }, 17.0f, fmtPct);   // percent like Saturn 2; 100 % = +36 dB of drive
        f (bid (i, "dpan"), n + "Drive Pan", { -100.0f, 100.0f }, 0.0f, fmtPan);
        f (bid (i, "mix"), n + "Mix", { 0.0f, 100.0f }, 100.0f, fmtPct);
        f (bid (i, "fb"), n + "Feedback", { 0.0f, 100.0f }, 0.0f, fmtPct);
        f (bid (i, "fbfreq"), n + "Feedback Freq", logRange (20.0f, 20000.0f), 1000.0f, fmtHz);
        f (bid (i, "dyn"), n + "Dynamics", { -100.0f, 100.0f }, 0.0f, fmtBi);
        f (bid (i, "bass"), n + "Bass", { -12.0f, 12.0f }, 0.0f, fmtDb);
        f (bid (i, "mid"), n + "Mid", { -12.0f, 12.0f }, 0.0f, fmtDb);
        f (bid (i, "treble"), n + "Treble", { -12.0f, 12.0f }, 0.0f, fmtDb);
        f (bid (i, "pres"), n + "Presence", { -12.0f, 12.0f }, 0.0f, fmtDb);
        f (bid (i, "level"), n + "Level", { -36.0f, 24.0f }, 0.0f, fmtDb);
        f (bid (i, "lpan"), n + "Level Pan", { -100.0f, 100.0f }, 0.0f, fmtPan);
    }
    c ("bands", "Bands", { "1", "2", "3", "4", "5", "6" }, 0);
    const float xoDef[5] = { 120.0f, 600.0f, 2500.0f, 6000.0f, 12000.0f };
    for (int i = 0; i < kMaxBands - 1; ++i)
    {
        f ("xo" + String (i + 1), "Crossover " + String (i + 1), logRange (20.0f, 20000.0f), xoDef[i], fmtHz);
        c ("sl" + String (i + 1), "Slope " + String (i + 1), { "6 dB", "12 dB", "24 dB", "48 dB" }, 2);
    }
    f ("input", "Input", { -36.0f, 36.0f }, 0.0f, fmtDb);
    f ("output", "Output", { -36.0f, 36.0f }, 0.0f, fmtDb);
    f ("mix", "Mix", { 0.0f, 100.0f }, 100.0f, fmtPct);
    c ("hq", "High Quality", { "Off", "2x", "4x", "8x" }, 1);
    c ("msmode", "Channel Mode", { "Stereo", "Mid/Side" }, 0);
    b ("autogain", "Auto Level", true);

    for (int k = 0; k < kModSources; ++k)
    {
        const String n = "Mod " + String (k + 1) + " ";
        c (mid (k, "type"), n + "Type", { "None", "XLFO", "Envelope", "Follower", "Transients", "Slider" }, 0);
        f (mid (k, "rate"), n + "Rate", logRange (0.02f, 20.0f), 1.0f, fmtRate);
        c (mid (k, "sync"), n + "Sync", { "Free", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars" }, 0);
        c (mid (k, "shape"), n + "Shape", { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random", "Steps" }, 0);
        f (mid (k, "atk"), n + "Attack", logRange (0.1f, 1000.0f), 5.0f, fmtMs);
        f (mid (k, "dec"), n + "Decay", logRange (1.0f, 4000.0f), 200.0f, fmtMs);
        f (mid (k, "sus"), n + "Sustain", { 0.0f, 100.0f }, 50.0f, fmtPct);
        f (mid (k, "rel"), n + "Release", logRange (1.0f, 4000.0f), 300.0f, fmtMs);
        f (mid (k, "thr"), n + "Threshold", { -60.0f, 0.0f }, -30.0f, fmtDb);
        c (mid (k, "input"), n + "Input", { "Main", "Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6", "Sidechain" }, 0);
        f (mid (k, "value"), n + "Value", { 0.0f, 100.0f }, 50.0f, fmtPct);
    }
    for (int j = 0; j < kModSlots; ++j)
    {
        const String n = "Slot " + String (j + 1) + " ";
        c (sid (j, "src"), n + "Source", { "None", "Mod 1", "Mod 2", "Mod 3", "Mod 4" }, 0);
        c (sid (j, "tgt"), n + "Target", targetNames(), 0);
        f (sid (j, "amt"), n + "Amount", { -100.0f, 100.0f }, 50.0f, fmtBi);
    }
    return { v.begin(), v.end() };
}

SISaturnProcessor::SISaturnProcessor()
    : AudioProcessor (SI_SIDECHAIN_BUSES),
      apvts (*this, nullptr, "SISaturn", createLayout()),
      presets (apvts, "SI Saturn", factoryPresets(), { "hq" })
{
}

bool SISaturnProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (! (out == AudioChannelSet::stereo() && l.getMainInputChannelSet() == out)) return false;
    return si::sc::layoutOk (l);
}

void SISaturnProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    blockSize = samplesPerBlock;
    for (int i = 1; i < 4; ++i)
    {
        os[i] = std::make_unique<dsp::Oversampling<float>> (2, (size_t) i, dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[i]->initProcessing ((size_t) samplesPerBlock);
    }
    osIndex = -1;
}

Settings SISaturnProcessor::readSettings() const
{
    auto v = [this] (const String& id) { return apvts.getRawParameterValue (id)->load(); };
    Settings s;
    s.bands = (int) v ("bands") + 1;
    for (int i = 0; i < kMaxBands - 1; ++i)
    {
        s.xo[(size_t) i] = v ("xo" + String (i + 1));
        s.slope[(size_t) i] = (int) v ("sl" + String (i + 1));
    }
    s.autoGain = v ("autogain") > 0.5f;
    s.midSide = v ("msmode") > 0.5f;
    s.mix = v ("mix") * 0.01f;
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& d = s.band[(size_t) b];
        d.on = v (bid (b, "on")) > 0.5f;
        d.mute = v (bid (b, "mute")) > 0.5f;
        d.solo = v (bid (b, "solo")) > 0.5f;
        d.style = (int) v (bid (b, "style"));
        d.driveDb = v (bid (b, "drive")) * 0.36f;
        d.drivePan = v (bid (b, "dpan")) * 0.01f;
        d.mix = v (bid (b, "mix")) * 0.01f;
        d.feedback = v (bid (b, "fb")) * 0.01f;
        d.fbFreq = v (bid (b, "fbfreq"));
        d.dyn = v (bid (b, "dyn")) * 0.01f;
        d.bass = v (bid (b, "bass"));
        d.mid = v (bid (b, "mid"));
        d.treble = v (bid (b, "treble"));
        d.presence = v (bid (b, "pres"));
        d.levelDb = v (bid (b, "level"));
        d.levelPan = v (bid (b, "lpan")) * 0.01f;
    }
    return s;
}

ModSourceParams SISaturnProcessor::readSource (int k) const
{
    auto v = [&] (const char* id) { return apvts.getRawParameterValue (mid (k, id))->load(); };
    ModSourceParams p;
    p.type = (ModType) (int) v ("type");
    p.rateHz = v ("rate");
    p.sync = (int) v ("sync");
    p.shape = (int) v ("shape");
    p.attackMs = v ("atk");
    p.decayMs = v ("dec");
    p.sustain = v ("sus") * 0.01f;
    p.releaseMs = v ("rel");
    p.thresholdDb = v ("thr");
    p.input = (int) v ("input");
    p.value = v ("value") * 0.01f;
    return p;
}

void SISaturnProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (getMainBusNumInputChannels() < 2 || buffer.getNumChannels() < 2) return;
    const auto key = si::sc::get (*this, buffer, true);   // only used by sources whose Input is "Sidechain"

    const int want = (int) apvts.getRawParameterValue ("hq")->load();
    if (want != osIndex)
    {
        osIndex = want;
        engine.prepare (sr * (1 << osIndex));
        if (osIndex > 0) os[osIndex]->reset();
        setLatencySamples (osIndex > 0 ? roundToInt (os[osIndex]->getLatencyInSamples()) : 0);
    }
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    const auto& ids = targetIds();
    dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) n);   // main bus only (not the sidechain)
    float ipk = 0, opk = 0;
    const float envRel = std::exp (-32.0f / (0.1f * (float) sr));

    constexpr int kChunk = 32;
    for (int start = 0; start < n; start += kChunk)
    {
        const int m = jmin (kChunk, n - start);
        float pk = 0;
        for (int i = start; i < start + m; ++i) pk = jmax (pk, std::abs (l[i]), std::abs (r[i]));
        mainEnv = jmax (pk, mainEnv * envRel);
        float spk = 0;
        if (key) for (int i = start; i < start + m; ++i) spk = jmax (spk, std::abs (key.l[i]), std::abs (key.r[i]));
        scEnv = jmax (spk, scEnv * envRel);

        // modulation sources
        float val[kModSources];
        ModSourceParams sp[kModSources];
        for (int k = 0; k < kModSources; ++k)
        {
            sp[k] = readSource (k);
            const float lvl = sp[k].input == 0 ? mainEnv : sp[k].input == 7 ? scEnv : engine.bandEnv[(size_t) jlimit (0, kMaxBands - 1, sp[k].input - 1)].load();
            val[k] = sources[(size_t) k].advance (sp[k], m / sr, bpm, lvl);
            modValue[(size_t) k] = val[k];
        }

        // slots: offsets in normalised parameter space
        std::array<float, 80> offset {};
        bool any = false;
        for (int j = 0; j < kModSlots; ++j)
        {
            const int src = (int) apvts.getRawParameterValue (sid (j, "src"))->load() - 1;
            const int tgt = (int) apvts.getRawParameterValue (sid (j, "tgt"))->load();
            if (src < 0 || tgt <= 0 || tgt >= ids.size() || sp[src].type == ModType::None) continue;
            offset[(size_t) tgt] += apvts.getRawParameterValue (sid (j, "amt"))->load() * 0.01f * val[src];
            any = true;
        }
        Settings s = readSettings();
        float inDb = apvts.getRawParameterValue ("input")->load(), outDb = apvts.getRawParameterValue ("output")->load();
        if (any)
            for (int t = 1; t < ids.size(); ++t)
            {
                if (offset[(size_t) t] == 0.0f) continue;
                auto* p = apvts.getParameter (ids[t]);
                const float real = p->convertFrom0to1 (jlimit (0.0f, 1.0f, p->getValue() + offset[(size_t) t]));
                if (t <= kMaxBands * 10)
                {
                    auto& bs = s.band[(size_t) ((t - 1) / 10)];
                    switch ((t - 1) % 10)
                    {
                        case 0: bs.driveDb = real * 0.36f; break;
                        case 1: bs.mix = real * 0.01f; break;
                        case 2: bs.feedback = real * 0.01f; break;
                        case 3: bs.fbFreq = real; break;
                        case 4: bs.dyn = real * 0.01f; break;
                        case 5: bs.bass = real; break;
                        case 6: bs.mid = real; break;
                        case 7: bs.treble = real; break;
                        case 8: bs.presence = real; break;
                        default: bs.levelDb = real; break;
                    }
                }
                else if (t <= kMaxBands * 10 + kMaxBands - 1) s.xo[(size_t) (t - 1 - kMaxBands * 10)] = real;
                else if (ids[t] == "input") inDb = real;
                else if (ids[t] == "output") outDb = real;
                else s.mix = real * 0.01f;
            }
        engine.setSettings (s);

        const float inT = Decibels::decibelsToGain (inDb), outT = Decibels::decibelsToGain (outDb);
        for (int i = start; i < start + m; ++i)
        {
            inSm += 0.01f * (inT - inSm);
            l[i] *= inSm; r[i] *= inSm;
            ipk = jmax (ipk, std::abs (l[i]), std::abs (r[i]));
        }
        if (osIndex > 0 && n <= blockSize)
        {
            auto sub = block.getSubBlock ((size_t) start, (size_t) m);
            auto up = os[osIndex]->processSamplesUp (sub);
            engine.process (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples());
            os[osIndex]->processSamplesDown (sub);
        }
        else
            engine.process (l + start, r + start, m);
        for (int i = start; i < start + m; ++i)
        {
            outSm += 0.01f * (outT - outSm);
            l[i] *= outSm; r[i] *= outSm;
            opk = jmax (opk, std::abs (l[i]), std::abs (r[i]));
        }
    }
    inLevel = jmax (ipk, inLevel.load() * 0.9f);
    outLevel = jmax (opk, outLevel.load() * 0.9f);
    fifo.push (l, r, n);
}

AudioProcessorEditor* SISaturnProcessor::createEditor() { return new SISaturnEditor (*this); }

void SISaturnProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SISaturnProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SISaturnProcessor(); }
