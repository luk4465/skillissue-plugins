#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/ReverbEngine.h"

using namespace juce;

namespace
{
const double kLengthBeats[] = { 1, 2, 4, 8, 16, 32 };              // in beats of a 4/4 bar; scaled by time signature
const bool kLengthIsBar[] = { false, false, true, true, true, true };
const double kFxBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
constexpr double kMaxCaptureSec = 30.0;

String fmtPct (float v, int) { return String (roundToInt (v)) + "%"; }
String fmtHz (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; }
String fmtX (float v, int) { return String (v, 2) + "x"; }
String fmtDb (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; }
String fmtSt (float v, int) { return v == 0.0f ? String ("0 st") : String (roundToInt (v)) + " st"; }
String fmtBi (float v, int) { return v == 0.0f ? String ("Linear") : (v < 0 ? "Slow " : "Fast ") + String (std::abs (roundToInt (v))); }
String fmtBpm (float v, int) { return String (v, 1); }

std::unique_ptr<AudioParameterFloat> fparam (const String& id, const String& name, NormalisableRange<float> r, float def, String (*fmt) (float, int))
{
    return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (fmt));
}
std::unique_ptr<AudioParameterBool> bparam (const String& id, const String& name, bool def)
{
    return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
}
std::unique_ptr<AudioParameterChoice> cparam (const String& id, const String& name, StringArray c, int def)
{
    return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def);
}

std::vector<si::FactoryPreset> factoryPresets()
{
    return {
        { "Init", {} },
        { "Trap Vox Suck",    { { "verbMode", 11 }, { "length", 2 }, { "shape", -40 }, { "tail", 2.0f }, { "post", 25 }, { "widthEnd", 140 } } },
        { "Adlib Ghost",      { { "verbMode", 6 }, { "length", 1 }, { "chorus", 1 }, { "pitch", -5 }, { "hicut", 7000 }, { "post", 15 } } },
        { "Club Build 8 Bars",{ { "verbMode", 18 }, { "length", 5 }, { "filter", 75 }, { "stutter", 1 }, { "accel", 1 }, { "fxRate", 3 }, { "shape", -50 } } },
        { "Classic Swell",    { { "verbMode", 0 }, { "size", 75 }, { "tail", 1.5f } } },
        { "Vintage Swell",    { { "verbMode", 20 }, { "verbColor", 0 }, { "hicut", 7000 } } },
        { "Wide Chorus Rise", { { "chorus", 1 }, { "widthStart", 0 }, { "widthEnd", 150 }, { "verbMode", 6 } } },
        { "Dream Wash",       { { "verbMode", 15 }, { "size", 95 }, { "tail", 2.5f }, { "shape", -40 } } },
        { "Phone Call",       { { "phone", 1 }, { "verbMode", 2 }, { "widthEnd", 60 } } },
        { "Tape Lift",        { { "pitch", -12 }, { "shape", 20 }, { "verbMode", 11 } } },
        { "Stutter Build",    { { "stutter", 1 }, { "accel", 1 }, { "fxRate", 3 } } },
        { "Tremolo Rush",     { { "tremolo", 1 }, { "accel", 1 }, { "fxRate", 1 } } },
        { "Cathedral Breath", { { "verbMode", 18 }, { "length", 3 }, { "tail", 2.5f }, { "size", 90 } } },
        { "Plate Suck",       { { "verbMode", 2 }, { "length", 1 }, { "tail", 1.2f }, { "shape", -30 } } },
        { "Lo-Fi Ghost",      { { "lofi", 1 }, { "verbColor", 0 }, { "hicut", 6000 }, { "verbMode", 9 } } },
        { "Pitch Climb",      { { "pitch", -24 }, { "filter", 60 }, { "filterType", 0 } } },
        { "Echo Tunnel",      { { "echo", 1 }, { "fxRate", 1 }, { "verbMode", 13 } } },
        { "HP Riser",         { { "filter", 70 }, { "filterType", 1 }, { "widthEnd", 140 } } },
        { "Band Sweep",       { { "filter", 55 }, { "filterType", 2 }, { "verbMode", 5 } } },
    };
}
} // namespace

AudioProcessorValueTreeState::ParameterLayout SIRiserProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    StringArray modes;
    for (int i = 0; i < si::dsp::kNumModes; ++i) modes.add (si::dsp::getMode (i).name);

    v.push_back (cparam ("length", "Length", { "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars" }, 2));
    v.push_back (cparam ("feed", "Feed", { "Auto", "1/16", "1/8", "1/4", "1/2", "1 Bar", "All" }, 0));
    v.push_back (cparam ("tempoSource", "Tempo Source", { "Host", "Manual" }, 0));
    v.push_back (fparam ("tempoBpm", "Manual BPM", { 40.0f, 240.0f, 0.1f }, 120.0f, fmtBpm));
    v.push_back (cparam ("verbMode", "Verb Mode", modes, 11));
    v.push_back (cparam ("verbColor", "Verb Color", { "1970s", "1980s", "Now" }, 2));
    v.push_back (fparam ("size", "Size", { 0.0f, 100.0f }, 80.0f, fmtPct));
    v.push_back (fparam ("tail", "Tail", { 0.5f, 4.0f }, 1.8f, fmtX));
    NormalisableRange<float> hc (1000.0f, 20000.0f); hc.setSkewForCentre (6000.0f);
    NormalisableRange<float> lc (20.0f, 1000.0f); lc.setSkewForCentre (150.0f);
    v.push_back (fparam ("hicut", "Hi Cut", hc, 9000.0f, fmtHz));
    v.push_back (fparam ("locut", "Lo Cut", lc, 180.0f, fmtHz));
    v.push_back (fparam ("deess", "De-Ess", { 0.0f, 100.0f }, 70.0f, fmtPct));
    v.push_back (fparam ("shape", "Curve", { -100.0f, 100.0f }, -25.0f, fmtBi));
    v.push_back (fparam ("pitch", "Pitch", { -24.0f, 0.0f, 1.0f }, 0.0f, fmtSt));
    v.push_back (fparam ("filter", "Filter", { 0.0f, 100.0f }, 0.0f, fmtPct));
    v.push_back (cparam ("filterType", "Filter Type", { "LP", "HP", "BP" }, 0));
    v.push_back (fparam ("widthStart", "Width In", { 0.0f, 100.0f }, 40.0f, fmtPct));
    v.push_back (fparam ("widthEnd", "Width Out", { 0.0f, 150.0f }, 130.0f, fmtPct));
    v.push_back (fparam ("post", "Post Verb", { 0.0f, 100.0f }, 0.0f, fmtPct));
    v.push_back (bparam ("phone", "Phone", false));
    v.push_back (bparam ("chorus", "Chorus", false));
    v.push_back (bparam ("tremolo", "Tremolo", false));
    v.push_back (bparam ("stutter", "Stutter", false));
    v.push_back (bparam ("lofi", "Lo-Fi", false));
    v.push_back (bparam ("echo", "Echo", false));
    v.push_back (cparam ("fxRate", "FX Rate", { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    v.push_back (bparam ("accel", "Accel", true));
    v.push_back (bparam ("includeDry", "Include Dry", true));
    v.push_back (bparam ("normalize", "Normalize", true));
    v.push_back (fparam ("gain", "Gain", { -24.0f, 6.0f }, 0.0f, fmtDb));
    v.push_back (bparam ("syncPreview", "Sync Preview", true));
    v.push_back (bparam ("monitor", "Input Thru", true));
    return { v.begin(), v.end() };
}

SIRiserProcessor::SIRiserProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true)),
      Thread ("SI Riser Render"),
      apvts (*this, nullptr, "SIRiser", createLayout()),
      presets (apvts, "SI Riser", factoryPresets())
{
    formats.registerBasicFormats();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (rp->getParameterID() != "syncPreview" && rp->getParameterID() != "monitor")
                apvts.addParameterListener (rp->getParameterID(), this);
    startThread (Thread::Priority::low);
    startTimerHz (30);
}

SIRiserProcessor::~SIRiserProcessor()
{
    stopTimer();
    cancelRender = true;
    signalThreadShouldExit();
    notify();
    stopThread (4000);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

bool SIRiserProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    const auto in = l.getMainInputChannelSet();
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

void SIRiserProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate != sr || capture.getNumSamples() == 0)
    {
        armed = false;
        sr = sampleRate;
        capture.setSize (2, (int) (kMaxCaptureSec * sr));
        if (sourceLen > 0) kickRender();   // result must match the new rate
    }
    capPos = 0;
}

void SIRiserProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (numIn > 1 ? 1 : 0);

    // ---- tempo + transport
    double tempo = apvts.getRawParameterValue ("tempoBpm")->load();
    bool hostOk = false, playing = false;
    Optional<double> ppq;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition();
            if (apvts.getRawParameterValue ("tempoSource")->load() < 0.5f)
                if (auto b = pos->getBpm()) { tempo = *b; hostOk = true; }
        }
    tempo = jlimit (20.0, 400.0, tempo);
    bpm = (float) tempo;
    hostBpmValid = hostOk;
    hostPlaying = playing;
    if (ppq.hasValue()) ppqNow = *ppq;

    // ---- level + capture
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (inL[i]), std::abs (inR[i])));
    inLevel = pk;
    if (armed.load())
    {
        const int room = capture.getNumSamples() - capPos;
        const int m = std::min (room, n);
        capture.copyFrom (0, capPos, inL, m);
        capture.copyFrom (1, capPos, inR, m);
        capPos += m;
        if (pk > 0.0032f) heardSignal = true;
        silentFor = (heardSignal && pk < 0.0018f) ? silentFor + n : 0;
        recSeconds = (float) (capPos / sr);
        status = heardSignal ? Status::Recording : Status::Armed;
        if (capPos >= capture.getNumSamples() || silentFor > (int) (1.2 * sr))
        {
            armed = false;
            captureDone = true;
        }
    }

    if (apvts.getRawParameterValue ("monitor")->load() < 0.5f)
        buffer.clear();

    // ---- preview transport
    const int cmd = previewCmd.exchange (0);
    if (cmd == 2) { previewPlaying = false; previewWaiting = false; }
    if (cmd == 1)
    {
        previewPlaying = false;
        previewWaiting = false;
        const bool sync = apvts.getRawParameterValue ("syncPreview")->load() > 0.5f;
        std::shared_ptr<si::RiserResult> r;
        {
            const SpinLock::ScopedTryLockType tl (resultLock);
            if (tl.isLocked()) r = result;
        }
        if (sync && playing && ppq.hasValue() && r != nullptr)
        {
            // start so that the drop lands on the next possible bar line
            double barBeats = 4.0;
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                    if (auto ts = pos->getTimeSignature()) barBeats = ts->numerator * 4.0 / jmax (1, ts->denominator);
            const double lb = r->riserSamples / r->sampleRate * tempo / 60.0;
            double target = std::ceil ((*ppq + lb) / barBeats) * barBeats - lb;
            if (target < *ppq) target += barBeats;
            waitTargetPpq = target;
            previewWaiting = true;
        }
        else
        {
            previewPos = 0;
            previewPlaying = true;
        }
    }

    int startOffset = 0;
    if (previewWaiting.load())
    {
        if (! playing || ! ppq.hasValue()) previewWaiting = false;
        else
        {
            const double off = (waitTargetPpq - *ppq) * sr * 60.0 / tempo;
            if (off < n)
            {
                startOffset = jlimit (0, n - 1, (int) off);
                previewWaiting = false;
                previewPos = 0;
                previewPlaying = true;
            }
        }
    }

    if (previewPlaying.load())
    {
        const SpinLock::ScopedTryLockType tl (resultLock);
        if (tl.isLocked() && result != nullptr)
        {
            const auto& a = result->audio;
            int64_t pos = previewPos.load();
            const int avail = (int) std::max<int64_t> (0, a.getNumSamples() - pos);
            const int m = std::min (avail, n - startOffset);
            for (int ch = 0; ch < numOut; ++ch)
                buffer.addFrom (ch, startOffset, a, std::min (ch, 1), (int) pos, m);
            pos += m;
            previewPos = pos;
            if (pos >= a.getNumSamples()) previewPlaying = false;
        }
        else if (tl.isLocked())
            previewPlaying = false;
    }
}

si::RiserSettings SIRiserProcessor::makeSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto b = [&v] (const char* id) { return v (id) > 0.5f; };
    si::RiserSettings s;
    s.bpm = bpm.load();
    const int li = (int) v ("length");
    s.lengthBeats = kLengthBeats[li];
    ignoreUnused (kLengthIsBar);
    s.feed = (int) v ("feed");
    s.verbMode = (int) v ("verbMode");
    s.verbColor = (int) v ("verbColor");
    s.size = v ("size") * 0.01f;
    s.tail = v ("tail");
    s.hiCut = v ("hicut");
    s.loCut = v ("locut");
    s.deEss = v ("deess") * 0.01f;
    s.shape = v ("shape") * 0.01f;
    s.pitch = v ("pitch");
    s.filter = v ("filter") * 0.01f;
    s.filterType = (int) v ("filterType");
    s.widthStart = v ("widthStart") * 0.01f;
    s.widthEnd = v ("widthEnd") * 0.01f;
    s.phone = b ("phone"); s.chorus = b ("chorus"); s.tremolo = b ("tremolo");
    s.stutter = b ("stutter"); s.lofi = b ("lofi"); s.echo = b ("echo");
    s.fxBeats = kFxBeats[(int) v ("fxRate")];
    s.accel = b ("accel");
    s.includeDry = b ("includeDry");
    s.normalize = b ("normalize");
    s.gainDb = v ("gain");
    s.post = v ("post") * 0.01f;
    return s;
}

void SIRiserProcessor::parameterChanged (const String&, float)
{
    dirtyAt = Time::getMillisecondCounter() | 1u;   // debounced in timerCallback
}

void SIRiserProcessor::kickRender()
{
    if (sourceLen.load() <= 0) return;
    lastRenderBpm = bpm.load();
    cancelRender = true;
    renderRequested = true;
    notify();
}

void SIRiserProcessor::timerCallback()
{
    if (captureDone.exchange (false))
    {
        {
            const ScopedLock sl (sourceLock);
            const int len = capPos;
            source.setSize (2, std::max (1, len));
            for (int ch = 0; ch < 2; ++ch) source.copyFrom (ch, 0, capture, ch, 0, len);
            sourceRate = sr;
            sourceLen = len;
        }
        capPos = 0;
        kickRender();
    }
    const auto d = dirtyAt.load();
    if (d != 0 && Time::getMillisecondCounter() - d > 180)
    {
        dirtyAt = 0;
        kickRender();
    }
    if (sourceLen.load() > 0 && std::abs (bpm.load() - lastRenderBpm) > 0.05f && status.load() != Status::Rendering
        && status.load() != Status::Recording)
        kickRender();
}

void SIRiserProcessor::run()
{
    while (! threadShouldExit())
    {
        wait (200);
        if (! renderRequested.exchange (false)) continue;
        cancelRender = false;
        status = Status::Rendering;

        const auto settings = makeSettings();
        const double rate = sr;
        AudioBuffer<float> src;
        {
            const ScopedLock sl (sourceLock);
            if (sourceLen.load() <= 0) { status = Status::Empty; continue; }
            if (std::abs (sourceRate - rate) < 1.0)
                src.makeCopyOf (source);
            else
            {
                const double ratio = sourceRate / rate;
                const int outLen = (int) (source.getNumSamples() / ratio);
                src.setSize (2, outLen);
                for (int ch = 0; ch < 2; ++ch)
                {
                    LagrangeInterpolator li;
                    li.process (ratio, source.getReadPointer (std::min (ch, source.getNumChannels() - 1)), src.getWritePointer (ch), outLen);
                }
            }
        }

        std::shared_ptr<si::RiserResult> res (si::renderRiser (settings, src, rate, &cancelRender).release());
        if (cancelRender.load() || renderRequested.load()) continue;   // superseded
        if (res == nullptr) { status = Status::Failed; continue; }
        {
            const SpinLock::ScopedLockType sl (resultLock);
            std::swap (result, res);
        }
        res.reset();
        ++resultVersion;
        status = Status::Ready;
    }
}

// ---- UI API
void SIRiserProcessor::toggleCapture()
{
    if (armed.load())
    {
        armed = false;
        if (capPos > 0) captureDone = true;
        else status = sourceLen > 0 ? Status::Ready : Status::Empty;
        return;
    }
    previewCmd = 2;
    capPos = 0;
    silentFor = 0;
    heardSignal = false;
    recSeconds = 0.0f;
    status = Status::Armed;
    armed = true;
}

bool SIRiserProcessor::loadFile (const File& f)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;
    const int len = (int) std::min<int64> (reader->lengthInSamples, (int64) (60.0 * reader->sampleRate));
    AudioBuffer<float> tmp (2, len);
    reader->read (&tmp, 0, len, 0, true, true);
    {
        const ScopedLock sl (sourceLock);
        source = std::move (tmp);
        sourceRate = reader->sampleRate;
        sourceLen = len;
    }
    kickRender();
    return true;
}

void SIRiserProcessor::requestPreview()
{
    previewCmd = (previewPlaying.load() || previewWaiting.load()) ? 2 : 1;
}

std::shared_ptr<si::RiserResult> SIRiserProcessor::getResult() const
{
    const SpinLock::ScopedLockType sl (resultLock);
    return result;
}

File SIRiserProcessor::writeRenderToFile (bool forDrag)
{
    auto r = getResult();
    if (r == nullptr) return {};
    auto dir = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("SkillIssue").getChildFile ("SI Riser Renders");
    dir.createDirectory();
    static const char* lenNames[] = { "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars" };
    const String base = "SI Riser " + String (lenNames[(int) apvts.getRawParameterValue ("length")->load()]) + " "
                        + String (roundToInt (r->bpm)) + "bpm" + (forDrag ? "" : " export");
    auto file = dir.getNonexistentChildFile (base + " " + String (++dragCounter).paddedLeft ('0', 3), ".wav", false);

    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (file.createOutputStream());
    if (os == nullptr) return {};
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), r->sampleRate, 2, 24, {}, 0));
    if (w == nullptr) return {};
    os.release();   // owned by the writer now
    w->writeFromAudioSampleBuffer (r->audio, 0, r->audio.getNumSamples());
    return file;
}

// ---- state: parameters + captured source (FLAC) so projects reopen with the riser
void SIRiserProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("SOURCE"), nullptr);
    {
        const ScopedLock sl (sourceLock);
        if (sourceLen.load() > 0)
        {
            MemoryBlock mb;
            FlacAudioFormat flac;
            if (auto* w = flac.createWriterFor (new MemoryOutputStream (mb, false), sourceRate, 2, 24, {}, 0))
            {
                w->writeFromAudioSampleBuffer (source, 0, sourceLen.load());
                delete w;
                ValueTree src ("SOURCE");
                src.setProperty ("rate", sourceRate, nullptr);
                src.setProperty ("flac", var (mb), nullptr);
                state.appendChild (src, nullptr);
            }
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SIRiserProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = ValueTree::fromXml (*xml);
    auto src = tree.getChildWithName ("SOURCE");
    tree.removeChild (src, nullptr);
    apvts.replaceState (tree);
    presets.restoreFromState();

    if (src.isValid())
        if (auto* mb = src.getProperty ("flac").getBinaryData())
        {
            FlacAudioFormat flac;
            std::unique_ptr<AudioFormatReader> r (flac.createReaderFor (new MemoryInputStream (*mb, true), true));
            if (r != nullptr && r->lengthInSamples > 0)
            {
                const int len = (int) r->lengthInSamples;
                AudioBuffer<float> tmp (2, len);
                r->read (&tmp, 0, len, 0, true, true);
                const ScopedLock sl (sourceLock);
                source = std::move (tmp);
                sourceRate = r->sampleRate;
                sourceLen = len;
            }
        }
    if (sourceLen.load() > 0) kickRender();
}

AudioProcessorEditor* SIRiserProcessor::createEditor() { return new SIRiserEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SIRiserProcessor(); }
