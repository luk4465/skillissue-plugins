#include "PluginEditor.h"
#include "Sidechain.h"
#include "Tempo.h"

using namespace juce;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 900, H = 620;

void setParam (si::APVTS& s, const String& id, float realValue)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

float pv (si::APVTS& s, const char* id) { return s.getRawParameterValue (id)->load(); }

void drawInset (Graphics& g, Rectangle<float> r, float rad = 14.0f)
{
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, rad);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.35f), 0, r.getY(), Colours::transparentBlack, 0, r.getY() + 14.0f, false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

void drawMiniChip (Graphics& g, Rectangle<float> r, const String& k, const String& v, bool lit)
{
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    g.setColour (col::label);
    g.setFont (si::font (8.5f, Weight::SemiBold, 0.16f));
    const float kw = GlyphArrangement::getStringWidth (g.getCurrentFont(), k);
    g.drawText (k, r.withTrimmedLeft (10.0f), Justification::centredLeft);
    g.setColour (lit ? col::accentHi : col::text);
    g.setFont (si::font (11.0f, Weight::Medium));
    g.drawText (v, r.withTrimmedLeft (16.0f + kw), Justification::centredLeft);
}

//==============================================================================
/** Animated "space": rings emitted by the input travel outwards at a speed set by Size and fade with RT60;
    a particle cloud breathes with the wet level; the envelope + beat grid runs along the bottom. */
class SpaceView : public Component, private Timer
{
public:
    explicit SpaceView (SIReverbProcessor& p) : proc (p)
    {
        Random r (5);
        for (auto& d : dots) d = { r.nextFloat() * MathConstants<float>::twoPi, 0.25f + 0.75f * std::sqrt (r.nextFloat()), 0.4f + r.nextFloat(), r.nextFloat() };
        startTimerHz (60);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawInset (g, r);
        Graphics::ScopedSaveState ss (g);
        Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), 14.0f);
        g.reduceClipRegion (clip);

        auto& s = proc.apvts;
        const float size = pv (s, "size") * 0.01f;
        const float rt = proc.effDecayS.load();
        const bool freeze = pv (s, "freeze") > 0.5f;
        const float wet = jlimit (0.0f, 1.0f, std::sqrt (proc.wetEnv.load()) * 1.2f);
        const auto c = Point<float> (r.getCentreX(), r.getY() + (r.getHeight() - 56.0f) * 0.55f);
        const float maxR = r.getWidth() * (0.22f + 0.30f * size);

        // grid
        g.setColour (Colours::white.withAlpha (0.055f));
        for (int i = 1; i <= 4; ++i)
        {
            const float rr = maxR * (float) i / 4.0f;
            g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 1.1f).withCentre (c), 1.0f);
        }
        for (int i = 0; i < 12; ++i)
        {
            const float a = MathConstants<float>::twoPi * (float) i / 12.0f;
            g.drawLine (c.x, c.y, c.x + std::cos (a) * maxR, c.y + std::sin (a) * maxR * 0.55f, 1.0f);
        }

        // core glow
        {
            const float gr = 30.0f + 70.0f * wet;
            ColourGradient cg (col::accent.withAlpha (0.10f + 0.35f * wet), c.x, c.y, col::accent.withAlpha (0.0f), c.x + gr * 2.2f, c.y, true);
            g.setGradientFill (cg);
            g.fillEllipse (Rectangle<float> (gr * 4.4f, gr * 2.6f).withCentre (c));
        }

        // rings
        for (auto& ring : rings)
        {
            if (ring.life <= 0.0f) continue;
            const float t = ring.age / ring.life;
            const float rr = maxR * (0.06f + 0.94f * (1.0f - std::pow (1.0f - t, 2.2f)));
            const float a = ring.energy * std::pow (1.0f - t, 1.6f);
            const auto tint = freeze ? col::info : col::accent.interpolatedWith (col::accent2, t);
            g.setColour (tint.withAlpha (a * 0.16f));
            g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 1.1f).withCentre (c), 6.0f);
            g.setColour (tint.withAlpha (a * 0.85f));
            g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 1.1f).withCentre (c), 1.6f);
        }

        // particles
        const float mod = pv (s, "moddepth") * 0.01f;
        for (auto& d : dots)
        {
            const float wob = 1.0f + 0.08f * mod * std::sin (phase * (0.6f + d.speed) * 3.0f + d.seed * 10.0f);
            const float rr = maxR * d.rad * wob;
            const float a = d.ang + phase * 0.12f * d.speed;
            const auto p = Point<float> (c.x + std::cos (a) * rr, c.y + std::sin (a) * rr * 0.55f);
            const float al = (0.28f + 0.65f * wet) * (0.4f + 0.6f * d.seed);
            g.setColour ((d.seed > 0.5f ? col::accent : col::accent2).withAlpha (al));
            const float ds = 1.4f + 1.6f * d.seed;
            g.fillEllipse (Rectangle<float> (ds, ds).withCentre (p));
        }

        // envelope strip
        auto strip = r.removeFromBottom (56.0f).reduced (14.0f, 8.0f);
        g.setGradientFill (ColourGradient (Colours::transparentBlack, 0, strip.getY() - 20.0f, Colours::black.withAlpha (0.45f), 0, strip.getY() + 10.0f, false));
        g.fillRect (getLocalBounds().toFloat().withTop (strip.getY() - 20.0f));
        drawEnvelope (g, strip);

        // chips
        auto top = getLocalBounds().toFloat().reduced (12.0f, 12.0f).removeFromTop (24.0f);
        drawMiniChip (g, top.removeFromLeft (100.0f), "PRE", si::tempo::formatMs (proc.effPreMs.load()), proc.effPreIdx.load() > 0);
        top.removeFromLeft (6.0f);
        drawMiniChip (g, top.removeFromLeft (104.0f), "RT60", si::tempo::formatMs (rt * 1000.0), proc.effDecayIdx.load() > 0);
        if (freeze)
        {
            g.setColour (col::info);
            g.setFont (si::font (9.0f, Weight::SemiBold, 0.2f));
            g.drawText ("FROZEN", top.removeFromRight (70.0f), Justification::centredRight);
        }

        // wet meter
        auto m = getLocalBounds().toFloat().reduced (12.0f, 14.0f).removeFromRight (4.0f).withTrimmedBottom (56.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (m, 2.0f);
        const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (proc.wetEnv.load(), -60.0f) + 60.0f) / 60.0f);
        auto mf = m.withTrimmedTop (m.getHeight() * (1.0f - lvl));
        g.setGradientFill (si::accentGradient (mf.getBottomLeft(), m.getTopLeft()));
        g.fillRoundedRectangle (mf, 2.0f);
    }

private:
    void drawEnvelope (Graphics& g, Rectangle<float> plot)
    {
        auto& s = proc.apvts;
        const float pre = proc.effPreMs.load() * 0.001f;
        const float rt = proc.effDecayS.load();
        const float bpm = proc.bpm.load();
        const float size = pv (s, "size") * 0.01f;
        const float att = pv (s, "attack") * 0.01f;
        const bool nonlin = si::dsp::getMode ((int) pv (s, "mode")).nonlin;
        const float attT = 0.004f + att * 0.28f * (0.4f + 0.6f * size);
        const float total = jlimit (0.4f, 20.0f, pre + attT + (nonlin ? jmin (rt, 1.5f) * 1.3f : rt * 1.08f));
        auto tx = [&] (float t) { return plot.getX() + plot.getWidth() * (t / total); };
        auto dy = [&] (float db) { return plot.getBottom() - plot.getHeight() * (1.0f + jlimit (-60.0f, 0.0f, db) / 60.0f); };

        const float beat = 60.0f / jmax (20.0f, bpm), bar = beat * 4.0f;
        const float stepT = total / beat > 48.0f ? bar : beat;
        int k = 0;
        for (float t = stepT; t <= total; t += stepT, ++k)
        {
            const bool isBar = stepT == bar || (k + 1) % 4 == 0;
            g.setColour (Colours::white.withAlpha (isBar ? 0.14f : 0.05f));
            g.fillRect (tx (t), plot.getY(), 1.0f, plot.getHeight());
        }

        Path env;
        env.startNewSubPath (plot.getX(), plot.getBottom());
        env.lineTo (tx (pre), plot.getBottom());
        if (nonlin)
        {
            const float gate = jmin (rt, 1.5f);
            const float startDb = att < 0.5f ? 0.0f : -18.0f * (att - 0.5f) * 2.0f;
            const float endDb = att < 0.5f ? -14.0f * att * 2.0f : 0.0f;
            env.lineTo (tx (pre + 0.003f), dy (startDb));
            env.lineTo (tx (pre + gate), dy (endDb));
            env.lineTo (tx (pre + gate + 0.01f), plot.getBottom());
        }
        else
        {
            for (int i = 0; i <= 16; ++i)
                env.lineTo (tx (pre + attT * (float) i / 16.0f), dy (-60.0f + 60.0f * std::pow ((float) i / 16.0f, 0.35f)));
            const float endT = jmin (total, pre + attT + rt);
            env.lineTo (tx (endT), dy (-60.0f * (endT - pre - attT) / jmax (0.01f, rt)));
        }
        env.lineTo (plot.getRight(), plot.getBottom());
        Path fill (env);
        fill.closeSubPath();
        g.setGradientFill (ColourGradient (col::accent.withAlpha (0.30f), 0, plot.getY(), col::accent2.withAlpha (0.0f), 0, plot.getBottom(), false));
        g.fillPath (fill);
        g.setGradientFill (si::accentGradient (plot.getTopLeft(), plot.getTopRight()));
        g.strokePath (env, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));

        // playhead sweep on each bar while the host plays
        if (proc.playing.load())
        {
            const float t = std::fmod ((float) proc.ppq.load() * beat, bar);
            if (t < total)
            {
                g.setColour (col::text.withAlpha (0.5f));
                g.fillRect (tx (t), plot.getY(), 1.0f, plot.getHeight());
            }
        }
    }

    void timerCallback() override
    {
        const float dt = 1.0f / 60.0f;
        phase += dt;
        const float env = proc.inEnv.load();
        const float rt = jlimit (0.25f, 6.0f, proc.effDecayS.load());
        const bool freeze = pv (proc.apvts, "freeze") > 0.5f;
        sinceEmit += dt;
        const bool transient = env > lastEnv * 1.35f + 0.01f;
        if (env > 0.02f && (transient ? sinceEmit > 0.06f : sinceEmit > 0.22f))
        {
            auto& slot = rings[(size_t) (nextRing++ % rings.size())];
            slot = { 0.0f, 0.5f + rt * 0.7f, jlimit (0.15f, 1.0f, std::sqrt (env) * 1.3f) };
            sinceEmit = 0.0f;
        }
        lastEnv = env;
        for (auto& r : rings)
            if (r.life > 0.0f)
            {
                r.age += freeze ? dt * 0.15f : dt;
                if (r.age >= r.life) r.life = 0.0f;
            }
        repaint();
    }

    struct Ring { float age = 0, life = 0, energy = 0; };
    struct Dot { float ang, rad, speed, seed; };
    SIReverbProcessor& proc;
    std::array<Ring, 24> rings {};
    std::array<Dot, 70> dots {};
    int nextRing = 0;
    float phase = 0.0f, lastEnv = 0.0f, sinceEmit = 1.0f;
};

//==============================================================================
/** Output EQ response (Lo/Hi Cut) + in-loop damping shelf. */
class EqView : public Component
{
public:
    explicit EqView (SIReverbProcessor& p) : proc (p) {}
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawInset (g, r, 12.0f);
        auto plot = r.reduced (10.0f, 12.0f);
        auto& s = proc.apvts;
        const float lc = pv (s, "lowcut"), hc = pv (s, "highcut"), hf = pv (s, "highfreq"), hs = pv (s, "highshelf");
        const float bx = pv (s, "bassxover"), bm = pv (s, "bassmult");
        auto fx = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
        auto dy = [&] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * jlimit (-1.0f, 1.0f, db / 24.0f); };

        g.setFont (si::font (8.0f, Weight::Medium));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (Colours::white.withAlpha (0.07f));
            g.fillRect (fx (f), plot.getY(), 1.0f, plot.getHeight());
            g.setColour (col::hint);
            g.drawText (f >= 1000.0f ? String ((int) (f / 1000)) + "k" : String ((int) f), Rectangle<float> (fx (f) + 3.0f, plot.getBottom() - 10.0f, 30.0f, 10.0f), Justification::left);
        }
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRect (plot.getX(), dy (0.0f), plot.getWidth(), 1.0f);

        Path resp, damp;
        for (int i = 0; i <= 120; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 120.0f);
            const float hp = (float) (1.0 / std::sqrt (1.0 + std::pow (lc / f, 4.0)));
            const float lp = (float) (1.0 / std::sqrt (1.0 + std::pow (f / hc, 4.0)));
            const float db = Decibels::gainToDecibels (hp * lp, -48.0f);
            const auto pt = Point<float> (fx (f), dy (db));
            i == 0 ? resp.startNewSubPath (pt) : resp.lineTo (pt);
            // damping: highs decay faster above Hi Freq, lows scale by Bass Mult
            float dd = f > hf ? hs * jmin (1.0f, std::log2 (f / hf) * 0.6f) : 0.0f;
            if (f < bx) dd += Decibels::gainToDecibels (bm) * 0.5f;
            const auto pd = Point<float> (fx (f), dy (dd));
            i == 0 ? damp.startNewSubPath (pd) : damp.lineTo (pd);
        }
        Path fill (resp);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (ColourGradient (col::accent.withAlpha (0.25f), 0, plot.getY(), col::accent2.withAlpha (0.0f), 0, plot.getBottom(), false));
        g.fillPath (fill);
        g.setGradientFill (si::accentGradient (plot.getTopLeft(), plot.getTopRight()));
        g.strokePath (resp, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col::info.withAlpha (0.7f));
        Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        PathStrokeType (1.2f).createDashedStroke (dashed, damp, dashes, 2);
        g.fillPath (dashed);
        g.setColour (col::label);
        g.setFont (si::font (8.5f, Weight::SemiBold, 0.12f));
        g.drawText ("OUTPUT", plot.withHeight (10.0f), Justification::topLeft);
        g.setColour (col::info.withAlpha (0.8f));
        g.drawText ("DAMPING", plot.withHeight (10.0f), Justification::topRight);
    }
private:
    SIReverbProcessor& proc;
};

//==============================================================================
/** Modulation preview: the LFO shape of the current mode at the current rate. */
class ModView : public Component, private Timer
{
public:
    explicit ModView (SIReverbProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawInset (g, r, 12.0f);
        auto plot = r.reduced (12.0f, 14.0f);
        auto& s = proc.apvts;
        const auto& md = si::dsp::getMode ((int) pv (s, "mode"));
        const float depth = pv (s, "moddepth") * 0.01f;
        const float rate = pv (s, "modrate");
        Path p;
        Random rnd (11);
        float rv[16];
        for (auto& v : rv) v = rnd.nextFloat() * 2.0f - 1.0f;
        for (int i = 0; i <= 160; ++i)
        {
            const float t = (float) i / 160.0f;
            const float ph = t * 2.0f + phase * jlimit (0.05f, 4.0f, rate) * 0.5f;
            float v;
            if (md.mod == si::dsp::ModType::Sine || md.mod == si::dsp::ModType::Chorus) v = std::sin (ph * MathConstants<float>::twoPi);
            else
            {
                const float fp = ph * 3.0f;
                const int a = (int) std::floor (fp);
                const float f = fp - (float) a;
                const float x0 = rv[(a % 16 + 16) % 16], x1 = rv[((a + 1) % 16 + 16) % 16];
                v = x0 + (x1 - x0) * (0.5f - 0.5f * std::cos (f * MathConstants<float>::pi));
                if (md.mod == si::dsp::ModType::Chaotic) v += 0.35f * std::sin (fp * 7.3f);
            }
            const auto pt = Point<float> (plot.getX() + plot.getWidth() * t, plot.getCentreY() - v * depth * plot.getHeight() * 0.45f);
            i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
        }
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
        g.setGradientFill (si::accentGradient (plot.getTopLeft(), plot.getTopRight(), 0.25f));
        g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setGradientFill (si::accentGradient (plot.getTopLeft(), plot.getTopRight()));
        g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        const char* names[] = { "SINE", "RANDOM", "CHORUS", "CHAOTIC" };
        g.setColour (col::label);
        g.setFont (si::font (8.5f, Weight::SemiBold, 0.14f));
        g.drawText (String (names[(int) md.mod]) + " LFO", plot.withHeight (10.0f), Justification::topLeft);
    }
private:
    void timerCallback() override { phase += 1.0f / 30.0f; repaint(); }
    SIReverbProcessor& proc;
    float phase = 0.0f;
};

//==============================================================================
/** Note grid: straight / dotted / triplet times for the current BPM. Click a cell to apply it. */
class TempoTable : public Component
{
public:
    explicit TempoTable (SIReverbProcessor& p) : proc (p) {}

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds();
        const auto& notes = si::tempo::tableNotes();
        const int rowH = r.getHeight() / (notes.size() + 1);
        const double bpm = proc.bpm.load();
        auto head = r.removeFromTop (rowH);
        const int cw = r.getWidth() / 4;
        const char* heads[] = { "NOTE", "STRAIGHT", "DOTTED", "TRIPLET" };
        for (int c = 0; c < 4; ++c)
        {
            g.setColour (col::hint);
            g.setFont (si::font (8.0f, Weight::SemiBold, 0.12f));
            g.drawText (heads[c], head.withX (head.getX() + c * cw).withWidth (cw).reduced (8, 0), c == 0 ? Justification::centredLeft : Justification::centredRight);
        }
        drawInset (g, r.toFloat(), 12.0f);
        const double mul[] = { 1.0, 1.5, 2.0 / 3.0 };
        for (int i = 0; i < notes.size(); ++i)
        {
            auto row = r.removeFromTop (rowH);
            if (i > 0)
            {
                g.setColour (Colours::white.withAlpha (0.045f));
                g.fillRect (row.getX() + 10, row.getY(), row.getWidth() - 20, 1);
            }
            g.setFont (si::font (11.0f, Weight::SemiBold));
            g.setColour (col::text);
            g.drawText (notes[i].name, row.withWidth (cw).reduced (10, 0), Justification::centredLeft);
            for (int c = 0; c < 3; ++c)
            {
                auto cell = row.withX (row.getX() + (c + 1) * cw).withWidth (cw);
                const bool h = hover.x == c && hover.y == i;
                if (h)
                {
                    g.setColour (col::accent.withAlpha (0.16f));
                    g.fillRoundedRectangle (cell.reduced (2, 3).toFloat(), 6.0f);
                }
                g.setColour (h ? col::accentHi : col::label);
                g.setFont (si::font (10.5f, h ? Weight::Medium : Weight::Regular));
                g.drawText (si::tempo::formatMs (si::tempo::beatsToMs (notes[i].beats * mul[c], bpm)), cell.reduced (8, 0), Justification::centredRight);
            }
        }
    }

    void mouseMove (const MouseEvent& e) override { setHover (cellAt (e.getPosition())); }
    void mouseExit (const MouseEvent&) override { setHover ({ -1, -1 }); }
    void mouseUp (const MouseEvent& e) override
    {
        const auto c = cellAt (e.getPosition());
        if (c.x < 0) return;
        const double mul[] = { 1.0, 1.5, 2.0 / 3.0 };
        const double ms = si::tempo::beatsToMs (si::tempo::tableNotes()[c.y].beats * mul[c.x], proc.bpm.load());
        PopupMenu m;
        m.addSectionHeader (si::tempo::formatMs (ms));
        m.addItem (1, "Set as Pre-Delay", ms <= 500.0);
        m.addItem (2, "Set as Decay", ms >= 200.0);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, ms] (int res)
        {
            auto& s = proc.apvts;
            if (res == 1) { setParam (s, "tempoAuto", 0.0f); setParam (s, "tempoPre", 0.0f); setParam (s, "predelay", (float) ms); }
            if (res == 2) { setParam (s, "tempoAuto", 0.0f); setParam (s, "tempoDecay", 0.0f); setParam (s, "decay", (float) ms * 0.001f); }
        });
    }

private:
    Point<int> cellAt (Point<int> pos) const
    {
        const int n = si::tempo::tableNotes().size();
        const int rowH = getHeight() / (n + 1);
        const int row = pos.y / rowH - 1;
        const int colI = pos.x / (getWidth() / 4) - 1;
        if (row < 0 || row >= n || colI < 0 || colI > 2) return { -1, -1 };
        return { colI, row };
    }
    void setHover (Point<int> h) { if (h != hover) { hover = h; repaint(); } }

    SIReverbProcessor& proc;
    Point<int> hover { -1, -1 };
};

//==============================================================================
class Root : public si::ScaledContent, private Timer
{
public:
    explicit Root (SIReverbProcessor& p)
        : si::ScaledContent (W, H), proc (p), s (p.apvts), presetBar (p.presets), space (p), eq (p), modView (p), table (p)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &sc, &modeBox, &colorSeg, &freezeBtn, &space, &mix, &pre, &decay, &size,
                                                            &attack, &early, &late, &width, &duck, &tabs, &srcSeg, &bpmKnob, &autoBtn,
                                                            &preSync, &decSync, &table, &hicut, &locut, &bmult, &bx, &hshelf, &hfreq,
                                                            &eq, &mrate, &mdepth, &modSync, &modView })
            addAndMakeVisible (c);

        modeBox.getProperties().set ("ghost", true);
        colorSeg.keepCase();
        freezeBtn.getProperties().set ("warn", true);
        autoBtn.getProperties().set ("cta", true);
        autoBtn.setTooltip ("Reads the project tempo (or Manual BPM) and sets Pre-Delay and Decay to the beat for the current mode and size.");
        freezeBtn.setTooltip ("Infinite sustain of the current tail");
        sc.setTooltip ("External sidechain: Duck listens to the Sidechain input (in Ableton pick a track in the plugin's Sidechain panel)");
        p.presets.onChange = [this] { presetBar.refresh(); };

        pre.valueText = [this]
        {
            const int i = proc.effPreIdx.load();
            return i > 0 ? String (si::tempo::preDelayNotes()[i].name) + String (CharPointer_UTF8 (" \xc2\xb7 ")) + si::tempo::formatMs (proc.effPreMs.load()) : String();
        };
        pre.highlighted = [this] { return proc.effPreIdx.load() > 0; };
        decay.valueText = [this]
        {
            const int i = proc.effDecayIdx.load();
            return i > 0 ? String (si::tempo::decayNotes()[i].name) + String (CharPointer_UTF8 (" \xc2\xb7 ")) + si::tempo::formatMs (proc.effDecayS.load() * 1000.0) : String();
        };
        decay.highlighted = [this] { return proc.effDecayIdx.load() > 0; };

        // grabbing a synced knob hands control back to the knob
        pre.slider.onDragStart = [this]
        {
            if (pv (s, "tempoAuto") > 0.5f) { setParam (s, "tempoDecay", (float) proc.effDecayIdx.load()); setParam (s, "tempoAuto", 0.0f); }
            if (proc.effPreIdx.load() > 0) { setParam (s, "predelay", proc.effPreMs.load()); setParam (s, "tempoPre", 0.0f); }
        };
        decay.slider.onDragStart = [this]
        {
            if (pv (s, "tempoAuto") > 0.5f) { setParam (s, "tempoPre", (float) proc.effPreIdx.load()); setParam (s, "tempoAuto", 0.0f); }
            if (proc.effDecayIdx.load() > 0) { setParam (s, "decay", proc.effDecayS.load()); setParam (s, "tempoDecay", 0.0f); }
        };

        tabs.onChange = [this] (int) { updatePages(); };
        updatePages();
        resized();   // base ctor sized us before this class existed
        startTimerHz (60);
    }

    ~Root() override { proc.presets.onChange = nullptr; }

    void selfTest()
    {
        for (int i = 0; i < 3; ++i)
        {
            tabs.setIndex (i);
            si::uiSelfTest (*this, String ("SI Reverb-") + (i == 0 ? "tempo" : (i == 1 ? "eq" : "mod")));
        }
        tabs.setIndex (0);
    }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 260, 44 }, "SI Reverb", "Algorithmic Reverb");
        drawChip (g);

        si::drawPanel (g, { 24, 80, 576, 262 }, {}, true);
        g.setColour (col::label);
        g.setFont (si::font (11.0f));
        g.drawText (si::dsp::getMode ((int) pv (s, "mode")).blurb, Rectangle<int> (46, 124, 290, 16), Justification::centredLeft, true);

        si::drawPanel (g, { 24, 354, 576, 250 }, "Reverb");
        si::drawPanel (g, { 612, 80, 264, 524 });

        if (tabs.getIndex() == 0)
        {
            si::drawSmallLabel (g, { 722, 236, 140, 14 }, "Pre-Delay Sync");
            si::drawSmallLabel (g, { 722, 286, 140, 14 }, "Decay Sync");
            g.setColour (col::hint);
            g.setFont (si::font (10.0f));
            g.drawText ("Click any time to apply it", Rectangle<int> (628, 576, 232, 16), Justification::centredLeft);
        }
        else if (tabs.getIndex() == 2)
            si::drawSmallLabel (g, { 628, 260, 232, 14 }, "Rate Sync");
    }

    void resized() override
    {
        presetBar.setBounds (318, 20, 290, 38);
        sc.setBounds (620, 24, 66, 30);
        modeBox.setBounds (36, 90, 300, 36);
        colorSeg.setBounds (360, 94, 150, 30);
        freezeBtn.setBounds (518, 94, 70, 30);
        space.setBounds (38, 146, 548, 182);

        Rectangle<int> big (40, 388, 544, 114);
        for (auto* k : { &mix, &pre, &decay, &size })
            k->setBounds (big.removeFromLeft (136).reduced (14, 0));
        Rectangle<int> small (40, 510, 544, 84);
        for (auto* k : { &attack, &early, &late, &width, &duck })
            k->setBounds (small.removeFromLeft (108).reduced (14, 0));

        tabs.setBounds (626, 94, 236, 34);
        // tempo
        srcSeg.setBounds (628, 142, 232, 30);
        autoBtn.setBounds (628, 182, 232, 40);
        bpmKnob.setBounds (628, 232, 84, 98);
        preSync.setBounds (722, 252, 138, 28);
        decSync.setBounds (722, 302, 138, 28);
        table.setBounds (628, 344, 232, 226);
        // eq
        Rectangle<int> e1 (628, 142, 232, 96), e2 (628, 246, 232, 96);
        for (auto* k : { &hicut, &locut, &hshelf }) k->setBounds (e1.removeFromLeft (77).reduced (4, 0));
        for (auto* k : { &hfreq, &bmult, &bx }) k->setBounds (e2.removeFromLeft (77).reduced (4, 0));
        eq.setBounds (628, 356, 232, 232);
        // mod
        Rectangle<int> m1 (628, 142, 232, 110);
        for (auto* k : { &mrate, &mdepth }) k->setBounds (m1.removeFromLeft (116).reduced (12, 0));
        modSync.setBounds (628, 278, 232, 30);
        modView.setBounds (628, 322, 232, 266);
    }

private:
    Rectangle<int> chipArea() const { return { 744, 20, 132, 38 }; }

    void drawChip (Graphics& g)
    {
        const bool manual = pv (s, "tempoSource") > 0.5f;
        const bool host = proc.hostBpmValid.load();
        const float beat = proc.playing.load() ? (float) (proc.ppq.load() - std::floor (proc.ppq.load())) : -1.0f;
        si::drawTempoChip (g, chipArea().toFloat(), proc.bpm.load(), manual ? "Manual" : (host ? "Host sync" : "No host"),
                           manual ? col::warn : (host ? col::accent : col::hint), beat);
    }

    void updatePages()
    {
        const int t = tabs.getIndex();
        for (auto* c : std::initializer_list<Component*> { &srcSeg, &bpmKnob, &autoBtn, &preSync, &decSync, &table })
            c->setVisible (t == 0);
        for (auto* c : std::initializer_list<Component*> { &hicut, &locut, &bmult, &bx, &hshelf, &hfreq, &eq })
            c->setVisible (t == 1);
        for (auto* c : std::initializer_list<Component*> { &mrate, &mdepth, &modSync, &modView })
            c->setVisible (t == 2);
        repaint();
    }

    void timerCallback() override
    {
        const bool manual = pv (s, "tempoSource") > 0.5f;
        const bool autoOn = pv (s, "tempoAuto") > 0.5f;
        bpmKnob.setEnabled (manual);
        bpmKnob.setAlpha (manual ? 1.0f : 0.45f);
        preSync.setEnabled (! autoOn);
        decSync.setEnabled (! autoOn);
        preSync.setAlpha (autoOn ? 0.4f : 1.0f);
        decSync.setAlpha (autoOn ? 0.4f : 1.0f);
        if (proc.playing.load() || ++idle % 15 == 0) repaint (chipArea().expanded (8));

        const auto key = String (proc.bpm.load(), 1) + String (proc.effPreMs.load()) + String (proc.effDecayS.load())
                         + String ((int) proc.hostBpmValid.load()) + String ((int) manual) + String (pv (s, "mode"));
        if (key != lastKey)
        {
            lastKey = key;
            repaint (40, 120, 300, 24);
            pre.refresh();
            decay.refresh();
            table.repaint();
            eq.repaint();
        }
    }

    SIReverbProcessor& proc;
    si::APVTS& s;
    si::PresetBar presetBar;
    si::Choice modeBox { s, "mode" };
    si::Segmented colorSeg { s, "color", { "70s", "80s", "Now" } };
    si::Toggle freezeBtn { s, "freeze", "Freeze", si::Icon::Freeze };
    si::Toggle sc { s, "extsc", "SC" };
    SpaceView space;
    si::Knob mix { s, "mix", "Mix" }, pre { s, "predelay", "Pre-Delay" }, decay { s, "decay", "Decay" }, size { s, "size", "Size" };
    si::Knob attack { s, "attack", "Attack" }, early { s, "earlydiff", "Early Diff" }, late { s, "latediff", "Late Diff" },
        width { s, "width", "Width" }, duck { s, "duck", "Duck" };
    si::TabBar tabs { { "Tempo", "EQ", "Mod" } };
    si::Segmented srcSeg { s, "tempoSource" };
    si::Knob bpmKnob { s, "tempoBpm", "BPM" };
    si::Toggle autoBtn { s, "tempoAuto", "Auto Tempo", si::Icon::Auto };
    si::Choice preSync { s, "tempoPre" }, decSync { s, "tempoDecay" };
    si::Knob hicut { s, "highcut", "Hi Cut" }, locut { s, "lowcut", "Lo Cut" }, bmult { s, "bassmult", "Bass Mult" },
        bx { s, "bassxover", "Bass X" }, hshelf { s, "highshelf", "Hi Shelf" }, hfreq { s, "highfreq", "Hi Freq" };
    EqView eq;
    si::Knob mrate { s, "modrate", "Mod Rate" }, mdepth { s, "moddepth", "Mod Depth" };
    si::Choice modSync { s, "tempoMod" };
    ModView modView;
    TempoTable table;
    String lastKey;
    int idle = 0;
};
} // namespace

//==============================================================================
SIReverbEditor::SIReverbEditor (SIReverbProcessor& p) : AudioProcessorEditor (p)
{
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (p);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 3 / 2, H * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    static_cast<Root*> (root.get())->selfTest();
}

SIReverbEditor::~SIReverbEditor() { setLookAndFeel (nullptr); }

void SIReverbEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
