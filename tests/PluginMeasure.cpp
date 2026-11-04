// Black-box measurements of hosted VST3 compressors / EQs (used to match SI plugins to reference hardware emulations).
// SITests --list <vst3> [filter]                         : plugin names in the file + their parameters
// SITests --comp <vst3> <plugin> <settings> [label]      : static curve, attack/release, harmonics, frequency response
//   <settings>: "Param Name=text;Other=text" (text goes through the plugin's own parser, "#0.25" = normalised)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace juce;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBs = 256;

std::unique_ptr<AudioPluginInstance> load (const String& file, const String& name, String& err)
{
    VST3PluginFormat fmt;
    OwnedArray<PluginDescription> descs;
    fmt.findAllTypesForFile (descs, file);
    for (auto* d : descs)
        if (name.isEmpty() || d->name.equalsIgnoreCase (name))
        {
            auto inst = fmt.createInstanceFromDescription (*d, kSr, kBs, err);
            if (inst != nullptr)
            {
                auto layout = inst->getBusesLayout();
                for (auto& b : layout.inputBuses) if (! b.isDisabled()) b = AudioChannelSet::stereo();
                layout.outputBuses.getReference (0) = AudioChannelSet::stereo();
                for (int i = 1; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference (i) = AudioChannelSet::disabled();
                inst->setBusesLayout (layout);
                inst->prepareToPlay (kSr, kBs);
            }
            return inst;
        }
    err = "plugin '" + name + "' not found in file";
    return nullptr;
}

AudioProcessorParameter* findParam (AudioPluginInstance& p, const String& name)
{
    for (auto* q : p.getParameters()) if (q->getName (128).equalsIgnoreCase (name)) return q;
    for (auto* q : p.getParameters()) if (q->getName (128).containsIgnoreCase (name)) return q;
    return nullptr;
}

bool apply (AudioPluginInstance& p, const String& settings)
{
    bool ok = true;
    for (auto& item : StringArray::fromTokens (settings, ";", ""))
    {
        const auto key = item.upToFirstOccurrenceOf ("=", false, false).trim(), val = item.fromFirstOccurrenceOf ("=", false, false).trim();
        if (key.isEmpty()) continue;
        auto* q = findParam (p, key);
        if (q == nullptr) { std::cout << "  ! no parameter '" << key << "'\n"; ok = false; continue; }
        // many plugins don't parse text back, so search the normalised range for the closest displayed value
        float v = 0.0f;
        if (val.startsWith ("#")) v = val.substring (1).getFloatValue();
        else
        {
            const bool numeric = val.containsOnly ("+-.0123456789 ");
            const double want = val.getDoubleValue();
            double bestD = 1e30;
            for (int k = 0; k <= 4000; ++k)
            {
                const float x = (float) k / 4000.0f;
                const auto t = q->getText (x, 64).trim();
                double d;
                if (numeric)
                {
                    if (! t.containsAnyOf ("0123456789")) continue;
                    d = std::abs (t.retainCharacters ("+-.0123456789").getDoubleValue() - want);
                }
                else d = t.equalsIgnoreCase (val) || t.upToFirstOccurrenceOf (" ", false, false).equalsIgnoreCase (val) ? 0.0 : 1e9;
                if (d < bestD) { bestD = d; v = x; }
            }
        }
        q->setValueNotifyingHost (jlimit (0.0f, 1.0f, v));
        std::cout << "  " << q->getName (64) << " = " << q->getCurrentValueAsText() << "\n";
    }
    // let queued host->plugin changes arrive
    AudioBuffer<float> b (jmax (2, p.getTotalNumInputChannels()), kBs);
    MidiBuffer m;
    for (int i = 0; i < 8; ++i) { b.clear(); p.processBlock (b, m); }
    return ok;
}

/** Runs `in` (mono) through the plugin on both channels, returns the left output (latency removed). */
std::vector<float> run (AudioPluginInstance& p, const std::vector<float>& in)
{
    const int lat = p.getLatencySamples();
    const int n = (int) in.size();
    std::vector<float> out ((size_t) n + (size_t) lat + kBs, 0.0f);
    AudioBuffer<float> b (jmax (2, p.getTotalNumInputChannels()), kBs);
    MidiBuffer m;
    for (int pos = 0; pos < n + lat; pos += kBs)
    {
        b.clear();
        for (int i = 0; i < kBs; ++i)
        {
            const float x = pos + i < n ? in[(size_t) (pos + i)] : 0.0f;
            b.setSample (0, i, x);
            b.setSample (1, i, x);
        }
        p.processBlock (b, m);
        for (int i = 0; i < kBs; ++i) out[(size_t) (pos + i)] = b.getSample (0, i);
    }
    std::vector<float> o ((size_t) n);
    for (int i = 0; i < n; ++i) o[(size_t) i] = out[(size_t) (i + lat)];
    return o;
}

void settle (AudioPluginInstance& p)   // many plugins ignore reset(): let any gain reduction recover
{
    p.reset();
    run (p, std::vector<float> ((size_t) (2.5 * kSr), 0.0f));
}

std::vector<float> sine (double hz, double dbfs, double sec)
{
    const float a = (float) std::pow (10.0, dbfs / 20.0);
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = a * (float) std::sin (2.0 * MathConstants<double>::pi * hz * (double) i / kSr);
    return v;
}

double peakDb (const std::vector<float>& v, int a, int b)   // sine peak level from RMS
{
    double s = 0;
    for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return 10.0 * std::log10 (2.0 * s / std::max (1, b - a) + 1e-30);
}

double goertzel (const std::vector<float>& v, int a, int b, double hz)
{
    double s1 = 0, s2 = 0;
    const double c = 2.0 * std::cos (2.0 * MathConstants<double>::pi * hz / kSr);
    for (int i = a; i < b; ++i) { const double s0 = v[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) * 2.0 / (b - a);
}
} // namespace

int pluginList (const String& file, const String& filter)
{
    VST3PluginFormat fmt;
    OwnedArray<PluginDescription> descs;
    fmt.findAllTypesForFile (descs, file);
    std::cout << descs.size() << " plugin(s) in " << file << "\n";
    for (auto* d : descs)
    {
        if (filter.isNotEmpty() && ! d->name.containsIgnoreCase (filter)) continue;
        std::cout << "== " << d->name << "  (" << d->manufacturerName << ", " << d->version << ")\n";
        if (filter.isEmpty()) continue;
        String err;
        auto inst = fmt.createInstanceFromDescription (*d, kSr, kBs, err);
        if (inst == nullptr) { std::cout << "   cannot load: " << err << "\n"; continue; }
        std::cout << "   latency " << inst->getLatencySamples() << ", inputs " << inst->getTotalNumInputChannels() << "\n";
        int i = 0;
        for (auto* q : inst->getParameters())
        {
            String range;
            for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) range << " " << q->getText (v, 32);
            std::cout << "   [" << i++ << "] " << q->getName (64) << " = " << q->getCurrentValueAsText() << "  (steps " << q->getNumSteps() << "; 0..1:" << range << ")\n";
        }
    }
    return 0;
}

/** Writes the plugin's state blob to <out> (VST3 state is usually XML or a binary chunk). */
int stateDump (const String& file, const String& name, const String& out)
{
    String err;
    auto p = load (file, name, err);
    if (p == nullptr) { std::cout << "load failed: " << err << "\n"; return 1; }
    MemoryBlock mb;
    p->getStateInformation (mb);
    File (out).replaceWithData (mb.getData(), mb.getSize());
    std::cout << "state " << mb.getSize() << " bytes -> " << out << "\n";
    return 0;
}

int compMeasure (const String& file, const String& name, const String& settings, const String& label, const String& what)
{
    auto want = [&] (const char* part) { return what.isEmpty() || what.contains (part); };
    String err;
    auto p = load (file, name, err);
    if (p == nullptr) { std::cout << "load failed: " << err << "\n"; return 1; }
    std::cout << "### " << label << " (" << name << ")\n";
    // "@<file>;..." loads a saved state first (e.g. a rack with a given module in slot A)
    String rest = settings;
    if (rest.startsWith ("@"))
    {
        const File sf (rest.substring (1).upToFirstOccurrenceOf (";", false, false));
        MemoryBlock mb;
        if (sf.loadFileAsData (mb)) { p->setStateInformation (mb.getData(), (int) mb.getSize()); std::cout << "  state <- " << sf.getFileName() << "\n"; }
        else std::cout << "  ! cannot read state " << sf.getFullPathName() << "\n";
        rest = rest.fromFirstOccurrenceOf (";", false, false);
    }
    apply (*p, rest);

    // 0) small-signal gain (no compression): 1 kHz at -50 dBFS
    if (what.contains ("lin"))
    {
        settle (*p);
        const auto y = run (*p, sine (1000.0, -50.0, 1.0));
        std::cout << "  linear gain " << String (peakDb (y, (int) (0.6 * kSr), (int) (1.0 * kSr)) + 50.0, 2) << " dB\n";
    }

    // 1) static curve: 1 kHz sine, input -40..0 dBFS
    if (want ("static"))
    {
    String curve = "static (in -> out dBFS):";
    for (int db = -40; db <= 0; db += 4)
    {
        settle (*p);
        const auto y = run (*p, sine (1000.0, db, 1.2));
        curve << " " << db << ":" << String (peakDb (y, (int) (0.9 * kSr), (int) (1.2 * kSr)), 1);
    }
    std::cout << "  " << curve << "\n";
    }

    // 2) attack / release: 12 kHz carrier, -30 dBFS -> +20 dB step for 400 ms -> back; gain envelope with ~0.17 ms resolution
    if (want ("timing"))
    {
        // 3 kHz carrier: -30 dBFS, +20 dB for 500 ms at 0.6 s, back to -30 dBFS for 2 s. Gain profile (re the settled
        // gain before the step) at fixed times after the step up and after the step down, plus 63 % times.
        settle (*p);
        const double lo = -30.0, hi = -10.0, tUp = 0.6, tDown = 1.1;
        std::vector<float> x ((size_t) (3.1 * kSr));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / kSr;
            const double db = (t >= tUp && t < tDown) ? hi : lo;
            x[i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * MathConstants<double>::pi * 3000.0 * t));
        }
        const auto y = run (*p, x);
        const int w = 16;   // one cycle of 3 kHz
        auto gainAt = [&] (double t) { const int i = (int) (t * kSr); double sy = 0, sx = 0; for (int k = i; k < i + w; ++k) { sy += y[(size_t) k] * y[(size_t) k]; sx += x[(size_t) k] * x[(size_t) k]; } return 10.0 * std::log10 (sy / (sx + 1e-30) + 1e-30); };
        double g0 = 0;
        for (int k = 0; k < 20; ++k) g0 += gainAt (tUp - 0.1 + k * 0.004) / 20.0;
        String att = "attack GR (ms:dB):", rel = "release GR (ms:dB):";
        for (double ms : { 0.33, 0.67, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 480.0 }) att << " " << ms << ":" << String (gainAt (tUp + ms / 1000.0) - g0, 1);
        for (double ms : { 1.0, 5.0, 20.0, 50.0, 100.0, 200.0, 400.0, 700.0, 1000.0, 1500.0, 1950.0 }) rel << " " << ms << ":" << String (gainAt (tDown + ms / 1000.0) - g0, 1);
        const double gHold = gainAt (tDown - 0.02) - g0;
        double tAtt = -1, tRel = -1;
        for (double t = tUp; t < tDown; t += 1.0 / kSr * 4) if (gainAt (t) - g0 <= 0.63 * gHold) { tAtt = (t - tUp) * 1000.0; break; }
        for (double t = tDown; t < 3.0; t += 1.0 / kSr * 16) if (gainAt (t) - g0 >= 0.37 * gHold) { tRel = (t - tDown) * 1000.0; break; }
        std::cout << "  " << att << "\n  " << rel << "\n  GR at hold " << String (gHold, 1) << " dB, attack63 " << String (tAtt, 2) << " ms, release63 " << String (tRel, 1) << " ms\n";
    }

    // 3) harmonics (relative to the fundamental) at 100 Hz and 1 kHz, -18 and -6 dBFS
    if (want ("thd"))
    {
    String hs = "harmonics H2/H3 (dB re fund):";
    for (double f : { 100.0, 1000.0 })
        for (double db : { -18.0, -6.0 })
        {
            settle (*p);
            const auto y = run (*p, sine (f, db, 1.5));
            const int a = (int) (1.0 * kSr), b = (int) (1.5 * kSr);
            const double h1 = goertzel (y, a, b, f), h2 = goertzel (y, a, b, 2 * f), h3 = goertzel (y, a, b, 3 * f);
            hs << "  " << (int) f << "Hz@" << (int) db << ": " << String (20 * std::log10 (h2 / h1 + 1e-12), 1) << "/" << String (20 * std::log10 (h3 / h1 + 1e-12), 1);
        }
    std::cout << "  " << hs << "\n";
    }

    // 4) frequency response at -40 dBFS (no gain reduction), relative to 1 kHz
    if (want ("fr"))
    {
    String fr = "response @-40 dBFS (dB re 1k):";
    double ref = 0;
    for (double f : { 1000.0, 30.0, 60.0, 100.0, 300.0, 3000.0, 6000.0, 10000.0, 16000.0 })
    {
        settle (*p);
        const auto y = run (*p, sine (f, -40.0, 0.8));
        const double lv = peakDb (y, (int) (0.4 * kSr), (int) (0.8 * kSr));
        if (f == 1000.0) { ref = lv; continue; }
        fr << " " << (f < 1000 ? String ((int) f) : String (f / 1000.0, 0) + "k") << ":" << String (lv - ref, 2);
    }
    std::cout << "  " << fr << "\n";
    }
    p->releaseResources();
    return 0;
}

#include "dsp/FetComp.h"
/** SITests --fet <inputDb> <outputDb> <model>: FetComp gain for a -50 dBFS 1 kHz sine (engine only, no plugin). */
int fetGain (float inDb, float outDb, int mdl)
{
    FetComp c;
    c.prepare (kSr);
    c.inputDb = inDb; c.outputDb = outDb; c.model = mdl; c.mix = 1.0f; c.color = 0.35f;
    auto x = sine (1000.0, -50.0, 1.0);
    std::vector<float> r = x;
    for (int i = 0; i < (int) x.size(); i += kBs) c.process (x.data() + i, r.data() + i, std::min (kBs, (int) x.size() - i), 2);
    std::cout << "in " << inDb << " out " << outDb << " -> gain " << String (peakDb (x, (int) (0.5 * kSr), (int) (1.0 * kSr)) + 50.0, 2) << " dB\n";
    return 0;
}
