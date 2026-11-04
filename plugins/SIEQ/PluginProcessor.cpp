#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sidechain.h"

using namespace juce;
using namespace si::eq;

namespace
{
String fmtHz (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " k" : String (roundToInt (v)) + " Hz"; }
String fmtDb (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }
String fmtQ (float v, int) { return String (v, v < 10.0f ? 2 : 1); }

NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi, [] (float a, float b, float t) { return a * std::pow (b / a, t); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); }, nullptr };
}

struct B { int type; float freq, gain, q; int slope = 1; int chan = 0; bool dyn = false; float thr = -24, tgt = 0; bool tight = true; };

si::FactoryPreset make (const String& name, std::initializer_list<B> bands)
{
    si::FactoryPreset p { name, {} };
    int i = 0;
    for (auto& b : bands)
    {
        auto id = [i] (const char* n) { return SIEQProcessor::pid (i, n); };
        p.values.push_back ({ id ("on"), 1.0f });
        p.values.push_back ({ id ("type"), (float) b.type });
        p.values.push_back ({ id ("freq"), b.freq });
        p.values.push_back ({ id ("gain"), b.gain });
        p.values.push_back ({ id ("q"), b.q });
        p.values.push_back ({ id ("slope"), (float) b.slope });
        p.values.push_back ({ id ("chan"), (float) b.chan });
        p.values.push_back ({ id ("dyn"), b.dyn ? 1.0f : 0.0f });
        p.values.push_back ({ id ("thr"), b.thr });
        p.values.push_back ({ id ("tgt"), b.tgt });
        p.values.push_back ({ id ("tight"), b.tight ? 1.0f : 0.0f });
        ++i;
    }
    return p;
}

enum { BELL, LS, HS, LC, HC, NOTCH, BP, TILT };

std::vector<si::FactoryPreset> factoryPresets()
{
    return {
        { "Init", {} },
        make ("Moje Clean",          { { LC, 120, 0, 0.9f, 1 }, { BELL, 254, 0, 1.0f, 1, 0, true, -30, -3.1f, false }, { BELL, 1380, 1.9f, 0.7f }, { HS, 5200, 1.8f, 0.71f } }),
        make ("Clean Vocal",         { { LC, 90, 0, 0.71f, 1 }, { BELL, 250, -2.5f, 1.2f }, { BELL, 3200, 2.0f, 0.9f }, { HS, 10000, 2.5f, 0.71f },
                                       { BELL, 6500, 0, 2.0f, 1, 0, true, -30, -5, true } }),
        make ("Trap Vocal Air",      { { LC, 120, 0, 0.71f, 1 }, { BELL, 300, -3, 1.0f }, { BELL, 2500, 2.5f, 1.0f }, { HS, 12000, 4, 0.71f },
                                       { BELL, 7000, 0, 2.0f, 1, 0, true, -30, -6, true } }),
        make ("Rap Vocal Presence",  { { LC, 100, 0, 0.71f, 1 }, { BELL, 200, -2, 1.0f }, { BELL, 1800, 3, 1.2f }, { BELL, 4500, 2, 1.4f }, { HS, 11000, 2, 0.71f } }),
        make ("R&B Smooth Vocal",    { { LC, 80, 0, 0.71f, 0 }, { LS, 150, 1, 0.71f }, { BELL, 3000, -1.5f, 1.5f }, { HS, 12000, 3, 0.6f },
                                       { BELL, 400, 0, 1.0f, 1, 0, true, -25, -3, false } }),
        make ("Podcast Voice",       { { LC, 80, 0, 0.71f, 1 }, { BELL, 200, 1.5f, 1.0f }, { BELL, 500, -2, 1.0f }, { BELL, 3000, 2, 1.0f }, { HC, 16000, 0, 0.71f, 1 } }),
        make ("De-Esser",            { { BELL, 6500, 0, 2.0f, 1, 0, true, -32, -8, true } }),
        make ("Dynamic Mud Control", { { BELL, 300, 0, 1.0f, 1, 0, true, -24, -5, false } }),
        make ("Kick Punch",          { { LC, 30, 0, 0.71f, 1 }, { BELL, 60, 3, 1.2f }, { BELL, 350, -4, 1.4f }, { BELL, 3500, 3, 1.0f } }),
        make ("808 Clean",           { { LC, 28, 0, 0.71f, 3 }, { BELL, 55, 2, 1.0f }, { BELL, 250, -3, 1.0f }, { HC, 6000, 0, 0.71f, 0 } }),
        make ("Snare Snap",          { { LC, 90, 0, 0.71f, 1 }, { BELL, 200, 2, 1.4f }, { BELL, 800, -3, 1.5f }, { BELL, 5000, 3, 1.0f }, { HS, 10000, 2, 0.71f } }),
        make ("Hi-Hat Air",          { { LC, 400, 0, 0.71f, 1 }, { BELL, 2500, -2, 1.2f }, { HS, 9000, 3, 0.71f } }),
        make ("Acoustic Guitar",     { { LC, 80, 0, 0.71f, 1 }, { BELL, 200, -3, 1.0f }, { BELL, 3000, 2, 1.0f }, { HS, 10000, 2, 0.71f } }),
        make ("Piano Clarity",       { { LC, 50, 0, 0.71f, 1 }, { BELL, 300, -2, 1.0f }, { BELL, 2000, 1.5f, 0.8f }, { HS, 8000, 2, 0.71f } }),
        make ("Master Smile",        { { LS, 80, 1.5f, 0.7f }, { BELL, 400, -1, 0.7f }, { HS, 10000, 1.5f, 0.7f } }),
        make ("Mono Low End",        { { LC, 25, 0, 0.71f, 1 }, { LC, 120, 0, 0.71f, 1, 2 } }),
        make ("De-Mud Mix Bus",      { { BELL, 300, -2, 0.9f }, { BELL, 4000, 1, 0.7f } }),
        make ("Wide Air",            { { HS, 8000, 3, 0.71f, 1, 2 } }),
        make ("Telephone",           { { LC, 400, 0, 0.71f, 3 }, { HC, 3200, 0, 0.71f, 3 }, { BELL, 1500, 4, 1.0f } }),
        make ("Dark & Warm",         { { TILT, 1000, -3, 0.5f }, { LS, 120, 1.5f, 0.71f } }),
    };
}

const float kDefFreq[kBands] = { 30, 100, 250, 1000, 3000, 6000, 10000, 18000 };
const int kDefType[kBands] = { LC, LS, BELL, BELL, BELL, BELL, HS, HC };
} // namespace

AudioProcessorValueTreeState::ParameterLayout SIEQProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    const StringArray types { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt" };
    for (int i = 0; i < kBands; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        auto f = [&] (const char* id, const String& name, NormalisableRange<float> r, float def, String (*fmt) (float, int))
        { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { pid (i, id), 1 }, n + name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (fmt))); };
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "on"), 1 }, n + "On", false));
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "type"), 1 }, n + "Type", types, kDefType[i]));
        f ("freq", "Freq", logRange (20.0f, 20000.0f), kDefFreq[i], fmtHz);
        f ("gain", "Gain", { -24.0f, 24.0f }, 0.0f, fmtDb);
        f ("q", "Q", logRange (0.1f, 18.0f), 0.71f, fmtQ);
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "slope"), 1 }, n + "Slope", StringArray { "12 dB", "24 dB", "36 dB", "48 dB" }, 1));
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "chan"), 1 }, n + "Channel", StringArray { "Stereo", "Mid", "Side", "Left", "Right" }, 0));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "dyn"), 1 }, n + "Dynamic", false));
        f ("thr", "Threshold", { -60.0f, 0.0f }, -24.0f, fmtDb);
        f ("tgt", "Target", { -24.0f, 24.0f }, 0.0f, fmtDb);
        v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "tight"), 1 }, n + "Timing", StringArray { "Smooth", "Tight" }, 1));
    }
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 24.0f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (fmtDb)));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "autogain", 1 }, "Auto Gain", false));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "extsc", 1 }, "External Sidechain", false));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "listen", 1 }, "Listen", StringArray { "Stereo", "Left", "Right", "Mid", "Side" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "analyzer", 1 }, "Analyzer", StringArray { "Post", "Pre", "Off" }, 0));
    return { v.begin(), v.end() };
}

SIEQProcessor::SIEQProcessor()
    : AudioProcessor (SI_SIDECHAIN_BUSES),
      apvts (*this, nullptr, "SIEQ", createLayout()),
      presets (apvts, "SI EQ", factoryPresets(), { "tempo", "extsc" })
{
}

bool SIEQProcessor::isBusesLayoutSupported (const BusesLayout& l) const { return si::sc::layoutOk (l); }

void SIEQProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
}

BandParams SIEQProcessor::readBand (int i) const
{
    auto v = [&] (const char* n) { return apvts.getRawParameterValue (pid (i, n))->load(); };
    BandParams p;
    p.on = v ("on") > 0.5f;
    p.type = (Type) (int) v ("type");
    p.freq = v ("freq");
    p.gain = v ("gain");
    p.q = v ("q");
    p.slope = (int) v ("slope");
    p.chan = (Chan) (int) v ("chan");
    p.dyn = v ("dyn") > 0.5f;
    p.thresh = v ("thr");
    p.target = v ("tgt");
    p.tight = v ("tight") > 0.5f;
    return p;
}

void SIEQProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const bool stereo = getMainBusNumInputChannels() > 1;
    float* l = buffer.getWritePointer (0);
    float* r = stereo ? buffer.getWritePointer (1) : l;

    preFifo.push (l, stereo ? r : nullptr, n);

    std::array<BandParams, kBands> bp;
    for (int i = 0; i < kBands; ++i) { bp[(size_t) i] = readBand (i); engine.setBand (i, bp[(size_t) i]); }
    engine.setSolo (soloBand.load());
    const auto key = si::sc::get (*this, buffer, apvts.getRawParameterValue ("extsc")->load() > 0.5f);   // dynamic bands can listen to the sidechain
    engine.keyL = key.l;
    engine.keyR = key.r;

    if (stereo) engine.process (l, r, n);
    else
    {
        HeapBlock<float> tmp (n);
        FloatVectorOperations::copy (tmp, l, n);
        engine.process (l, tmp, n);
    }

    // auto gain: undo the average static boost/cut across the vocal range
    float comp = 0.0f;
    if (apvts.getRawParameterValue ("autogain")->load() > 0.5f)
    {
        float acc = 0.0f;
        constexpr int kPts = 24;
        for (int k = 0; k < kPts; ++k)
        {
            const float f = 80.0f * std::pow (10000.0f / 80.0f, (float) k / (kPts - 1));
            float db = 0.0f;
            for (auto& b : bp) db += bandResponseDb (b, b.gain, f, (float) sr);
            acc += db;
        }
        comp = -0.8f * acc / (float) kPts;
    }
    autoGainDb = comp;
    const float target = Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load() + comp);
    const float a = std::exp (-1.0f / (0.03f * (float) sr));

    const int listen = (int) apvts.getRawParameterValue ("listen")->load();
    float pkL = 0, pkR = 0;
    for (int i = 0; i < n; ++i)
    {
        outSm = target + a * (outSm - target);
        float L = l[i] * outSm, R = r[i] * outSm;
        switch (listen)
        {
            case 1: R = L; break;
            case 2: L = R; break;
            case 3: L = R = 0.5f * (L + R); break;
            case 4: { const float s = 0.5f * (L - R); L = s; R = s; break; }
            default: break;
        }
        l[i] = L;
        if (stereo) r[i] = R;
        pkL = jmax (pkL, std::abs (L));
        pkR = jmax (pkR, std::abs (R));
    }
    meterL = jmax (pkL, meterL.load() * 0.9f);
    meterR = jmax (pkR, meterR.load() * 0.9f);
    postFifo.push (l, stereo ? r : nullptr, n);
}

void SIEQProcessor::toggleAB()
{
    ab[abIndex] = apvts.copyState();
    abIndex ^= 1;
    if (ab[abIndex].isValid()) apvts.replaceState (ab[abIndex].createCopy());
}

AudioProcessorEditor* SIEQProcessor::createEditor() { return new SIEQEditor (*this); }

void SIEQProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SIEQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SIEQProcessor(); }
