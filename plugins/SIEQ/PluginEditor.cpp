#include "PluginEditor.h"
#include "Sidechain.h"

using namespace juce;
using namespace si::eq;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 1000, H = 640;
constexpr float kMinF = 20.0f, kMaxF = 20000.0f, kRangeDb = 18.0f;

const Colour kBandCol[kBands] = { Colour (0xff9fe83a), Colour (0xff22d3b4), Colour (0xff38bdf8), Colour (0xff6378ff),
                                  Colour (0xffa78bfa), Colour (0xfff472b6), Colour (0xfffb923c), Colour (0xfffacc15) };

void setParam (si::APVTS& s, const String& id, float v)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

bool hasGain (Type t) { return t == Type::Bell || t == Type::LowShelf || t == Type::HighShelf || t == Type::Tilt; }

//==============================================================================
/** Floating controls for the selected band (rebuilt whenever the selection changes). */
class BandPanel : public Component
{
public:
    BandPanel (SIEQProcessor& p) : proc (p)
    {
        for (auto* b : { &onBtn, &soloBtn, &upBtn, &downBtn })
        {
            addAndMakeVisible (b);
            b->getProperties().set ("radius", 8.0);
        }
        si::setIcon (onBtn, si::Icon::Power);
        si::setIcon (soloBtn, si::Icon::Headphones);
        si::setIcon (upBtn, si::Icon::Up);
        si::setIcon (downBtn, si::Icon::Down);
        upBtn.getProperties().set ("seg", true);
        downBtn.getProperties().set ("seg", true);
        soloBtn.setClickingTogglesState (true);
        soloBtn.setTooltip ("Listen to this band only");
        onBtn.setTooltip ("Band on / off");
        soloBtn.onClick = [this] { proc.soloBand = soloBtn.getToggleState() ? band : -1; };
        upBtn.onClick = [this] { if (onSelect) onSelect ((band + 1) % kBands); };
        downBtn.onClick = [this] { if (onSelect) onSelect ((band + kBands - 1) % kBands); };
    }

    std::function<void (int)> onSelect;

    void setBand (int b)
    {
        band = b;
        auto& s = proc.apvts;
        freq = std::make_unique<si::Knob> (s, SIEQProcessor::pid (b, "freq"), "Frequency");
        gain = std::make_unique<si::Knob> (s, SIEQProcessor::pid (b, "gain"), "Gain", true);
        q = std::make_unique<si::Knob> (s, SIEQProcessor::pid (b, "q"), "Q");
        thr = std::make_unique<si::Knob> (s, SIEQProcessor::pid (b, "thr"), "Threshold");
        tgt = std::make_unique<si::Knob> (s, SIEQProcessor::pid (b, "tgt"), "Target", true);
        type = std::make_unique<si::Choice> (s, SIEQProcessor::pid (b, "type"));
        slope = std::make_unique<si::Choice> (s, SIEQProcessor::pid (b, "slope"));
        chan = std::make_unique<si::Choice> (s, SIEQProcessor::pid (b, "chan"));
        dyn = std::make_unique<si::Toggle> (s, SIEQProcessor::pid (b, "dyn"), "Dynamic");
        tight = std::make_unique<si::Segmented> (s, SIEQProcessor::pid (b, "tight"), StringArray { "Smooth", "Tight" });
        tight->keepCase();
        onAtt = std::make_unique<si::APVTS::ButtonAttachment> (s, SIEQProcessor::pid (b, "on"), onBtn);
        onBtn.setClickingTogglesState (true);
        for (Component* c : std::initializer_list<Component*> { freq.get(), gain.get(), q.get(), thr.get(), tgt.get(), type.get(), slope.get(),
                                                               chan.get(), dyn.get(), tight.get() })
            addAndMakeVisible (c);
        soloBtn.setToggleState (proc.soloBand.load() == b, dontSendNotification);
        refresh();
        resized();
        repaint();
    }

    void refresh()
    {
        if (freq == nullptr) return;
        const auto p = proc.readBand (band);
        const bool cut = p.type == Type::LowCut || p.type == Type::HighCut;
        gain->setEnabled (hasGain (p.type));
        gain->setAlpha (hasGain (p.type) ? 1.0f : 0.35f);
        slope->setEnabled (cut);
        slope->setAlpha (cut ? 1.0f : 0.35f);
        for (Component* c : std::initializer_list<Component*> { thr.get(), tgt.get(), tight.get() })
        {
            c->setEnabled (p.dyn);
            c->setAlpha (p.dyn ? 1.0f : 0.35f);
        }
        // Pro-Q style ring on the gain knob: static gain -> dynamic target
        auto& props = gain->slider.getProperties();
        if (p.dyn && hasGain (p.type))
        {
            auto* gp = proc.apvts.getParameter (SIEQProcessor::pid (band, "gain"));
            const float f0 = gp->convertTo0to1 (p.gain), f1 = gp->convertTo0to1 (jlimit (-24.0f, 24.0f, p.target));
            if ((float) props.getWithDefault ("dynFrom", -1.0f) != f0 || (float) props.getWithDefault ("dynTo", -1.0f) != f1)
            {
                props.set ("dynFrom", f0);
                props.set ("dynTo", f1);
                props.set ("dynColour", (int64) kBandCol[band].getARGB());
                gain->slider.repaint();
            }
        }
        else if (props.contains ("dynFrom"))
        {
            props.remove ("dynFrom");
            props.remove ("dynTo");
            gain->slider.repaint();
        }
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0, 4).expanded (2), 18.0f);
        g.setColour (Colour (0xf0101217));
        g.fillRoundedRectangle (r, 16.0f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.14f), 0, r.getY(), Colours::white.withAlpha (0.03f), 0, r.getBottom(), false));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
        // band badge
        const auto c = kBandCol[band];
        auto badge = Rectangle<float> (14.0f, r.getCentreY() - 13.0f, 26.0f, 26.0f);
        g.setColour (c.withAlpha (0.25f));
        g.fillEllipse (badge.expanded (3.0f));
        g.setColour (c);
        g.fillEllipse (badge);
        g.setColour (col::bg);
        g.setFont (si::font (12.0f, Weight::SemiBold));
        g.drawText (String (band + 1), badge, Justification::centred);
        // dividers
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRect (88.0f, 14.0f, 1.0f, r.getHeight() - 28.0f);
        g.fillRect (502.0f, 14.0f, 1.0f, r.getHeight() - 28.0f);
    }

    void resized() override
    {
        upBtn.setBounds (16, 8, 22, 18);
        downBtn.setBounds (16, getHeight() - 26, 22, 18);
        onBtn.setBounds (48, 22, 30, 30);
        soloBtn.setBounds (48, 62, 30, 30);
        if (freq == nullptr) return;
        Rectangle<int> k (96, 12, 222, 96);
        for (auto* kn : { freq.get(), gain.get(), q.get() }) kn->setBounds (k.removeFromLeft (74).reduced (3, 0));
        type->setBounds (326, 14, 168, 28);
        slope->setBounds (326, 46, 82, 28);
        chan->setBounds (412, 46, 82, 28);
        dyn->setBounds (326, 80, 168, 26);
        tight->setBounds (512, 82, 152, 26);
        Rectangle<int> d (510, 4, 156, 76);
        thr->setBounds (d.removeFromLeft (78).reduced (2, 0));
        tgt->setBounds (d.reduced (2, 0));
    }

    int band = 0;

private:
    SIEQProcessor& proc;
    TextButton onBtn, soloBtn, upBtn, downBtn;
    std::unique_ptr<si::APVTS::ButtonAttachment> onAtt;
    std::unique_ptr<si::Knob> freq, gain, q, thr, tgt;
    std::unique_ptr<si::Choice> type, slope, chan;
    std::unique_ptr<si::Toggle> dyn;
    std::unique_ptr<si::Segmented> tight;
};

//==============================================================================
/** Main display: analyzer, per-band fills, total + live curve, draggable nodes. */
class EqDisplay : public Component, private Timer
{
public:
    explicit EqDisplay (SIEQProcessor& p) : proc (p), panel (p)
    {
        addAndMakeVisible (panel);
        panel.onSelect = [this] (int b) { select (b); };
        startTimerHz (30);
    }

    void select (int b)
    {
        selected = b;
        panel.setBand (b);
        repaint();
    }

    void resized() override
    {
        plot = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        panel.setBounds (getWidth() / 2 - 340, getHeight() - 132, 680, 118);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.38f));
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
        const float sr = (float) proc.getRate();

        // grid
        g.setFont (si::font (9.5f, Weight::Medium));
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = fx (f);
            g.setColour (Colours::white.withAlpha (f == 100.f || f == 1000.f || f == 10000.f ? 0.07f : 0.035f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (col::accentHi.withAlpha (0.65f));
            g.drawText (f >= 1000.f ? String ((int) (f / 1000)) + "k" : String ((int) f), Rectangle<float> (x - 20.0f, plot.getY() + 4.0f, 40.0f, 12.0f), Justification::centred);
        }
        for (int db = -12; db <= 12; db += 6)
        {
            const float y = dy ((float) db);
            g.setColour (Colours::white.withAlpha (db == 0 ? 0.10f : 0.035f));
            g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour (col::info.withAlpha (0.6f));
            g.drawText ((db > 0 ? "+" : "") + String (db), Rectangle<float> (plot.getX() + 2.0f, y - 12.0f, 30.0f, 11.0f), Justification::left);
        }

        // analyzer
        const int amode = (int) proc.apvts.getRawParameterValue ("analyzer")->load();
        if (amode != 2)
        {
            Path spec;
            const int N = 220;
            for (int i = 0; i <= N; ++i)
            {
                const float f = kMinF * std::pow (kMaxF / kMinF, (float) i / N);
                const float db = spectrum.dbAt (f);
                const float y = jlimit (plot.getY(), plot.getBottom(), plot.getBottom() - plot.getHeight() * (db + 90.0f) / 90.0f);
                i == 0 ? spec.startNewSubPath (fx (f), y) : spec.lineTo (fx (f), y);
            }
            Path fill (spec);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.13f), 0, plot.getY(), Colours::white.withAlpha (0.01f), 0, plot.getBottom(), false));
            g.fillPath (fill);
            g.setColour (Colours::white.withAlpha (0.28f));
            g.strokePath (spec, PathStrokeType (1.0f));
        }

        // per-band fills
        std::array<BandParams, kBands> bp;
        for (int i = 0; i < kBands; ++i) bp[(size_t) i] = proc.readBand (i);
        const int N = 260;
        std::vector<float> total ((size_t) N + 1, 0.0f), live ((size_t) N + 1, 0.0f);
        bool anyDyn = false;
        for (int bi = 0; bi < kBands; ++bi)
        {
            const auto& b = bp[(size_t) bi];
            if (! b.on) continue;
            anyDyn |= b.dyn;
            Path band;
            band.startNewSubPath (plot.getX(), dy (0));
            const float lg = jlimit (jmin (b.gain, b.target), jmax (b.gain, b.target), proc.getLiveGain (bi));   // never outside the dynamic range
            for (int i = 0; i <= N; ++i)
            {
                const float f = kMinF * std::pow (kMaxF / kMinF, (float) i / N);
                const float db = bandResponseDb (b, b.gain, f, sr);
                total[(size_t) i] += db;
                live[(size_t) i] += b.dyn ? bandResponseDb (b, lg, f, sr) : db;
                band.lineTo (fx (f), dy (db));
            }
            band.lineTo (plot.getRight(), dy (0));
            band.closeSubPath();
            const auto c = kBandCol[bi];
            const float a = bi == selected ? 0.32f : 0.18f;
            const bool cut = b.type == Type::LowCut || b.type == Type::HighCut;
            g.setGradientFill (ColourGradient (c.withAlpha (a), 0, dy (cut ? -kRangeDb : 6.0f), c.withAlpha (a * 0.25f), 0, dy (0), false));
            g.fillPath (band);

            // dynamic range (Pro-Q style): the area the band can travel between its static gain and its target
            if (b.dyn && hasGain (b.type) && std::abs (b.target - b.gain) > 0.05f)
            {
                Path range;
                for (int i = 0; i <= N; ++i)
                {
                    const float f = kMinF * std::pow (kMaxF / kMinF, (float) i / N);
                    i == 0 ? range.startNewSubPath (fx (f), dy (bandResponseDb (b, b.gain, f, sr))) : range.lineTo (fx (f), dy (bandResponseDb (b, b.gain, f, sr)));
                }
                Path tgt;
                for (int i = N; i >= 0; --i)
                {
                    const float f = kMinF * std::pow (kMaxF / kMinF, (float) i / N);
                    const auto pt = Point<float> (fx (f), dy (bandResponseDb (b, b.target, f, sr)));
                    range.lineTo (pt);
                    i == N ? tgt.startNewSubPath (pt) : tgt.lineTo (pt);
                }
                range.closeSubPath();
                g.setGradientFill (ColourGradient (c.withAlpha (0.55f), 0, dy (b.gain), c.withAlpha (0.25f), 0, dy (b.target), false));
                g.fillPath (range);
                g.setColour (c.withAlpha (0.9f));
                g.strokePath (tgt, PathStrokeType (1.2f));
            }
        }

        auto curve = [&] (const std::vector<float>& v)
        {
            Path p;
            for (int i = 0; i <= N; ++i)
            {
                const float f = kMinF * std::pow (kMaxF / kMinF, (float) i / N);
                i == 0 ? p.startNewSubPath (fx (f), dy (v[(size_t) i])) : p.lineTo (fx (f), dy (v[(size_t) i]));
            }
            return p;
        };
        const auto tp = curve (total);
        g.setColour (Colours::white);
        g.strokePath (tp, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        if (anyDyn)
        {
            g.setColour (col::accentHi.withAlpha (0.9f));
            g.strokePath (curve (live), PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // nodes
        for (int bi = 0; bi < kBands; ++bi)
        {
            const auto& b = bp[(size_t) bi];
            if (! b.on) continue;
            const auto pt = nodePos (b);
            const auto c = kBandCol[bi];
            if (b.dyn && hasGain (b.type))
            {
                // up / down arrows under the node show where the dynamic range goes
                const bool down = b.target < b.gain;
                const float ay = pt.y + 15.0f;
                Path tri;
                tri.addTriangle (pt.x - 4.0f, down ? ay : ay + 5.0f, pt.x + 4.0f, down ? ay : ay + 5.0f, pt.x, down ? ay + 5.0f : ay);
                g.setColour (Colours::white.withAlpha (0.75f));
                g.fillPath (tri);
            }
            const bool sel = bi == selected, hov = bi == hover;
            const float rad = sel ? 9.0f : (hov ? 8.5f : 7.5f);
            g.setColour (sel ? Colours::white : col::bg);
            g.fillEllipse (Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pt));
            g.setColour (c);
            g.drawEllipse (Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pt), 2.0f);
            g.setColour (sel ? col::bg : c);
            g.setFont (si::font (9.5f, Weight::SemiBold));
            g.drawText (String (bi + 1), Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pt).translated (0, 0.5f), Justification::centred);
        }

        // hover readout
        if (mouseInside && plot.contains (mousePos))
        {
            const float f = xf (mousePos.x);
            const String t = f >= 1000.f ? String (f / 1000.f, 2) + " kHz" : String (roundToInt (f)) + " Hz";
            auto tag = Rectangle<float> (mousePos.x - 32.0f, plot.getY() + 2.0f, 64.0f, 18.0f);
            g.setColour (col::card);
            g.fillRoundedRectangle (tag, 5.0f);
            g.setColour (col::text);
            g.setFont (si::font (10.0f, Weight::Medium));
            g.drawText (t, tag, Justification::centred);
        }
        if (selected < 0 || ! bp[(size_t) selected].on)
        {
            g.setColour (col::hint);
            g.setFont (si::font (11.0f));
            g.drawText (String (CharPointer_UTF8 ("Double-click to add a band  \xc2\xb7  drag to move  \xc2\xb7  scroll for Q  \xc2\xb7  right-click for options")),
                        plot.withHeight (20.0f).translated (0, 22), Justification::centred);
        }
    }

    // ---- mouse
    void mouseMove (const MouseEvent& e) override
    {
        mousePos = e.position;
        mouseInside = true;
        const int h = hitNode (e.position);
        if (h != hover) hover = h;
        setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::CrosshairCursor);
        repaint();
    }
    void mouseExit (const MouseEvent&) override { mouseInside = false; hover = -1; repaint(); }

    void mouseDown (const MouseEvent& e) override
    {
        const int h = hitNode (e.position);
        if (h >= 0)
        {
            if (h != selected) select (h);
            if (e.mods.isPopupMenu()) { showMenu (h); return; }
            dragging = h;
            const auto b = proc.readBand (h);
            dragStartF = b.freq;
            dragStartG = b.gain;
            for (const char* n : { "freq", "gain" })
                if (auto* p = proc.apvts.getParameter (SIEQProcessor::pid (h, n))) p->beginChangeGesture();
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging < 0) return;
        mousePos = e.position;
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const auto start = e.mouseDownPosition;
        const float x = start.x + (e.position.x - start.x) * fine;
        const float y = start.y + (e.position.y - start.y) * fine;
        const float startX = fx (dragStartF), startY = dy (dragStartG);
        const float f = jlimit (kMinF, kMaxF, xf (startX + (x - start.x)));
        const float gdb = jlimit (-24.0f, 24.0f, yd (startY + (y - start.y)));
        if (auto* p = proc.apvts.getParameter (SIEQProcessor::pid (dragging, "freq"))) p->setValueNotifyingHost (p->convertTo0to1 (f));
        if (hasGain (proc.readBand (dragging).type))
            if (auto* p = proc.apvts.getParameter (SIEQProcessor::pid (dragging, "gain"))) p->setValueNotifyingHost (p->convertTo0to1 (gdb));
        repaint();
    }

    void mouseUp (const MouseEvent&) override
    {
        if (dragging >= 0)
            for (const char* n : { "freq", "gain" })
                if (auto* p = proc.apvts.getParameter (SIEQProcessor::pid (dragging, n))) p->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int h = hitNode (e.position);
        if (h >= 0)
        {
            setParam (proc.apvts, SIEQProcessor::pid (h, "on"), 0.0f);
            if (proc.soloBand.load() == h) proc.soloBand = -1;
            repaint();
            return;
        }
        if (! plot.contains (e.position)) return;
        for (int i = 0; i < kBands; ++i)
            if (! proc.readBand (i).on)
            {
                const float f = xf (e.position.x);
                const int t = f < 35.0f ? (int) Type::LowCut : (f > 17000.0f ? (int) Type::HighCut : (int) Type::Bell);
                auto& s = proc.apvts;
                setParam (s, SIEQProcessor::pid (i, "type"), (float) t);
                setParam (s, SIEQProcessor::pid (i, "freq"), f);
                setParam (s, SIEQProcessor::pid (i, "gain"), t == (int) Type::Bell ? jlimit (-24.0f, 24.0f, yd (e.position.y)) : 0.0f);
                setParam (s, SIEQProcessor::pid (i, "q"), 0.71f);
                setParam (s, SIEQProcessor::pid (i, "dyn"), 0.0f);
                setParam (s, SIEQProcessor::pid (i, "on"), 1.0f);
                select (i);
                return;
            }
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const int h = hitNode (e.position);
        const int b = h >= 0 ? h : selected;
        if (b < 0 || ! proc.readBand (b).on) return;
        if (auto* p = proc.apvts.getParameter (SIEQProcessor::pid (b, "q")))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.15f));
            p->endChangeGesture();
        }
    }

    void refreshPanel() { panel.refresh(); }
    int selected = 0;

private:
    float fx (float f) const { return plot.getX() + plot.getWidth() * std::log (f / kMinF) / std::log (kMaxF / kMinF); }
    float xf (float x) const { return kMinF * std::pow (kMaxF / kMinF, jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth())); }
    float dy (float db) const { return plot.getCentreY() - (plot.getHeight() * 0.5f) * jlimit (-1.2f, 1.2f, db / kRangeDb); }
    float yd (float y) const { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * kRangeDb; }

    Point<float> nodePos (const BandParams& b) const { return { fx (b.freq), dy (hasGain (b.type) ? b.gain : 0.0f) }; }

    int hitNode (Point<float> pos) const
    {
        int best = -1;
        float bestD = 14.0f;
        for (int i = 0; i < kBands; ++i)
        {
            const auto b = proc.readBand (i);
            if (! b.on) continue;
            const float d = nodePos (b).getDistanceFrom (pos);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void showMenu (int b)
    {
        PopupMenu m, types, slopes, chans;
        const auto p = proc.readBand (b);
        const char* tn[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt" };
        for (int i = 0; i < 8; ++i) types.addItem (100 + i, tn[i], true, (int) p.type == i);
        const char* sn[] = { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
        for (int i = 0; i < 4; ++i) slopes.addItem (200 + i, sn[i], true, p.slope == i);
        const char* cn[] = { "Stereo", "Mid", "Side", "Left", "Right" };
        for (int i = 0; i < 5; ++i) chans.addItem (300 + i, cn[i], true, (int) p.chan == i);
        m.addSectionHeader ("Band " + String (b + 1));
        m.addSubMenu ("Type", types);
        m.addSubMenu ("Slope", slopes, p.type == Type::LowCut || p.type == Type::HighCut);
        m.addSubMenu ("Channel", chans);
        m.addItem (400, "Dynamic", true, p.dyn);
        m.addSeparator();
        m.addItem (500, "Delete band");
        m.showMenuAsync (PopupMenu::Options().withMousePosition(), [this, b] (int r)
        {
            auto& s = proc.apvts;
            if (r >= 100 && r < 108) setParam (s, SIEQProcessor::pid (b, "type"), (float) (r - 100));
            if (r >= 200 && r < 204) setParam (s, SIEQProcessor::pid (b, "slope"), (float) (r - 200));
            if (r >= 300 && r < 305) setParam (s, SIEQProcessor::pid (b, "chan"), (float) (r - 300));
            if (r == 400) setParam (s, SIEQProcessor::pid (b, "dyn"), proc.readBand (b).dyn ? 0.0f : 1.0f);
            if (r == 500) setParam (s, SIEQProcessor::pid (b, "on"), 0.0f);
            panel.refresh();
            repaint();
        });
    }

    void timerCallback() override
    {
        const int amode = (int) proc.apvts.getRawParameterValue ("analyzer")->load();
        if (amode != 2) spectrum.update (amode == 1 ? proc.preFifo : proc.postFifo, proc.getRate());
        panel.refresh();
        repaint();
    }

    SIEQProcessor& proc;
    BandPanel panel;
    si::Spectrum spectrum;
    Rectangle<float> plot;
    int hover = -1, dragging = -1;
    float dragStartF = 1000.0f, dragStartG = 0.0f;
    Point<float> mousePos;
    bool mouseInside = false;
};

//==============================================================================
class OutputColumn : public Component, private Timer
{
public:
    explicit OutputColumn (SIEQProcessor& p) : proc (p), out (p.apvts, "output", "Output", true), autoBtn (p.apvts, "autogain", "Auto")
    {
        addAndMakeVisible (out);
        addAndMakeVisible (autoBtn);
        autoBtn.setTooltip ("Auto gain: keeps loudness constant while you EQ, so you judge tone, not volume");
        const char* names[] = { "L", "R", "M", "S" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = listen.add (new TextButton (names[i]));
            b->getProperties().set ("radius", 7.0);
            b->setClickingTogglesState (false);
            b->onClick = [this, i]
            {
                const int cur = (int) proc.apvts.getRawParameterValue ("listen")->load();
                setParam (proc.apvts, "listen", cur == i + 1 ? 0.0f : (float) (i + 1));
            };
            b->setTooltip ("Listen to Left / Right / Mid / Side of the output");
            addAndMakeVisible (b);
        }
        startTimerHz (30);
    }

    void paint (Graphics& g) override
    {
        auto m = Rectangle<float> (14.0f, 14.0f, (float) getWidth() - 28.0f, 300.0f);
        auto l = m.removeFromLeft (m.getWidth() * 0.5f - 2.0f);
        m.removeFromLeft (4.0f);
        si::drawLevelMeter (g, l, proc.meterL.load());
        si::drawLevelMeter (g, m, proc.meterR.load());
        const float ag = proc.autoGainDb.load();
        if (std::abs (ag) > 0.05f)
        {
            g.setColour (col::accentHi);
            g.setFont (si::font (9.5f, Weight::Medium));
            g.drawText ((ag > 0 ? "+" : "") + String (ag, 1) + " dB", Rectangle<int> (0, 452, getWidth(), 14), Justification::centred);
        }
    }

    void resized() override
    {
        out.setBounds (6, 322, getWidth() - 12, 92);
        autoBtn.setBounds (10, 420, getWidth() - 20, 28);
        for (int i = 0; i < 4; ++i)
            listen[i]->setBounds (10 + (i % 2) * ((getWidth() - 20) / 2 + 2), 472 + (i / 2) * 30, (getWidth() - 24) / 2, 26);
    }

private:
    void timerCallback() override
    {
        const int cur = (int) proc.apvts.getRawParameterValue ("listen")->load();
        for (int i = 0; i < 4; ++i) listen[i]->setToggleState (cur == i + 1, dontSendNotification);
        repaint (0, 0, getWidth(), 320);
        repaint (0, 450, getWidth(), 18);
    }
    SIEQProcessor& proc;
    si::Knob out;
    si::Toggle autoBtn;
    OwnedArray<TextButton> listen;
};

//==============================================================================
class Root : public si::ScaledContent
{
public:
    explicit Root (SIEQProcessor& p) : si::ScaledContent (W, H), proc (p), presetBar (p.presets), display (p), outCol (p)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &aBtn, &bBtn, &analyzerSeg, &sc, &display, &outCol })
            addAndMakeVisible (c);
        p.presets.onChange = [this] { presetBar.refresh(); display.refreshPanel(); };
        aBtn.setRadioGroupId (7);
        bBtn.setRadioGroupId (7);
        aBtn.setToggleState (p.getAB() == 0, dontSendNotification);
        bBtn.setToggleState (p.getAB() == 1, dontSendNotification);
        aBtn.onClick = [this] { if (proc.getAB() != 0) proc.toggleAB(); syncAB(); };
        bBtn.onClick = [this] { if (proc.getAB() != 1) proc.toggleAB(); syncAB(); };
        aBtn.setTooltip ("Compare two settings");
        sc.setTooltip ("External sidechain: Dynamic bands listen to the Sidechain input (in Ableton pick a track in the plugin's Sidechain panel)");
        // select the first enabled band
        int first = 0;
        for (int i = 0; i < kBands; ++i) if (p.readBand (i).on) { first = i; break; }
        display.select (first);
        resized();
    }
    ~Root() override { proc.presets.onChange = nullptr; }
    void selectFirst()
    {
        for (int i = 0; i < kBands; ++i) if (proc.readBand (i).on) { display.select (i); return; }
    }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 260, 44 }, "SI EQ", "Dynamic Equalizer");
        si::drawPanel (g, { 24, 80, 860, 544 }, {}, true);
        si::drawPanel (g, { 896, 80, 80, 544 });
    }

    void resized() override
    {
        presetBar.setBounds (300, 20, 300, 38);
        aBtn.setBounds (614, 24, 40, 30);
        bBtn.setBounds (658, 24, 40, 30);
        analyzerSeg.setBounds (716, 24, 168, 30);
        sc.setBounds (900, 24, 70, 30);
        display.setBounds (36, 92, 836, 520);
        outCol.setBounds (896, 80, 80, 544);
    }

private:
    void syncAB()
    {
        aBtn.setToggleState (proc.getAB() == 0, dontSendNotification);
        bBtn.setToggleState (proc.getAB() == 1, dontSendNotification);
        display.refreshPanel();
        repaint();
    }
    SIEQProcessor& proc;
    si::PresetBar presetBar;
    TextButton aBtn { "A" }, bBtn { "B" };
    si::Segmented analyzerSeg { proc.apvts, "analyzer" };
    si::Toggle sc { proc.apvts, "extsc", "SC" };
    EqDisplay display;
    OutputColumn outCol;
};
} // namespace

//==============================================================================
SIEQEditor::SIEQEditor (SIEQProcessor& p) : AudioProcessorEditor (p)
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
        p.presets.load (1);   // dev snapshot with a real curve
        static_cast<Root*> (root.get())->selectFirst();
        si::uiSelfTest (*root, "SI EQ");
    }
}

SIEQEditor::~SIEQEditor() { setLookAndFeel (nullptr); }

void SIEQEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
