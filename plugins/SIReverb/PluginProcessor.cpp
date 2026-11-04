#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sidechain.h"
#include "Tempo.h"

using namespace juce;

namespace
{
String fmtPct (float v, int) { return String (roundToInt (v)) + "%"; }
String fmtMs (float v, int) { return si::tempo::formatMs (v); }
String fmtSec (float v, int) { return v >= 10.0f ? String (v, 1) + " s" : String (v, 2) + " s"; }
String fmtHz (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; }
String fmtRate (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; }
String fmtX (float v, int) { return String (v, 2) + "x"; }
String fmtDb (float v, int) { return String (v, 1) + " dB"; }
String fmtBpm (float v, int) { return String (v, 1); }

NormalisableRange<float> skewed (float lo, float hi, float centre)
{
    NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

std::unique_ptr<AudioParameterFloat> fparam (const String& id, const String& name, NormalisableRange<float> r, float def,
                                             String (*fmt) (float, int))
{
    return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (fmt));
}

std::vector<si::FactoryPreset> factoryPresets()
{
    return {
        { "Init", {} },
        { "My Concert Hall", { { "mode", 0 }, { "color", 2 }, { "mix", 100 }, { "predelay", 1.87f }, { "decay", 1.27f },
                               { "highfreq", 6000 }, { "highshelf", -24 }, { "bassxover", 700 }, { "bassmult", 1.5f },
                               { "size", 86.4f }, { "attack", 50 }, { "earlydiff", 100 }, { "latediff", 100 },
                               { "modrate", 2.53f }, { "moddepth", 38 }, { "highcut", 8290 }, { "lowcut", 210 } } },
        { "Vocal Plate",    { { "mode", 2 },  { "decay", 1.6f }, { "size", 45 }, { "predelay", 25 }, { "mix", 25 }, { "highcut", 8000 }, { "lowcut", 150 }, { "attack", 10 } } },
        { "Trap Vox Verb",   { { "mode", 11 }, { "decay", 2.0f }, { "size", 55 }, { "predelay", 35 }, { "mix", 22 }, { "highcut", 9000 }, { "lowcut", 220 }, { "duck", 45 } } },
        { "Big Hall",        { { "mode", 0 },  { "decay", 3.8f }, { "size", 80 }, { "predelay", 30 }, { "mix", 30 }, { "highcut", 7000 }, { "lowcut", 100 }, { "color", 1 } } },
        { "Drum Room",       { { "mode", 3 },  { "decay", 0.9f }, { "size", 35 }, { "predelay", 5 },  { "mix", 20 }, { "attack", 5 }, { "highcut", 10000 } } },
        { "Snare Nonlin",    { { "mode", 14 }, { "decay", 0.35f },{ "attack", 0 },  { "mix", 30 }, { "highcut", 9000 } } },
        { "Reverse Gate",    { { "mode", 14 }, { "decay", 0.5f }, { "attack", 100 },{ "mix", 35 } } },
        { "Ambient Wash",    { { "mode", 15 }, { "decay", 12 },   { "size", 95 }, { "mix", 45 }, { "moddepth", 70 }, { "highcut", 6000 }, { "attack", 60 } } },
        { "Cathedral Pad",   { { "mode", 18 }, { "decay", 9 },    { "size", 90 }, { "mix", 40 }, { "highcut", 6500 } } },
        { "80s Snare Hall",  { { "mode", 21 }, { "decay", 2.2f }, { "size", 60 }, { "color", 1 }, { "mix", 28 } } },
        { "Dark Chamber 79", { { "mode", 20 }, { "decay", 1.4f }, { "size", 50 }, { "color", 0 }, { "mix", 25 } } },
        { "Lo-Fi 70s Hall",  { { "mode", 0 },  { "decay", 2.5f }, { "size", 70 }, { "color", 0 }, { "highcut", 5000 }, { "mix", 30 } } },
        { "Chorus Dream",    { { "mode", 6 },  { "decay", 5 },    { "size", 75 }, { "moddepth", 80 }, { "modrate", 0.4f }, { "mix", 35 } } },
        { "Tight Ambience",  { { "mode", 7 },  { "decay", 0.5f }, { "size", 30 }, { "mix", 25 } } },
        { "Endless Space",   { { "mode", 13 }, { "decay", 40 },   { "size", 100 },{ "mix", 50 }, { "highcut", 5500 } } },
        { "Dirty Plate Vox", { { "mode", 10 }, { "decay", 1.8f }, { "size", 50 }, { "mix", 22 } } },
        { "Palace Strings",  { { "mode", 19 }, { "decay", 4.5f }, { "size", 80 }, { "mix", 32 } } },
        { "Sanctuary Swell", { { "mode", 8 },  { "decay", 7 },    { "size", 85 }, { "attack", 70 }, { "mix", 38 } } },
        { "Chaos Pad",       { { "mode", 16 }, { "decay", 6 },    { "size", 70 }, { "moddepth", 90 }, { "mix", 40 } } },
        { "Send 100% Wet",   { { "mode", 0 },  { "decay", 2.8f }, { "mix", 100 } } },
        // the user's VintageVerb setting (Smooth Random, NOW) used as the reference when matching the tail
        { "Moje VV Smooth Random (Send)", { { "mode", 13 }, { "color", 2 }, { "mix", 100 }, { "predelay", 28.23f }, { "decay", 2.72f },
                                            { "highfreq", 6000 }, { "highshelf", -24 }, { "bassxover", 700 }, { "bassmult", 1.5f },
                                            { "size", 86.4f }, { "attack", 50 }, { "earlydiff", 100 }, { "latediff", 100 },
                                            { "modrate", 2.53f }, { "moddepth", 38 }, { "highcut", 8290 }, { "lowcut", 210 } } },
        // from a mix analysis of "Waima - O WLOS": wide, dense, washed vocal space that never fully dies between lines (sends)
        { "Waima Main Wash (Send)",   { { "mode", 13 }, { "decay", 3.2f }, { "size", 75 }, { "predelay", 57 }, { "mix", 100 }, { "attack", 40 },
                                        { "earlydiff", 90 }, { "latediff", 90 }, { "moddepth", 50 }, { "modrate", 0.8f }, { "highcut", 7000 },
                                        { "lowcut", 300 }, { "highshelf", -6 }, { "width", 130 }, { "duck", 35 } } },
        { "Waima Adlib Space (Send)", { { "mode", 8 }, { "decay", 5.5f }, { "size", 90 }, { "predelay", 113 }, { "mix", 100 }, { "attack", 60 },
                                        { "moddepth", 60 }, { "highcut", 6500 }, { "lowcut", 400 }, { "width", 150 } } },
        { "Waima Intro Cloud (Send)", { { "mode", 15 }, { "decay", 9 }, { "size", 95 }, { "predelay", 0 }, { "mix", 100 }, { "attack", 70 },
                                        { "moddepth", 70 }, { "highcut", 6000 }, { "lowcut", 350 }, { "width", 150 } } },
    };
}
} // namespace

struct SIReverbProcessor::P
{
    std::atomic<float>* mix, *pre, *decay, *size, *attack, *ed, *ld, *mrate, *mdepth, *hc, *lc, *bmult, *bx, *hs, *hf,
        *mode, *color, *width, *duck, *freeze, *tSrc, *tBpm, *tAuto, *tPre, *tDecay, *tMod;
};

AudioProcessorValueTreeState::ParameterLayout SIReverbProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    StringArray modes;
    for (int i = 0; i < si::dsp::kNumModes; ++i) modes.add (si::dsp::getMode (i).name);

    v.push_back (fparam ("mix", "Mix", { 0.0f, 100.0f }, 30.0f, fmtPct));
    v.push_back (fparam ("predelay", "Pre-Delay", skewed (0.0f, 500.0f, 60.0f), 20.0f, fmtMs));
    v.push_back (fparam ("decay", "Decay", skewed (0.2f, 70.0f, 4.0f), 2.5f, fmtSec));
    v.push_back (fparam ("size", "Size", { 0.0f, 100.0f }, 60.0f, fmtPct));
    v.push_back (fparam ("attack", "Attack", { 0.0f, 100.0f }, 30.0f, fmtPct));
    v.push_back (fparam ("earlydiff", "Early Diff", { 0.0f, 100.0f }, 70.0f, fmtPct));
    v.push_back (fparam ("latediff", "Late Diff", { 0.0f, 100.0f }, 70.0f, fmtPct));
    v.push_back (fparam ("modrate", "Mod Rate", skewed (0.01f, 10.0f, 1.0f), 0.6f, fmtRate));
    v.push_back (fparam ("moddepth", "Mod Depth", { 0.0f, 100.0f }, 40.0f, fmtPct));
    v.push_back (fparam ("highcut", "Hi Cut", skewed (200.0f, 20000.0f, 4000.0f), 9000.0f, fmtHz));
    v.push_back (fparam ("lowcut", "Lo Cut", skewed (10.0f, 2000.0f, 200.0f), 80.0f, fmtHz));
    v.push_back (fparam ("bassmult", "Bass Mult", skewed (0.25f, 4.0f, 1.0f), 1.2f, fmtX));
    v.push_back (fparam ("bassxover", "Bass X", skewed (100.0f, 2000.0f, 500.0f), 400.0f, fmtHz));
    v.push_back (fparam ("highshelf", "Hi Shelf", { -24.0f, 0.0f }, -6.0f, fmtDb));
    v.push_back (fparam ("highfreq", "Hi Freq", skewed (1000.0f, 20000.0f, 5000.0f), 5000.0f, fmtHz));
    v.push_back (fparam ("width", "Width", { 0.0f, 150.0f }, 100.0f, fmtPct));
    v.push_back (fparam ("duck", "Duck", { 0.0f, 100.0f }, 0.0f, fmtPct));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "extsc", 1 }, "External Sidechain", false));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", modes, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "color", 1 }, "Color", StringArray { "1970s", "1980s", "Now" }, 2));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));

    // tempo assist ("tempo*" params are kept when loading presets)
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tempoSource", 1 }, "Tempo Source", StringArray { "Host", "Manual" }, 0));
    v.push_back (fparam ("tempoBpm", "Manual BPM", { 40.0f, 240.0f, 0.1f }, 120.0f, fmtBpm));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "tempoAuto", 1 }, "Auto Tempo", false));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tempoPre", 1 }, "Pre-Delay Sync", si::tempo::names (si::tempo::preDelayNotes()), 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tempoDecay", 1 }, "Decay Sync", si::tempo::names (si::tempo::decayNotes()), 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tempoMod", 1 }, "Mod Sync", StringArray { "Off", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars" }, 0));
    return { v.begin(), v.end() };
}

SIReverbProcessor::SIReverbProcessor()
    : AudioProcessor (SI_SIDECHAIN_BUSES),
      apvts (*this, nullptr, "SIReverb", createLayout()),
      presets (apvts, "SI Reverb", factoryPresets(), { "tempo", "extsc" }),
      p (std::make_unique<P>())
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    *p = { g ("mix"), g ("predelay"), g ("decay"), g ("size"), g ("attack"), g ("earlydiff"), g ("latediff"), g ("modrate"),
           g ("moddepth"), g ("highcut"), g ("lowcut"), g ("bassmult"), g ("bassxover"), g ("highshelf"), g ("highfreq"),
           g ("mode"), g ("color"), g ("width"), g ("duck"), g ("freeze"), g ("tempoSource"), g ("tempoBpm"), g ("tempoAuto"),
           g ("tempoPre"), g ("tempoDecay"), g ("tempoMod") };
}

bool SIReverbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    const auto in = l.getMainInputChannelSet();
    if (in != AudioChannelSet::stereo() && in != AudioChannelSet::mono()) return false;
    if (l.inputBuses.size() > 1) { const auto sc = l.inputBuses[1]; return sc.isDisabled() || sc == AudioChannelSet::mono() || sc == AudioChannelSet::stereo(); }
    return true;
}

void SIReverbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
    wet.setSize (2, samplesPerBlock, false, false, true);
    duckEnv = 0.0f;
    mixSm = p->mix->load() * 0.01f;
}

void SIReverbProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getMainBusNumInputChannels();   // the sidechain bus is not audio to process
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
    if (wet.getNumSamples() < n) wet.setSize (2, n, false, false, true);

    // ---- tempo
    double tempo = p->tBpm->load();
    bool hostOk = false, isPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            isPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) ppq = *q;
            if (p->tSrc->load() < 0.5f)
                if (auto b = pos->getBpm())
                {
                    tempo = *b;
                    hostOk = true;
                }
        }
    playing = isPlaying;
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numIn; ++ch) pk = jmax (pk, buffer.getMagnitude (ch, 0, n));
        const float prev = inEnv.load();
        inEnv = pk > prev ? pk : prev * std::pow (0.5f, (float) n / (0.08f * (float) sr));
    }
    tempo = jlimit (20.0, 400.0, tempo);
    bpm = (float) tempo;
    hostBpmValid = hostOk;

    const int modeIdx = (int) p->mode->load();
    const auto& md = si::dsp::getMode (modeIdx);
    const float size01 = p->size->load() * 0.01f;
    const auto& preN = si::tempo::preDelayNotes();
    const auto& decN = si::tempo::decayNotes();

    int preIdx = (int) p->tPre->load();
    int decIdx = (int) p->tDecay->load();
    if (p->tAuto->load() > 0.5f)
    {
        // AUTO: musical pre-delay + decay for this mode, size and tempo
        preIdx = md.autoPreMs <= 0.0f ? 0 : si::tempo::closest (preN, md.autoPreMs * (0.6 + 0.8 * size01), tempo);
        decIdx = si::tempo::closest (decN, si::tempo::beatsToMs (md.autoDecayBeats * (0.5 + size01), tempo), tempo);
    }
    const float preMs = preIdx > 0 ? (float) si::tempo::beatsToMs (preN[preIdx].beats, tempo) : p->pre->load();
    const float decayS = decIdx > 0 ? (float) si::tempo::beatsToMs (decN[decIdx].beats, tempo) * 0.001f : p->decay->load();
    effPreIdx = preIdx;
    effDecayIdx = decIdx;
    effPreMs = preMs;
    effDecayS = decayS;

    float modRate = p->mrate->load();
    static const double modBeats[] = { 0, 1, 2, 4, 8, 16 };
    if (const int mi = (int) p->tMod->load(); mi > 0)
        modRate = (float) (1000.0 / si::tempo::beatsToMs (modBeats[mi], tempo));

    si::dsp::ReverbParams rp;
    rp.mode = modeIdx;
    rp.color = (int) p->color->load();
    rp.decay = decayS;
    rp.size = size01;
    rp.attack = p->attack->load() * 0.01f;
    rp.earlyDiff = p->ed->load() * 0.01f;
    rp.lateDiff = p->ld->load() * 0.01f;
    rp.modRate = modRate;
    rp.modDepth = p->mdepth->load() * 0.01f;
    rp.highCut = p->hc->load();
    rp.lowCut = p->lc->load();
    rp.bassMult = p->bmult->load();
    rp.bassXover = p->bx->load();
    rp.highShelf = p->hs->load();
    rp.highFreq = p->hf->load();
    rp.preDelayMs = preMs;
    rp.width = p->width->load() * 0.01f;
    rp.freeze = p->freeze->load() > 0.5f;
    engine.setParams (rp);

    // ---- wet path
    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (numIn > 1 ? 1 : 0);
    float* wl = wet.getWritePointer (0);
    float* wr = wet.getWritePointer (1);
    for (int i = 0; i < n; ++i) { wl[i] = inL[i]; wr[i] = inR[i]; }
    engine.process (wl, wr, n);
    wetEnv = engine.getEnvelope();

    // ---- mix + duck
    const float mixT = p->mix->load() * 0.01f;
    const float duck = p->duck->load() * 0.01f;
    const auto key = si::sc::get (*this, buffer, apvts.getRawParameterValue ("extsc")->load() > 0.5f);   // Duck can listen to the sidechain
    const float atk = std::exp (-1.0f / (0.005f * (float) sr));
    const float rel = std::exp (-1.0f / (0.25f * (float) sr));
    const float mixA = std::exp (-1.0f / (0.02f * (float) sr));
    float* outL = buffer.getWritePointer (0);
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float dl = inL[i], dr = inR[i];
        const float lvl = key ? std::max (std::abs (key.l[i]), std::abs (key.r[i])) : std::max (std::abs (dl), std::abs (dr));
        duckEnv = lvl > duckEnv ? lvl + atk * (duckEnv - lvl) : lvl + rel * (duckEnv - lvl);
        mixSm = mixT + mixA * (mixSm - mixT);
        const float duckG = 1.0f - duck * std::min (1.0f, duckEnv * 4.0f);
        const float dryG = std::cos (mixSm * MathConstants<float>::halfPi);
        const float wetG = std::sin (mixSm * MathConstants<float>::halfPi) * duckG;
        const float l = dl * dryG + wl[i] * wetG;
        const float r = dr * dryG + wr[i] * wetG;
        if (outR) { outL[i] = l; outR[i] = r; }
        else outL[i] = 0.5f * (l + r);
    }
}

AudioProcessorEditor* SIReverbProcessor::createEditor() { return new SIReverbEditor (*this); }

void SIReverbProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SIReverbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SIReverbProcessor(); }
