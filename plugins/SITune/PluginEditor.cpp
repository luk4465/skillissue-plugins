#include "PluginEditor.h"

using namespace juce;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 900, H = 600;
const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

String noteName (float midi)
{
    if (midi < 0.0f) return "-";
    const int n = (int) std::round (midi);
    return String (kNoteNames[((n % 12) + 12) % 12]) + String (n / 12 - 1);
}

void setParam (si::APVTS& s, const String& id, float v)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

//==============================================================================
/** The orb: a particle sphere that spins and swirls with the voice; ring = cents gauge. */
class Orb : public Component, private Timer
{
public:
    explicit Orb (SITuneProcessor& p) : proc (p)
    {
        const int n = (int) pts.size();
        const float golden = MathConstants<float>::pi * (3.0f - std::sqrt (5.0f));
        for (int i = 0; i < n; ++i)
        {
            const float y = 1.0f - 2.0f * ((float) i + 0.5f) / (float) n;
            const float rr = std::sqrt (1.0f - y * y);
            const float th = golden * (float) i;
            pts[(size_t) i] = { std::cos (th) * rr, y, std::sin (th) * rr };
        }
        startTimerHz (60);
    }

    void paint (Graphics& g) override
    {
        auto& e = proc.engine;
        const auto c = getLocalBounds().toFloat().getCentre();
        const float R = jmin (getWidth(), getHeight()) * 0.34f;
        const float in = e.inMidi.load(), tgt = e.targetMidi.load();
        const bool voiced = in > 0.0f;
        const float cents = voiced && tgt > 0.0f ? (in - tgt) * 100.0f : 0.0f;
        const float lv = levelSm;

        // aura
        ColourGradient aura (col::accent.withAlpha (0.10f + 0.25f * lv), c.x, c.y, col::accent2.withAlpha (0.0f), c.x + R * 1.9f, c.y, true);
        g.setGradientFill (aura);
        g.fillEllipse (Rectangle<float> (R * 3.8f, R * 3.8f).withCentre (c));

        // outer gauge ring
        const float ringR = R * 1.30f;
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawEllipse (Rectangle<float> (ringR * 2, ringR * 2).withCentre (c), 1.5f);
        for (int i = -10; i <= 10; ++i)
        {
            const float a = (float) i / 10.0f * 2.1f;
            const float r0 = ringR + (i % 5 == 0 ? 6.0f : 3.0f), r1 = ringR + 10.0f;
            g.setColour (Colours::white.withAlpha (i == 0 ? 0.5f : (i % 5 == 0 ? 0.22f : 0.1f)));
            g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, i == 0 ? 2.0f : 1.0f);
        }
        if (voiced)
        {
            const float a = jlimit (-2.1f, 2.1f, centsSm / 50.0f * 2.1f);
            Path arc;
            arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, jmin (0.0f, a), jmax (0.0f, a), true);
            const auto ac = std::abs (centsSm) < 10.0f ? col::accent : (std::abs (centsSm) < 25.0f ? col::accentHi : col::warn);
            g.setColour (ac.withAlpha (0.25f));
            g.strokePath (arc, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (ac);
            g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            const Point<float> tip (c.x + std::sin (a) * ringR, c.y - std::cos (a) * ringR);
            g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (tip));
        }

        // pulse ring
        const float pr = R * (1.10f + 0.04f * lv);
        g.setColour (col::accent2.withAlpha (0.12f + 0.45f * lv));
        g.drawEllipse (Rectangle<float> (pr * 2, pr * 2).withCentre (c), 2.0f + 4.0f * lv);

        // glass body
        auto body = Rectangle<float> (R * 2, R * 2).withCentre (c);
        g.setGradientFill (ColourGradient (Colour (0xff1c2129), c.x - R * 0.4f, c.y - R * 0.5f, Colour (0xff07080b), c.x + R, c.y + R, true));
        g.fillEllipse (body);

        // particles
        const float cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (tilt), sp = std::sin (tilt);
        for (auto& pt : pts)
        {
            // swirl: longitude twist grows with latitude while singing
            const float tw = twist * pt.y;
            const float ct = std::cos (tw), st = std::sin (tw);
            float x = pt.x * ct - pt.z * st, z = pt.x * st + pt.z * ct, y = pt.y;
            const float wob = 1.0f + 0.06f * lv * std::sin (phase * 3.0f + y * 6.0f);
            float x1 = x * cy - z * sy, z1 = x * sy + z * cy;
            float y1 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
            const float depth = (z2 + 1.0f) * 0.5f;
            const float px = c.x + x1 * R * 0.93f * wob, py = c.y + y1 * R * 0.93f * wob;
            const float al = (0.06f + 0.9f * depth * depth) * (0.35f + 0.65f * jmax (0.25f, lv));
            const auto colr = col::accent.interpolatedWith (col::accent2, (y1 + 1.0f) * 0.5f).interpolatedWith (col::info, jmin (1.0f, std::abs (centsSm) / 60.0f) * 0.6f);
            g.setColour (colr.withAlpha (al));
            const float s = 0.8f + 2.0f * depth;
            g.fillEllipse (px - s * 0.5f, py - s * 0.5f, s, s);
        }
        // glass highlight
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), c.x - R * 0.35f, c.y - R * 0.75f, Colours::white.withAlpha (0.0f), c.x, c.y - R * 0.1f, true));
        g.fillEllipse (body.reduced (R * 0.08f));
        g.setColour (Colours::white.withAlpha (0.12f));
        g.drawEllipse (body, 1.0f);

        // note readout
        if (voiced)
        {
            g.setColour (col::text);
            g.setFont (si::font (R * 0.34f, Weight::SemiBold));
            g.drawText (noteName (tgt > 0 ? tgt : in), Rectangle<float> (R * 2.0f, R * 0.5f).withCentre (c.translated (0, -R * 0.06f)), Justification::centred);
            g.setColour (col::label);
            g.setFont (si::font (11.0f, Weight::Medium, 0.1f));
            g.drawText ((cents > 0 ? "+" : "") + String (roundToInt (cents)) + " CT", Rectangle<float> (R * 2.0f, 16.0f).withCentre (c.translated (0, R * 0.26f)), Justification::centred);
        }
    }

private:
    void timerCallback() override
    {
        const float dt = 1.0f / 60.0f;
        auto& e = proc.engine;
        const float lvRaw = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (e.level.load(), -60.0f) + 60.0f) / 50.0f);
        levelSm += (lvRaw - levelSm) * (lvRaw > levelSm ? 0.35f : 0.06f);
        const float in = e.inMidi.load(), tgt = e.targetMidi.load();
        const float cents = in > 0 && tgt > 0 ? (in - tgt) * 100.0f : 0.0f;
        centsSm += (cents - centsSm) * 0.25f;
        phase += dt;
        yaw += dt * (0.25f + 3.2f * levelSm + 0.02f * std::abs (centsSm));
        tilt = 0.35f + 0.18f * std::sin (phase * 0.4f);
        twist += ((1.8f * levelSm * std::sin (phase * 0.9f)) - twist) * 0.05f;
        repaint();
    }

    struct P3 { float x, y, z; };
    SITuneProcessor& proc;
    std::array<P3, 900> pts {};
    float yaw = 0, tilt = 0.35f, twist = 0, phase = 0, levelSm = 0, centsSm = 0;
};

//==============================================================================
/** Pitch history: input (grey) vs corrected (lime) over the last ~1.3 s, on a semitone grid. */
class History : public Component, private Timer
{
public:
    explicit History (SITuneProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        auto plot = r.reduced (10.0f, 10.0f);
        auto& e = proc.engine;
        const int mask = proc.currentMask();
        const float t = e.targetMidi.load();
        if (t > 0.0f) centre += (t - centre) * 0.1f;
        auto my = [&] (float m) { return plot.getCentreY() - (m - centre) / 7.0f * plot.getHeight() * 0.5f; };
        g.setFont (si::font (8.5f, Weight::Medium));
        for (int n = (int) centre - 7; n <= (int) centre + 7; ++n)
        {
            const float y = my ((float) n);
            if (y < plot.getY() || y > plot.getBottom()) continue;
            const bool on = (mask >> (((n % 12) + 12) % 12)) & 1;
            g.setColour (Colours::white.withAlpha (on ? 0.09f : 0.03f));
            g.fillRect (plot.getX() + 26.0f, y, plot.getWidth() - 26.0f, 1.0f);
            if (on)
            {
                g.setColour (col::hint);
                g.drawText (noteName ((float) n), Rectangle<float> (plot.getX(), y - 6.0f, 26.0f, 12.0f), Justification::left);
            }
        }
        auto line = [&] (std::array<std::atomic<float>, si::tune::TuneEngine::kHist>& h, Colour c, float width)
        {
            const int pos = e.histPos.load();
            Path p;
            bool open = false;
            const int N = si::tune::TuneEngine::kHist;
            for (int i = 0; i < N; ++i)
            {
                const float m = h[(size_t) ((pos + i) % N)].load();
                const float x = plot.getX() + 26.0f + (plot.getWidth() - 26.0f) * (float) i / (float) (N - 1);
                if (m <= 0.0f) { open = false; continue; }
                const float y = jlimit (plot.getY(), plot.getBottom(), my (m));
                if (! open) { p.startNewSubPath (x, y); open = true; }
                else p.lineTo (x, y);
            }
            g.setColour (c.withAlpha (0.25f));
            g.strokePath (p, PathStrokeType (width * 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (c);
            g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
        };
        line (e.histIn, Colours::white.withAlpha (0.4f), 1.2f);
        line (e.histOut, col::accent, 1.8f);
        g.setColour (col::label);
        g.setFont (si::font (8.5f, Weight::SemiBold, 0.14f));
        g.drawText ("INPUT", plot.withHeight (10.0f), Justification::topRight);
        g.setColour (col::accent);
        g.drawText ("TUNED", plot.withHeight (10.0f).translated (0, 11), Justification::topRight);
    }
private:
    void timerCallback() override { repaint(); }
    SITuneProcessor& proc;
    float centre = 60.0f;
};

//==============================================================================
/** One octave: note tiles (allowed notes, click to toggle) + piano with the sung note lit. */
class Keyboard : public Component, private Timer
{
public:
    explicit Keyboard (SITuneProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const int mask = proc.currentMask();
        const float in = proc.engine.inMidi.load(), tgt = proc.engine.targetMidi.load();
        const int sung = in > 0 ? ((int) std::round (in) % 12 + 12) % 12 : -1;
        const int target = tgt > 0 ? ((int) std::round (tgt) % 12 + 12) % 12 : -1;
        auto tiles = r.removeFromTop (30.0f);
        r.removeFromTop (6.0f);
        const float tw = tiles.getWidth() / 12.0f;
        for (int i = 0; i < 12; ++i)
        {
            auto t = Rectangle<float> (tiles.getX() + tw * (float) i, tiles.getY(), tw, tiles.getHeight()).reduced (2.0f, 0.0f);
            const bool on = (mask >> i) & 1;
            if (on) g.setGradientFill (si::accentGradient (t.getTopLeft(), t.getBottomRight(), i == hover ? 1.0f : 0.85f));
            else g.setColour (Colours::white.withAlpha (i == hover ? 0.10f : 0.05f));
            g.fillRoundedRectangle (t, 7.0f);
            g.setColour (on ? col::onAccent : col::label);
            g.setFont (si::font (11.0f, Weight::SemiBold));
            g.drawText (kNoteNames[i], t, Justification::centred);
        }
        // piano
        const int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
        const float ww = r.getWidth() / 7.0f;
        for (int k = 0; k < 7; ++k)
        {
            auto kr = Rectangle<float> (r.getX() + ww * (float) k, r.getY(), ww, r.getHeight()).reduced (1.5f, 0.0f);
            const int pc = whites[k];
            g.setColour (Colour (0xffd9dde4).withAlpha ((mask >> pc) & 1 ? 0.92f : 0.35f));
            g.fillRoundedRectangle (kr, 5.0f);
            if (pc == target) { g.setColour (col::accent.withAlpha (0.9f)); g.fillRoundedRectangle (kr.removeFromBottom (kr.getHeight() * 0.35f), 5.0f); }
            if (pc == sung) { g.setColour (col::accent2.withAlpha (0.45f)); g.fillRoundedRectangle (kr, 5.0f); }
        }
        const int blacks[] = { 1, 3, -1, 6, 8, 10 };
        for (int k = 0; k < 6; ++k)
        {
            if (blacks[k] < 0) continue;
            const int pc = blacks[k];
            auto kr = Rectangle<float> (r.getX() + ww * ((float) k + 0.68f), r.getY(), ww * 0.64f, r.getHeight() * 0.6f);
            g.setColour (Colour (0xff15171c));
            g.fillRoundedRectangle (kr, 4.0f);
            g.setColour ((mask >> pc) & 1 ? Colour (0xff3a3f4a) : Colour (0xff1f2229));
            g.fillRoundedRectangle (kr.reduced (2.0f), 3.0f);
            if (pc == target) { g.setColour (col::accent); g.fillRoundedRectangle (kr.reduced (3.0f).removeFromBottom (kr.getHeight() * 0.3f), 3.0f); }
            if (pc == sung) { g.setColour (col::accent2.withAlpha (0.6f)); g.fillRoundedRectangle (kr.reduced (2.0f), 3.0f); }
        }
    }
    void mouseMove (const MouseEvent& e) override { const int h = e.y < 30 ? (int) (e.x / (getWidth() / 12.0f)) : -1; if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (e.y > 30) return;
        const int i = jlimit (0, 11, (int) (e.x / (getWidth() / 12.0f)));
        auto& s = proc.apvts;
        const int mask = proc.currentMask() ^ (1 << i);
        for (int k = 0; k < 12; ++k) setParam (s, "n" + String (k), (mask >> k) & 1 ? 1.0f : 0.0f);
        setParam (s, "scale", (float) (SITuneProcessor::scaleNames().size() - 1));   // Custom
        repaint();
    }
private:
    void timerCallback() override { repaint(); }
    SITuneProcessor& proc;
    int hover = -1;
};

//==============================================================================
class Root : public si::ScaledContent, private Timer
{
public:
    explicit Root (SITuneProcessor& p) : si::ScaledContent (W, H), proc (p), s (p.apvts), presetBar (p.presets), orb (p), history (p), keys (p)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &lowLat, &orb, &speed, &flex, &natvib, &amount, &humanize, &history, &inputType, &tracking, &key, &scale,
                                                            &keys, &transpose, &ref })
            addAndMakeVisible (c);

        flex.slider.setTooltip ("Flex Tune: only notes that are already close to the target get corrected - slides and expressive moves stay natural. 0 = correct everything.");
        natvib.slider.setTooltip ("Natural Vibrato: make the singer's own vibrato smaller (-) or bigger (+) without touching the tuning.");
        tracking.slider.setTooltip ("Tracking: lower = strict (clean vocals), higher = relaxed (breathy or noisy vocals).");
        inputType.setTooltip ("Pick the voice range so detection locks fast and never jumps an octave.");
        lowLat.setTooltip ("Zero Latency: reports 0 samples, reads right behind the input - track through headphones with it. Off = Quality mode (7.5 ms, smoother).");
        speed.slider.setTooltip ("How fast notes snap to pitch. Hard (0) = robotic T-Pain effect, 20-60 ms = natural.");
        humanize.slider.setTooltip ("Lets small natural movements (vibrato, scoops) through.");

        p.presets.onChange = [this] { presetBar.refresh(); };
        resized();
        startTimerHz (10);
    }
    ~Root() override { proc.presets.onChange = nullptr; }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 260, 44 }, "SI Tune", "Real-Time Pitch Correction");
        // latency chip
        auto chip = Rectangle<float> (622.0f, 20.0f, 126.0f, 38.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (chip, 19.0f);
        g.setColour (Colours::white.withAlpha (0.09f));
        g.drawRoundedRectangle (chip.reduced (0.5f), 19.0f, 1.0f);
        g.setColour (col::label);
        g.setFont (si::font (8.5f, Weight::SemiBold, 0.16f));
        g.drawText ("LATENCY", Rectangle<float> (chip.getX(), chip.getY() + 5.0f, chip.getWidth(), 10.0f), Justification::centred);
        g.setColour (col::accentHi);
        g.setFont (si::font (13.0f, Weight::SemiBold));
        g.drawText (String (proc.getLatencySamples() / jmax (1.0, proc.getRate()) * 1000.0, 1) + " ms", Rectangle<float> (chip.getX(), chip.getY() + 16.0f, chip.getWidth(), 18.0f), Justification::centred);

        si::drawPanel (g, { 24, 80, 872, 320 }, {}, true);
        si::drawPanel (g, { 24, 412, 872, 176 }, "Scale & Notes");
    }

    void resized() override
    {
        presetBar.setBounds (300, 20, 306, 38);
        lowLat.setBounds (760, 22, 116, 34);
        orb.setBounds (40, 92, 420, 296);
        speed.setBounds (480, 92, 132, 140);
        flex.setBounds (616, 100, 130, 132);
        natvib.setBounds (750, 100, 130, 132);
        amount.setBounds (480, 244, 100, 128);
        humanize.setBounds (584, 244, 100, 128);
        history.setBounds (692, 240, 188, 148);
        inputType.setBounds (40, 448, 170, 30);
        key.setBounds (218, 448, 130, 30);
        scale.setBounds (356, 448, 244, 30);
        keys.setBounds (40, 488, 560, 88);
        transpose.setBounds (612, 446, 89, 128);
        tracking.setBounds (701, 446, 89, 128);
        ref.setBounds (790, 446, 89, 128);

    }

private:
    void timerCallback() override { repaint (622, 20, 126, 38); }

    SITuneProcessor& proc;
    si::APVTS& s;
    si::PresetBar presetBar;
    si::Toggle lowLat { s, "lowlat", "Zero Latency" };
    Orb orb;
    si::Knob speed { s, "speed", "Speed" }, amount { s, "amount", "Amount" }, humanize { s, "humanize", "Humanize" };
    History history;
    si::Choice key { s, "key" }, scale { s, "scale" }, inputType { s, "inputType" };
    si::Knob flex { s, "flex", "Flex Tune" }, natvib { s, "natvib", "Natural Vibrato", true }, tracking { s, "tracking", "Tracking" };

    Keyboard keys;
    si::Knob transpose { s, "transpose", "Transpose", true }, ref { s, "ref", "Pitch Ref" };
};
} // namespace

//==============================================================================
SITuneEditor::SITuneEditor (SITuneProcessor& p) : AudioProcessorEditor (p)
{
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (p);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 3 / 2, H * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    si::uiSelfTest (*root, "SI Tune");
}

SITuneEditor::~SITuneEditor() { setLookAndFeel (nullptr); }

void SITuneEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
