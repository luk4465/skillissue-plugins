#include "PluginEditor.h"
#include "Sidechain.h"

using namespace juce;
using namespace si::echo;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 940, H = 580;
float pv (si::APVTS& s, const char* id) { return s.getRawParameterValue (id)->load(); }

// short character description of every style for the Style panel: wow, saturation, bandwidth (0..1), noise
struct StyleInfo { float wow, sat, band, noise; const char* text; };
const StyleInfo kInfo[kNumStyles] = {
    { 0.0f, 0.0f, 1.0f, 0.0f, "Pristine digital repeats" },
    { 0.3f, 0.35f, 0.7f, 0.15f, "Warm studio tape machine" },
    { 0.15f, 0.25f, 0.85f, 0.1f, "Smooth, expensive mastering tape" },
    { 0.8f, 0.55f, 0.35f, 0.5f, "Wobbly, crunchy cassette" },
    { 0.6f, 0.65f, 0.3f, 0.35f, "Classic tape echo, dark repeats" },
    { 0.2f, 0.35f, 0.25f, 0.25f, "Bucket-brigade analog with chorus" },
    { 0.15f, 0.45f, 0.15f, 0.25f, "Darker bucket-brigade pedal" },
    { 0.1f, 0.45f, 0.45f, 0.1f, "Soft analog warmth" },
    { 0.0f, 0.25f, 0.5f, 0.0f, "12-bit sampler grit" },
    { 0.05f, 0.55f, 0.1f, 0.2f, "Narrow phone band" },
    { 0.1f, 0.35f, 0.2f, 0.7f, "Old AM radio, hiss" },
    { 0.3f, 0.45f, 0.15f, 0.9f, "Lo-fi transmitter, bit-crushed" },
    { 0.1f, 0.15f, 0.5f, 0.0f, "Diffused, reverb-like repeats" },
    { 0.3f, 0.35f, 0.5f, 0.15f, "Tape that smears into space" },
    { 0.0f, 0.05f, 1.0f, 0.0f, "Bright, thin digital" },
    { 0.5f, 0.75f, 0.2f, 0.25f, "Saturated dub feedback" },
};

//==============================================================================
class EchoView : public Component, private Timer
{
public:
    explicit EchoView (SIEchoProcessor& p) : proc (p) { startTimerHz (30); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.38f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        auto plot = r.reduced (16.0f, 30.0f);
        const auto p = proc.readParams();
        const double beat = 60.0 / jmax (20.0f, proc.bpm.load());
        const double maxT = jmax (p.timeL, p.mode == Mode::Dual ? p.timeR : 0.0);
        const double span = jlimit (beat, 8.0, maxT * (p.mode == Mode::Rhythm ? 9.0 : 8.5));
        auto tx = [&] (double t) { return plot.getX() + plot.getWidth() * (float) (t / span); };
        const float mid = plot.getCentreY();

        // beat grid
        g.setFont (si::font (8.5f, Weight::Medium));
        const double step = span / beat > 32.0 ? beat * 4.0 : beat;
        int k = 0;
        for (double t = 0.0; t <= span + 1e-6; t += step, ++k)
        {
            const bool bar = std::fmod (t / beat + 1e-6, 4.0) < 0.01;
            g.setColour (Colours::white.withAlpha (bar ? 0.10f : 0.04f));
            g.fillRect (tx (t), plot.getY(), 1.0f, plot.getHeight());
        }
        g.setColour (Colours::white.withAlpha (0.10f));
        g.fillRect (plot.getX(), mid, plot.getWidth(), 1.0f);

        // taps
        const float fb = p.freeze ? 1.0f : jmin (1.0f, p.feedback);
        const float lit = jlimit (0.0f, 1.0f, proc.engine.wetLevel.load() * 3.0f);
        auto tap = [&] (double t, float level, bool right)
        {
            if (t > span || level < 0.01f) return;
            const float x = tx (t);
            const float h = plot.getHeight() * 0.46f * jlimit (0.0f, 1.0f, level);
            auto bar = right ? Rectangle<float> (x - 3.0f, mid + 2.0f, 6.0f, h) : Rectangle<float> (x - 3.0f, mid - 2.0f - h, 6.0f, h);
            const auto c = right ? col::info : col::accent;
            g.setGradientFill (ColourGradient (c.withAlpha (0.35f + 0.65f * level), x, right ? bar.getBottom() : bar.getY(), c.withAlpha (0.15f), x, mid, false));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (c.interpolatedWith (Colours::white, 0.4f * lit));
            g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ x, right ? bar.getBottom() : bar.getY() }));
        };
        float lvl = 1.0f;
        switch (p.mode)
        {
            case Mode::Single:
                for (int n = 1; n <= 12; ++n, lvl *= fb) { tap (p.timeL * n, lvl, false); tap (p.timeL * n, lvl * 0.92f, true); }
                break;
            case Mode::Dual:
                for (int n = 1; n <= 12; ++n, lvl *= fb) { tap (p.timeL * n, lvl, false); tap (p.timeR * n, lvl, true); }
                break;
            case Mode::PingPong:
                for (int n = 1; n <= 16; ++n, lvl *= fb)
                    tap (p.timeL * n + (n % 2 == 0 ? p.timeL * 0.5 * p.groove : 0.0), lvl, n % 2 == 0);
                break;
            case Mode::Rhythm:
            {
                const auto& pat = pattern (p.patternIdx);
                for (int rep = 0; rep < 3; ++rep, lvl *= fb * 0.6f)
                    for (int s = 0; s < 8; ++s)
                        tap (p.timeL * (rep * 8 + s + 1) * (s % 2 ? 1.0 + 0.33 * p.groove : 1.0), lvl * pat[(size_t) s], s % 2 == 1);
                break;
            }
        }
        // dry hit
        g.setColour (col::text);
        g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre ({ plot.getX(), mid }));

        // readouts
        auto top = r.reduced (14.0f, 8.0f).removeFromTop (18.0f);
        g.setFont (si::font (10.0f, Weight::SemiBold, 0.06f));
        const bool sync = pv (proc.apvts, "sync") > 0.5f;
        String t = (sync ? SIEchoProcessor::noteNames()[(int) pv (proc.apvts, "noteL")] + "  " : String()) + String (roundToInt (p.timeL * 1000.0)) + " ms";
        if (p.mode == Mode::Dual) t << "   R " << (sync ? SIEchoProcessor::noteNames()[(int) pv (proc.apvts, "noteR")] + "  " : String()) << roundToInt (p.timeR * 1000.0) << " ms";
        g.setColour (col::text);
        g.drawText (t, top, Justification::centredLeft);
        g.setColour (p.freeze ? col::info : col::label);
        g.drawText (p.freeze ? "FROZEN" : String (styleName (p.style)).toUpperCase(), top, Justification::centredRight);
        g.setColour (col::accent);
        g.drawText ("L", Rectangle<float> (plot.getRight() - 12.0f, plot.getY(), 12.0f, 12.0f), Justification::centred);
        g.setColour (col::info);
        g.drawText ("R", Rectangle<float> (plot.getRight() - 12.0f, plot.getBottom() - 12.0f, 12.0f, 12.0f), Justification::centred);
    }
private:
    void timerCallback() override { repaint(); }
    SIEchoProcessor& proc;
};

//==============================================================================
//==============================================================================
/** Big H-Delay style readout: delay in ms + note value + tempo. */
class DelayReadout : public Component, private Timer
{
public:
    explicit DelayReadout (SIEchoProcessor& p) : proc (p) { startTimerHz (10); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.38f));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        const auto p = proc.readParams();
        const bool sync = pv (proc.apvts, "sync") > 0.5f;
        g.setColour (col::text);
        g.setFont (si::font (30.0f, Weight::ExtraBold, -0.01f));
        g.drawText (String (p.timeL * 1000.0, p.timeL < 1.0 ? 1 : 0) + " ms", r.reduced (16.0f, 0.0f), Justification::centredLeft);
        g.setColour (sync ? col::accentHi : col::label);
        g.setFont (si::font (13.0f, Weight::Bold));
        const String sub = sync ? SIEchoProcessor::noteNames()[(int) pv (proc.apvts, "noteL")] + "  -  " + String (proc.bpm.load(), 1) + " BPM"
                                : String ("FREE  -  ") + String (proc.bpm.load(), 1) + " BPM";
        g.drawText (sub, r.reduced (16.0f, 0.0f), Justification::centredRight);
    }
private:
    void timerCallback() override { repaint(); }
    SIEchoProcessor& proc;
};

//==============================================================================
class Root : public si::ScaledContent, private Timer
{
public:
    explicit Root (SIEchoProcessor& p) : si::ScaledContent (W, H), proc (p), s (p.apvts), presetBar (p.presets), view (p), readout (p)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &view, &readout, &timeNote, &timeMs, &feedback, &mix, &output,
                                                            &sync, &tap, &pingPong, &lofi, &analog, &modDepth, &modRate,
                                                            &lowcut, &highcut, &drive, &phaseL, &phaseR,
                                                            &style, &patternBox, &modeBox, &freeze, &timeRNote, &timeRMs, &ducking, &width, &diffusion, &sc })
            addAndMakeVisible (c);
        freeze.getProperties().set ("warn", true);
        si::setIcon (freeze, si::Icon::Freeze);
        si::setIcon (sync, si::Icon::Sync);
        analog.keepCase();
        pingPong.setClickingTogglesState (true);
        pingPong.onClick = [this] { setParam ("mode", pingPong.getToggleState() ? 2.0f : 0.0f); };
        tap.onClick = [this] { tapTempo(); };
        tap.setTooltip ("Tap at least twice to set the delay time");
        ducking.slider.setTooltip ("Ducking: echoes get quieter while you sing and come back in the gaps");
        feedback.slider.setTooltip ("Over 100% the echoes build up (saturation keeps them in check)");
        analog.setTooltip ("Analog models: 1 warm, 2 chorused BBD, 3 dark BBD, 4 tape echo. Off = the Character style in Extras");
        style.setTooltip ("Character used when Analog is Off");
        sc.setTooltip ("External sidechain: Ducking listens to the Sidechain input (in Ableton pick a track in the plugin's Sidechain panel)");
        p.presets.onChange = [this] { presetBar.refresh(); };
        resized();
        timerCallback();
        startTimerHz (10);
    }
    ~Root() override { proc.presets.onChange = nullptr; }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 270, 44 }, "SI Echo", "Analog Delay");
        si::drawTempoChip (g, chip().toFloat(), proc.bpm.load(), proc.hostBpm.load() ? "Host sync" : "No host", proc.hostBpm.load() ? col::accent : col::hint, -1.0f);
        si::drawPanel (g, { 24, 80, 892, 250 }, {}, true);
        si::drawPanel (g, { 24, 342, 290, 214 }, "Analog");
        si::drawPanel (g, { 326, 342, 280, 214 }, "Filters");
        si::drawPanel (g, { 618, 342, 298, 214 }, "Extras");
        static const char* model[5] = { "Off - clean (or the Character in Extras)", "1 - warm analog", "2 - chorused bucket-brigade", "3 - dark bucket-brigade", "4 - tape echo" };
        g.setColour (col::label);
        g.setFont (si::font (10.5f));
        g.drawText (model[jlimit (0, 4, (int) pv (s, "analog"))], Rectangle<int> (40, 410, 258, 16), Justification::centredLeft);
    }

    void resized() override
    {
        presetBar.setBounds (318, 20, 300, 38);
        sc.setBounds (630, 24, 66, 30);
        view.setBounds (40, 96, 420, 218);
        readout.setBounds (476, 96, 420, 56);
        Rectangle<int> k (476, 160, 420, 124);
        timeNote.setBounds (k.getX(), k.getY(), 105, 124);
        timeMs.setBounds (k.getX(), k.getY(), 105, 124);
        k.removeFromLeft (105);
        for (auto* kn : { &feedback, &mix, &output }) kn->setBounds (k.removeFromLeft (105));
        Rectangle<int> b (476, 292, 420, 28);
        for (auto* bt : std::initializer_list<Component*> { &sync, &tap, &pingPong, &lofi }) { bt->setBounds (b.removeFromLeft (99)); b.removeFromLeft (8); }
        analog.setBounds (40, 374, 258, 30);
        Rectangle<int> m (40, 434, 258, 112);
        modDepth.setBounds (m.removeFromLeft (129).reduced (12, 0));
        modRate.setBounds (m.reduced (12, 0));
        Rectangle<int> f (342, 374, 248, 100);
        for (auto* kn : { &lowcut, &highcut, &drive }) kn->setBounds (f.removeFromLeft (82).reduced (2, 0));
        phaseL.setBounds (342, 498, 120, 30);
        phaseR.setBounds (470, 498, 120, 30);
        style.setBounds (634, 374, 266, 28);
        patternBox.setBounds (634, 374, 266, 28);
        modeBox.setBounds (634, 408, 180, 28);
        freeze.setBounds (820, 408, 80, 28);
        Rectangle<int> e (634, 446, 266, 96);
        timeRNote.setBounds (e.getX(), e.getY(), 66, 96);
        timeRMs.setBounds (e.getX(), e.getY(), 66, 96);
        e.removeFromLeft (66);
        for (auto* kn : { &ducking, &width, &diffusion }) kn->setBounds (e.removeFromLeft (66));
    }

private:
    Rectangle<int> chip() const { return { 782, 20, 134, 38 }; }
    void setParam (const char* id, float real)
    {
        if (auto* p = s.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (real)); p->endChangeGesture(); }
    }
    void tapTempo()
    {
        const double now = Time::getMillisecondCounterHiRes();
        if (now - lastTap < 2000.0)
        {
            taps.add (now - lastTap);
            if (taps.size() > 4) taps.remove (0);
            double avg = 0;
            for (auto t : taps) avg += t;
            avg /= taps.size();
            setParam ("sync", 0.0f);
            setParam ("msL", (float) jlimit (1.0, 4000.0, avg));
        }
        else taps.clear();
        lastTap = now;
    }
    void timerCallback() override
    {
        const bool syncOn = pv (s, "sync") > 0.5f;
        const int mode = (int) pv (s, "mode");
        timeNote.setVisible (syncOn);
        timeMs.setVisible (! syncOn);
        timeRNote.setVisible (syncOn);
        timeRMs.setVisible (! syncOn);
        for (Component* c : std::initializer_list<Component*> { &timeRNote, &timeRMs }) { c->setEnabled (mode == 1); c->setAlpha (mode == 1 ? 1.0f : 0.35f); }
        style.setVisible (mode != 3);
        patternBox.setVisible (mode == 3);
        const bool analogOn = pv (s, "analog") > 0.5f;
        style.setEnabled (! analogOn);
        style.setAlpha (analogOn ? 0.4f : 1.0f);
        pingPong.setToggleState (mode == 2, dontSendNotification);
        repaint (chip());
        repaint (40, 410, 258, 16);
    }

    SIEchoProcessor& proc;
    si::APVTS& s;
    si::PresetBar presetBar;
    EchoView view;
    DelayReadout readout;
    si::Knob timeNote { s, "noteL", "Delay" }, timeMs { s, "msL", "Delay" }, feedback { s, "feedback", "Feedback" }, mix { s, "mix", "Dry / Wet" },
        output { s, "output", "Output", true };
    si::Toggle sync { s, "sync", "Sync" }, lofi { s, "lofi", "LoFi" };
    TextButton tap { "Tap" }, pingPong { "Ping-Pong" };
    si::Segmented analog { s, "analog", { "Off", "1", "2", "3", "4" } };
    si::Knob modDepth { s, "modDepth", "Mod Depth" }, modRate { s, "modRate", "Mod Rate" };
    si::Knob lowcut { s, "lowcut", "HiPass" }, highcut { s, "highcut", "LoPass" }, drive { s, "drive", "Saturation" };
    si::Toggle phaseL { s, "phaseL", "Phase L" }, phaseR { s, "phaseR", "Phase R" };
    si::Choice style { s, "style" }, patternBox { s, "pattern" }, modeBox { s, "mode" };
    si::Toggle freeze { s, "freeze", "Freeze" };
    si::Toggle sc { s, "extsc", "SC" };
    si::Knob timeRNote { s, "noteR", "Time R" }, timeRMs { s, "msR", "Time R" }, ducking { s, "ducking", "Ducking" }, width { s, "width", "Width" },
        diffusion { s, "diffusion", "Diffusion" };
    double lastTap = 0.0;
    Array<double> taps;
};
} // namespace

SIEchoEditor::SIEchoEditor (SIEchoProcessor& p) : AudioProcessorEditor (p)
{
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (p);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 3 / 2, H * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    if (SystemStats::getEnvironmentVariable ("SI_UI_SELFTEST", {}).isNotEmpty())
    {
        p.presets.load (2);
        si::uiSelfTest (*root, "SI Echo");
    }
}

SIEchoEditor::~SIEchoEditor() { setLookAndFeel (nullptr); }

void SIEchoEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
