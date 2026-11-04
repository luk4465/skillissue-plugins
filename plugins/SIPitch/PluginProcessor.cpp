#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
String fmtPct (float v, int) { return String (roundToInt (v)) + "%"; }

// interval patterns (semitones from the root), same list as SI Tune
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

File userDir (const char* sub)
{
    auto d = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("SkillIssue").getChildFile (sub);
    d.createDirectory();
    return d;
}

bool writeAudio (AudioFormat& fmt, const AudioBuffer<float>& a, double rate, const File& f)
{
    f.deleteFile();
    auto os = f.createOutputStream();
    if (os == nullptr) return false;
    std::unique_ptr<AudioFormatWriter> w (fmt.createWriterFor (os.get(), rate, (unsigned int) a.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples());
}

bool readAudio (const File& f, AudioBuffer<float>& out, double& rate)
{
    AudioFormatManager m;
    m.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (m.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0 || r->lengthInSamples > (int64) std::numeric_limits<int>::max()) return false;
    const int n = (int) r->lengthInSamples;
    out.setSize (2, n);
    r->read (&out, 0, n, 0, true, true);
    if (r->numChannels == 1) out.copyFrom (1, 0, out, 0, 0, n);
    rate = r->sampleRate;
    return true;
}

String timeText (double s)
{
    const int m = (int) (s / 60.0);
    return String (m) + ":" + String (s - m * 60.0, 1).paddedLeft ('0', 4);
}
} // namespace

const StringArray& SIPitchProcessor::keyNames()
{
    static const StringArray k { "C", "C# / Db", "D", "D# / Eb", "E", "F", "F# / Gb", "G", "G# / Ab", "A", "A# / Bb", "B" };
    return k;
}

const StringArray& SIPitchProcessor::scaleNames()
{
    static const StringArray s { "Chromatic", "Major", "Minor", "Harm. Minor", "Major Pent.", "Minor Pent.", "Blues", "Dorian", "Phrygian", "Mixolydian" };
    return s;
}

AudioProcessorValueTreeState::ParameterLayout SIPitchProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keyNames(), 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames(), 1));
    auto f = [&] (const char* id, const char* name, float lo, float hi, float def)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def, AudioParameterFloatAttributes().withStringFromValueFunction (fmtPct))); };
    f ("center", "Pitch Center", 0.0f, 100.0f, 0.0f);
    f ("drift", "Pitch Drift", 0.0f, 100.0f, 0.0f);
    f ("vibrato", "Vibrato", 0.0f, 200.0f, 100.0f);
    return { v.begin(), v.end() };
}

SIPitchProcessor::SIPitchProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SIPitch", createLayout())
{
    lastCorrection = currentCorrection();
    startTimerHz (20);
}

SIPitchProcessor::~SIPitchProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 20000);
    live.store (nullptr);
    owned.clear();
    freeChunks();
}

si::pitch::Correction SIPitchProcessor::currentCorrection() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    si::pitch::Correction c;
    const int key = (int) v ("key"), scale = jlimit (0, (int) kScales.size() - 1, (int) v ("scale"));
    c.scaleMask = 0;
    for (int iv : kScales[(size_t) scale]) c.scaleMask |= 1 << ((key + iv) % 12);
    c.center = v ("center") * 0.01f;
    c.drift = v ("drift") * 0.01f;
    c.vibrato = v ("vibrato") * 0.01f;
    return c;
}

bool SIPitchProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == AudioChannelSet::stereo() || out == AudioChannelSet::mono()) && l.getMainInputChannelSet() == out;
}

void SIPitchProcessor::prepareToPlay (double sampleRate, int) { sr = sampleRate; }

//==============================================================================
void SIPitchProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    int64 pos = 0;
    bool isPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto p = ph->getPosition())
        {
            isPlaying = p->getIsPlaying();
            if (auto t = p->getTimeInSamples()) pos = *t;
            else if (auto s = p->getTimeInSeconds()) pos = (int64) std::llround (*s * sr);
            if (auto b = p->getBpm()) bpm = *b;
        }
    playing = isPlaying;
    hostSeconds = (double) pos / sr;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // ---- transfer (recording passes the input through)
    if (recording && (! armed.load() || ! isPlaying || pos != recStart + recLen))
    {
        recording = false;
        armed = false;
        finalizePending = true;
    }
    if (! recording && armed.load() && isPlaying && ! finalizePending.load())
    {
        recording = true;
        recStart = pos;
        recLen = 0;
        recStartShared = pos;
        status = (int) Status::Recording;
    }
    if (recording)
    {
        for (int i = 0; i < n; ++i)
        {
            const int idx = (int) (recLen / kChunk), off = (int) (recLen % kChunk);
            float* cl = idx < kMaxChunks ? chunkL[(size_t) idx].load (std::memory_order_acquire) : nullptr;
            float* cr = idx < kMaxChunks ? chunkR[(size_t) idx].load (std::memory_order_acquire) : nullptr;
            if (cl == nullptr || cr == nullptr)   // out of buffer: end the take here
            {
                recording = false;
                armed = false;
                finalizePending = true;
                break;
            }
            cl[off] = L[i];
            cr[off] = R != nullptr ? R[i] : L[i];
            ++recLen;
        }
        recordedSamples = recLen;
        return;
    }

    // ---- playback of the edited take, in sync with the timeline
    if (! isPlaying) return;
    if (auto* r = live.load (std::memory_order_acquire))
    {
        if (std::abs (r->sr - sr) > 1.0) return;
        const int len = r->audio.getNumSamples();
        const float* a = r->audio.getReadPointer (0);
        const float* b = r->audio.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const int64 p = pos + i - r->hostStart;
            if (p < 0 || p >= len) continue;
            if (R != nullptr) { L[i] = a[p]; R[i] = b[p]; }
            else L[i] = 0.5f * (a[p] + b[p]);
        }
    }
}

//==============================================================================
void SIPitchProcessor::setArmed (bool on)
{
    if (on)
    {
        if (finalizePending.load() || getStatus() == Status::Analysing) return;
        while (chunksAllocated < 64)
        {
            chunkL[(size_t) chunksAllocated].store (new float[kChunk], std::memory_order_release);
            chunkR[(size_t) chunksAllocated].store (new float[kChunk], std::memory_order_release);
            ++chunksAllocated;
        }
        recordedSamples = 0;
        armed = true;
        status = (int) Status::Armed;
    }
    else
    {
        armed = false;
        if (getStatus() == Status::Armed) status = (int) (take != nullptr ? Status::Ready : Status::Empty);
    }
}

void SIPitchProcessor::freeChunks()
{
    for (int i = 0; i < kMaxChunks; ++i)
    {
        delete[] chunkL[(size_t) i].exchange (nullptr);
        delete[] chunkR[(size_t) i].exchange (nullptr);
    }
    chunksAllocated = 0;
}

void SIPitchProcessor::finishRecording()
{
    const int64 len = recordedSamples.load();
    if (len < (int64) (sr * 0.25))
    {
        freeChunks();
        status = (int) (take != nullptr ? Status::Ready : Status::Empty);
        return;
    }
    AudioBuffer<float> a (2, (int) len);
    for (int64 i = 0; i < len; i += kChunk)
    {
        const int idx = (int) (i / kChunk), cnt = (int) std::min ((int64) kChunk, len - i);
        a.copyFrom (0, (int) i, chunkL[(size_t) idx].load(), cnt);
        a.copyFrom (1, (int) i, chunkR[(size_t) idx].load(), cnt);
    }
    freeChunks();
    startAnalysis (std::move (a), sr, recStartShared.load(), File(), {});
}

void SIPitchProcessor::startAnalysis (AudioBuffer<float>&& audio, double rate, int64 start, File existing, std::vector<si::pitch::Note>)
{
    status = (int) Status::Analysing;
    progress = 0.0f;
    auto buf = std::make_shared<AudioBuffer<float>> (std::move (audio));
    pool.addJob ([this, buf, rate, start, existing] { analyseTake (std::move (*buf), rate, start, existing); });
}

void SIPitchProcessor::analyseTake (AudioBuffer<float>&& audio, double rate, int64 start, const File& existing)
{
    auto t = std::make_unique<Take>();
    t->audio = std::move (audio);
    t->sr = rate;
    t->hostStart = start;
    const int n = t->audio.getNumSamples();
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (t->audio.getSample (0, i) + t->audio.getSample (1, i));
    t->an = si::pitch::analyse (mono.data(), n, rate, [this] (float p) { progress = p; });
    t->file = existing;
    if (! existing.existsAsFile())
    {
        FlacAudioFormat flac;
        t->file = userDir ("SI Pitch Takes").getChildFile ("Take " + Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".flac");
        writeAudio (flac, t->audio, rate, t->file);
    }
    const ScopedLock sl (resultLock);
    pendingTake = std::move (t);
}

bool SIPitchProcessor::loadFile (const File& f)
{
    const auto st = getStatus();
    if (st == Status::Analysing || st == Status::Recording || armed.load()) return false;
    const int64 start = (int64) std::llround (hostSeconds.load() * sr);
    const double hostRate = sr;
    status = (int) Status::Analysing;
    progress = 0.0f;
    pool.addJob ([this, f, start, hostRate]
    {
        AudioBuffer<float> a;
        double rate = 0.0;
        if (! readAudio (f, a, rate))
        {
            const ScopedLock sl (resultLock);
            errorText = "Could not read " + f.getFileName();
            status = (int) Status::Error;
            return;
        }
        if (std::abs (rate - hostRate) > 1.0)   // play it at the project's rate
        {
            const double ratio = rate / hostRate;
            const int n2 = (int) std::floor (a.getNumSamples() / ratio) - 4;
            AudioBuffer<float> b (2, jmax (1, n2));
            for (int ch = 0; ch < 2; ++ch)
            {
                WindowedSincInterpolator interp;
                interp.process (ratio, a.getReadPointer (ch), b.getWritePointer (ch), b.getNumSamples());
            }
            a = std::move (b);
        }
        analyseTake (std::move (a), hostRate, start, File());
    });
    return true;
}

void SIPitchProcessor::applyRestoredEdits (std::vector<si::pitch::Note>& ns, const std::vector<si::pitch::Note>& saved)
{
    for (auto& s : saved)
        if (s.manual)
            for (auto& nt : ns)
                if (std::abs (nt.start - s.start) < 600 && std::abs (nt.end - s.end) < 600) { nt.manual = true; nt.target = s.target; break; }
}

void SIPitchProcessor::startRender()
{
    renderDirty = false;
    renderBusy = true;
    auto tk = take;
    auto ns = getNotes();
    const auto c = currentCorrection();
    lastCorrection = c;
    pool.addJob ([this, tk, ns, c]
    {
        auto off = si::pitch::offsetCurve (tk->an, ns, c);
        auto r = std::make_unique<Rendered>();
        r->hostStart = tk->hostStart;
        r->sr = tk->sr;
        const int n = tk->audio.getNumSamples();
        r->audio.setSize (2, n);
        if (si::pitch::isIdentity (off))
        {
            r->audio.copyFrom (0, 0, tk->audio, 0, 0, n);
            r->audio.copyFrom (1, 0, tk->audio, 1, 0, n);
        }
        else
        {
            const float* in[2] { tk->audio.getReadPointer (0), tk->audio.getReadPointer (1) };
            float* out[2] { r->audio.getWritePointer (0), r->audio.getWritePointer (1) };
            si::pitch::render (tk->an, off, in, out, 2);
        }
        {
            const ScopedLock sl (resultLock);
            pendingRender = std::move (r);
        }
        renderBusy = false;
    });
}

void SIPitchProcessor::timerCallback()
{
    // keep buffer allocated ahead of the recording
    if (armed.load() || getStatus() == Status::Recording)
    {
        const int need = jmin (kMaxChunks, (int) (recordedSamples.load() / kChunk) + 64);
        while (chunksAllocated < need)
        {
            chunkL[(size_t) chunksAllocated].store (new float[kChunk], std::memory_order_release);
            chunkR[(size_t) chunksAllocated].store (new float[kChunk], std::memory_order_release);
            ++chunksAllocated;
        }
    }
    if (finalizePending.load())
    {
        finalizePending = false;
        finishRecording();
    }

    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    {
        const ScopedLock sl (resultLock);
        if (pendingTake != nullptr)
        {
            take = std::shared_ptr<const Take> (pendingTake.release());
            {
                const ScopedLock nl (notesLock);
                notes = take->an.notes;
                applyRestoredEdits (notes, savedEdits);
            }
            savedEdits.clear();
            status = (int) Status::Ready;
            renderDirty = true;
        }
        if (pendingRender != nullptr)
        {
            owned.push_back ({ std::move (pendingRender), now });
            live.store (owned.back().first.get(), std::memory_order_release);
        }
    }
    // free renders the audio thread can no longer be reading
    for (size_t i = 0; i + 1 < owned.size();)
    {
        if (now - owned[i + 1].second > 3.0) owned.erase (owned.begin() + (long) i);
        else ++i;
    }

    const auto c = currentCorrection();
    if (c.scaleMask != lastCorrection.scaleMask || c.center != lastCorrection.center || c.drift != lastCorrection.drift || c.vibrato != lastCorrection.vibrato)
    {
        lastCorrection = c;
        renderDirty = true;
    }
    if (renderDirty && take != nullptr && ! renderBusy.load()) startRender();
}

//==============================================================================
std::vector<si::pitch::Note> SIPitchProcessor::getNotes() const
{
    const ScopedLock sl (notesLock);
    return notes;
}

void SIPitchProcessor::setNotes (std::vector<si::pitch::Note> ns)
{
    {
        const ScopedLock sl (notesLock);
        notes = std::move (ns);
    }
    renderDirty = true;
}

void SIPitchProcessor::clearTake()
{
    live.store (nullptr, std::memory_order_release);
    owned.push_back ({ nullptr, Time::getMillisecondCounterHiRes() * 0.001 });
    take.reset();
    setNotes ({});
    renderDirty = false;
    status = (int) Status::Empty;
}

File SIPitchProcessor::exportRendered()
{
    auto* r = live.load();
    if (r == nullptr) return {};
    WavAudioFormat wav;
    auto f = userDir ("SI Pitch Exports").getChildFile ("SI Pitch " + Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S")
                                                        + " (starts " + timeText ((double) r->hostStart / r->sr).replaceCharacter (':', '.') + ").wav");
    return writeAudio (wav, r->audio, r->sr, f) ? f : File();
}

String SIPitchProcessor::getMessage() const
{
    switch (getStatus())
    {
        case Status::Empty:     return "Press Transfer, then play the part with the vocal in your DAW";
        case Status::Armed:     return "Armed - press play in your DAW, stop when the vocal is done";
        case Status::Recording: return "Recording " + timeText ((double) recordedSamples.load() / sr) + " - press stop when the vocal is done";
        case Status::Analysing: return "Analysing notes " + String (roundToInt (progress.load() * 100.0f)) + "%";
        case Status::Error:     { const ScopedLock sl (resultLock); return errorText; }
        case Status::Ready:     break;
    }
    if (take == nullptr) return {};
    if (std::abs (take->sr - sr) > 1.0) return "The take was recorded at " + String (take->sr, 0) + " Hz - set the project back to it";
    const int nn = (int) take->an.notes.size();
    return String (nn) + (nn == 1 ? " note" : " notes") + ", take starts at " + timeText ((double) take->hostStart / take->sr)
         + (isRendering() ? "  -  rendering..." : "  -  drag a note to move it, Correct puts every note in key");
}

bool SIPitchProcessor::loadTakeSync (const File& f)
{
    AudioBuffer<float> a;
    double rate = 0.0;
    if (! readAudio (f, a, rate)) return false;
    auto t = std::make_shared<Take>();
    t->sr = rate;
    t->file = f;
    const int n = a.getNumSamples();
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (a.getSample (0, i) + a.getSample (1, i));
    t->an = si::pitch::analyse (mono.data(), n, rate);
    t->audio = std::move (a);
    take = t;
    {
        const ScopedLock sl (notesLock);
        notes = take->an.notes;
    }
    status = (int) Status::Ready;
    // render right here
    auto off = si::pitch::offsetCurve (take->an, getNotes(), currentCorrection());
    auto r = std::make_unique<Rendered>();
    r->sr = rate;
    r->audio.makeCopyOf (take->audio);
    if (! si::pitch::isIdentity (off))
    {
        const float* in[2] { take->audio.getReadPointer (0), take->audio.getReadPointer (1) };
        float* out[2] { r->audio.getWritePointer (0), r->audio.getWritePointer (1) };
        si::pitch::render (take->an, off, in, out, 2);
    }
    owned.push_back ({ std::move (r), Time::getMillisecondCounterHiRes() * 0.001 });
    live.store (owned.back().first.get());
    lastCorrection = currentCorrection();
    return true;
}

//==============================================================================
AudioProcessorEditor* SIPitchProcessor::createEditor() { return new SIPitchEditor (*this); }

void SIPitchProcessor::getStateInformation (MemoryBlock& dest)
{
    auto st = apvts.copyState();
    if (take != nullptr && take->file.existsAsFile())
    {
        ValueTree tk ("Take");
        tk.setProperty ("file", take->file.getFullPathName(), nullptr);
        tk.setProperty ("start", String (take->hostStart), nullptr);
        for (auto& nt : getNotes())
            if (nt.manual)
            {
                ValueTree e ("Edit");
                e.setProperty ("s", nt.start, nullptr);
                e.setProperty ("e", nt.end, nullptr);
                e.setProperty ("t", nt.target, nullptr);
                tk.appendChild (e, nullptr);
            }
        st.appendChild (tk, nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void SIPitchProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = ValueTree::fromXml (*xml);
    auto tk = tree.getChildWithName ("Take");
    if (tk.isValid()) tree.removeChild (tk, nullptr);
    apvts.replaceState (tree);
    lastCorrection = currentCorrection();
    if (! tk.isValid()) return;
    const File f (tk.getProperty ("file").toString());
    if (! f.existsAsFile())
    {
        errorText = "Take file is missing: " + f.getFullPathName();
        status = (int) Status::Error;
        return;
    }
    savedEdits.clear();
    for (auto e : tk)
    {
        si::pitch::Note nt;
        nt.start = e.getProperty ("s");
        nt.end = e.getProperty ("e");
        nt.target = (float) e.getProperty ("t");
        nt.manual = true;
        savedEdits.push_back (nt);
    }
    const int64 start = tk.getProperty ("start").toString().getLargeIntValue();
    status = (int) Status::Analysing;
    pool.addJob ([this, f, start]
    {
        AudioBuffer<float> a;
        double rate = 0.0;
        if (! readAudio (f, a, rate)) return;
        auto t = std::make_unique<Take>();
        t->sr = rate;
        t->hostStart = start;
        t->file = f;
        const int n = a.getNumSamples();
        std::vector<float> mono ((size_t) n);
        for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (a.getSample (0, i) + a.getSample (1, i));
        t->an = si::pitch::analyse (mono.data(), n, rate, [this] (float p) { progress = p; });
        t->audio = std::move (a);
        const ScopedLock sl (resultLock);
        pendingTake = std::move (t);
    });
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SIPitchProcessor(); }
