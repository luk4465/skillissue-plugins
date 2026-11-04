#include "PluginEditor.h"
#include "Sidechain.h"

using namespace juce;
using namespace si::rack;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 1100, H = 660;
float pv (si::APVTS& s, const String& id) { return s.getRawParameterValue (id)->load(); }

void setParam (si::APVTS& s, const String& id, float real)
{
    if (auto* p = s.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (real)); p->endChangeGesture(); }
}

const StringArray& categories() { static const StringArray c { "EQ", "Dynamics", "Color", "Utility" }; return c; }

void showModuleMenu (SIRackProcessor& proc, int slot, Component* target, std::function<void()> done)
{
    PopupMenu m;
    for (auto& c : categories())
    {
        m.addSectionHeader (c);
        for (int t = 1; t < kNumModules; ++t)
            if (String (moduleDef (t).category) == c) m.addItem (t, moduleDef (t).name, true, proc.slotType (slot) == t);
    }
    if (proc.slotType (slot) > 0) { m.addSeparator(); m.addItem (1000, "Remove module"); }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target), [&proc, slot, done] (int r)
    {
        if (r > 0 && r < kNumModules) proc.loadModule (slot, r);
        if (r == 1000) proc.loadModule (slot, 0);
        if (done) done();
    });
}

/** Horizontal gain-reduction meter, 0..20 dB with a scale. */
void drawGrMeter (Graphics& g, Rectangle<float> r, float grDb, bool big)
{
    const float gr = jlimit (0.0f, 20.0f, -grDb);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    auto fill = r.reduced (2.0f);
    fill = fill.withTrimmedLeft (fill.getWidth() * (1.0f - gr / 20.0f));
    g.setGradientFill (ColourGradient (col::warn, fill.getX(), 0, col::accent, r.getRight(), 0, false));
    if (gr > 0.05f) g.fillRoundedRectangle (fill, fill.getHeight() * 0.5f);
    if (! big) return;
    g.setFont (si::font (9.0f, Weight::SemiBold));
    for (int d : { 0, 2, 4, 6, 10, 15, 20 })
    {
        const float x = r.getRight() - r.getWidth() * (float) d / 20.0f;
        g.setColour (Colours::white.withAlpha (0.25f));
        g.fillRect (x - 0.5f, r.getBottom() + 3.0f, 1.0f, 4.0f);
        g.setColour (col::hint);
        g.drawText (String (d), Rectangle<float> (x - 12.0f, r.getBottom() + 7.0f, 24.0f, 11.0f), Justification::centred);
    }
}

//============================================================================== rack tile (one slot)
class SlotTile : public Component, public SettableTooltipClient
{
public:
    SlotTile (SIRackProcessor& p, int s)
        : proc (p), slot (s), power (p.apvts, SIRackProcessor::pid (s, "on"), "", si::Icon::Power), solo (p.apvts, SIRackProcessor::pid (s, "solo"), "S")
    {
        power.getProperties().set ("radius", 7.0);
        solo.getProperties().set ("radius", 7.0);
        solo.getProperties().set ("tint", (int64) 0xffffb020);
        power.setTooltip ("Power (bypass the module)");
        solo.setTooltip ("Solo: hear only the soloed modules");
        addAndMakeVisible (power);
        addAndMakeVisible (solo);
        setTooltip ("Click to open, drag to move, right-click to change the module");
    }
    std::function<void (int)> onSelect, onAdd;
    std::function<void (int)> onDragStart, onDragTo;   // drag a module: start (this slot), then the slot under the mouse
    std::function<int (Point<int>)> slotAt;  // parent coordinates -> slot under the mouse
    bool selected = false;

    void refresh()
    {
        const bool has = proc.slotType (slot) > 0;
        if (power.isVisible() != has) { power.setVisible (has); solo.setVisible (has); }
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const int type = proc.slotType (slot);
        const auto& d = moduleDef (type);
        const bool on = pv (proc.apvts, SIRackProcessor::pid (slot, "on")) > 0.5f;
        if (type > 0)
            g.setGradientFill (ColourGradient (Colour (d.tint).withMultipliedBrightness (on ? 0.9f : 0.45f).withAlpha (0.85f), 0, r.getY(),
                                               Colour (0xff111114), 0, r.getBottom(), false));
        else
            g.setColour (Colours::white.withAlpha (dragHover ? 0.06f : 0.025f));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (selected ? col::accent : Colours::white.withAlpha (type > 0 ? 0.10f : 0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, selected ? 2.0f : 1.0f);
        g.setColour (col::hint);
        g.setFont (si::font (9.5f, Weight::Bold));
        g.drawText (String (slot + 1), Rectangle<float> (r.getX() + 10.0f, r.getY() + 8.0f, 20.0f, 12.0f), Justification::centredLeft);
        if (type == 0)
        {
            g.setColour (col::label);
            g.setFont (si::font (26.0f, Weight::Medium));
            g.drawText ("+", r.withTrimmedBottom (14.0f), Justification::centred);
            g.setFont (si::font (10.0f, Weight::Medium));
            g.drawText ("Add module", r.withTrimmedTop (r.getHeight() * 0.55f), Justification::centred);
            return;
        }
        g.setColour (on ? col::text : col::hint);
        g.setFont (si::font (14.0f, Weight::ExtraBold));
        g.drawFittedText (d.name, Rectangle<int> ((int) r.getX() + 10, (int) r.getY() + 22, (int) r.getWidth() - 20, 20), Justification::centredLeft, 1);
        g.setColour (Colours::white.withAlpha (on ? 0.6f : 0.3f));
        g.setFont (si::font (8.5f, Weight::Bold, 0.12f));
        const String mode = d.modes.empty() ? String() : String (d.modes[(size_t) jlimit (0, (int) d.modes.size() - 1, (int) pv (proc.apvts, SIRackProcessor::pid (slot, "mode")))]);
        g.drawText (String (d.category).toUpperCase() + (mode.isNotEmpty() ? "  " + mode.toUpperCase() : String()),
                    Rectangle<float> (r.getX() + 10.0f, r.getY() + 42.0f, r.getWidth() - 20.0f, 12.0f), Justification::centredLeft);
        if (d.hasGR) drawGrMeter (g, Rectangle<float> (r.getX() + 10.0f, r.getY() + 60.0f, r.getWidth() - 20.0f, 6.0f), proc.gr[(size_t) slot].load(), false);
    }

    void resized() override
    {
        power.setBounds (8, getHeight() - 34, 34, 26);
        solo.setBounds (46, getHeight() - 34, 30, 26);
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragged = false;
        if (e.mods.isPopupMenu()) { showModuleMenu (proc, slot, this, [this] { if (onSelect) onSelect (slot); }); return; }
        if (proc.slotType (slot) == 0) { if (onAdd) onAdd (slot); return; }
        if (onSelect) onSelect (slot);
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 8 || ! slotAt || ! onDragTo) return;
        if (! dragged)
        {
            if (proc.slotType (slot) == 0) return;
            setMouseCursor (MouseCursor::DraggingHandCursor);
            dragged = true;
            if (onDragStart) onDragStart (slot);
        }
        const int target = slotAt (e.getEventRelativeTo (getParentComponent()).getPosition());
        if (target >= 0) onDragTo (target);   // the root knows where the dragged module is now
    }
    void mouseUp (const MouseEvent&) override { setMouseCursor (MouseCursor::NormalCursor); dragged = false; }

    bool dragHover = false;

private:
    SIRackProcessor& proc;
    int slot;
    si::Toggle power, solo;
    bool dragged = false;
};

//============================================================================== module browser (cards, inside the plugin)
class ModuleBrowser : public Component
{
public:
    std::function<void (int)> onPick;
    std::function<void()> onClose;
    int current = 0;

    ModuleBrowser()
    {
        close.getProperties().set ("radius", 10.0);
        close.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (close);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xf20d0d10));
        g.fillRoundedRectangle (r, 18.0f);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 1.0f);
        g.setColour (col::text);
        g.setFont (si::font (20.0f, Weight::ExtraBold));
        g.drawText ("Choose a module", Rectangle<float> (28, 18, 400, 28), Justification::centredLeft);
        g.setColour (col::label);
        g.setFont (si::font (11.0f));
        g.drawText ("Click a card to load it into the selected slot", Rectangle<float> (28, 46, 400, 16), Justification::centredLeft);
        for (int c = 0; c < categories().size(); ++c)
        {
            auto col0 = column (c);
            g.setColour (col::accentHi);
            g.setFont (si::font (10.0f, Weight::Bold, 0.15f));
            g.drawText (categories()[c].toUpperCase(), col0.removeFromTop (18), Justification::centredLeft);
        }
        for (auto& cd : cards ())
        {
            const auto& d = moduleDef (cd.type);
            auto b = cd.bounds.toFloat();
            const bool hot = cd.type == hover, cur = cd.type == current;
            g.setGradientFill (ColourGradient (Colour (d.tint).withMultipliedBrightness (hot ? 1.0f : 0.75f).withAlpha (0.9f), 0, b.getY(), Colour (0xff141417), 0, b.getBottom(), false));
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (cur ? col::accent : Colours::white.withAlpha (hot ? 0.25f : 0.08f));
            g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, cur ? 2.0f : 1.0f);
            g.setColour (col::text);
            g.setFont (si::font (13.0f, Weight::Bold));
            g.drawText (d.name, b.reduced (12.0f, 6.0f).removeFromTop (18.0f), Justification::centredLeft);
            g.setColour (Colours::white.withAlpha (0.62f));
            g.setFont (si::font (9.5f));
            g.drawFittedText (d.blurb, b.reduced (12.0f, 6.0f).withTrimmedTop (19.0f).toNearestInt(), Justification::topLeft, 2);
        }
    }

    void resized() override { close.setBounds (getWidth() - 118, 20, 96, 30); }
    void mouseMove (const MouseEvent& e) override { const int t = cardAt (e.getPosition()); if (t != hover) { hover = t; repaint(); } }
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const MouseEvent& e) override { const int t = cardAt (e.getPosition()); if (t > 0 && onPick) onPick (t); }

private:
    struct Card { int type; Rectangle<int> bounds; };
    Rectangle<int> column (int c) const
    {
        const int top = 76, w = (getWidth() - 56 - 3 * 16) / 4;
        return { 28 + c * (w + 16), top, w, getHeight() - top - 20 };
    }
    std::vector<Card> cards() const
    {
        std::vector<Card> out;
        for (int c = 0; c < categories().size(); ++c)
        {
            auto area = column (c).withTrimmedTop (24);
            for (int t = 1; t < kNumModules; ++t)
                if (String (moduleDef (t).category) == categories()[c])
                    out.push_back ({ t, area.removeFromTop (54).withTrimmedBottom (6) });
        }
        return out;
    }
    int cardAt (Point<int> p) const { for (auto& c : cards()) if (c.bounds.contains (p)) return c.type; return -1; }
    int hover = -1;
    TextButton close { "Close" };
};

//============================================================================== module faceplate (the selected slot, large)
class Faceplate : public Component, private Timer
{
public:
    explicit Faceplate (SIRackProcessor& p) : proc (p)
    {
        for (auto* b : { &changeBtn, &removeBtn }) { b->getProperties().set ("radius", 10.0); addAndMakeVisible (b); }
        changeBtn.getProperties().set ("cta", true);
        changeBtn.onClick = [this] { if (onBrowse) onBrowse(); };
        removeBtn.onClick = [this] { proc.loadModule (slot, 0); rebuild(); };
        startTimerHz (25);
    }
    std::function<void()> onBrowse;

    void setSlot (int s) { slot = s; rebuild(); }

    void rebuild()
    {
        type = proc.slotType (slot);
        knobs.clear();
        modeButtons.clear();
        const auto& d = moduleDef (type);
        auto& s = proc.apvts;
        for (int k = 0; k < kParams; ++k)
        {
            if (d.knobs[(size_t) k].name == nullptr) continue;
            auto kn = std::make_unique<si::Knob> (s, SIRackProcessor::pid (slot, "p" + String (k + 1)), d.knobs[(size_t) k].name);
            const int t = type, idx = k;
            auto* raw = kn.get();
            kn->valueText = [t, idx, raw] { return knobText (t, idx, (float) raw->slider.getValue()); };
            kn->slider.setDoubleClickReturnValue (true, knobDefaultNorm (type, k));
            addAndMakeVisible (*kn);
            knobs.push_back (std::move (kn));
        }
        for (int m = 0; m < (int) d.modes.size(); ++m)
        {
            auto* b = modeButtons.add (new TextButton (d.modes[(size_t) m]));
            b->getProperties().set ("keepCase", true);
            b->getProperties().set ("radius", 9.0);
            b->onClick = [this, m] { setParam (proc.apvts, SIRackProcessor::pid (slot, "mode"), (float) m); };
            addAndMakeVisible (b);
        }
        changeBtn.setButtonText (type == 0 ? "Add module" : "Change module");
        removeBtn.setVisible (type != 0);
        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto& d = moduleDef (type);
        const Colour tint (d.tint);
        g.setGradientFill (ColourGradient (tint.withMultipliedBrightness (0.6f).withAlpha (0.95f), 0, r.getY(), Colour (0xff0e0e11), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 18.0f);
        for (int i = 0; i < (int) r.getHeight(); i += 3)
        {
            g.setColour (Colours::white.withAlpha (i % 6 == 0 ? 0.012f : 0.006f));
            g.fillRect (r.getX() + 12.0f, r.getY() + (float) i, r.getWidth() - 24.0f, 1.0f);
        }
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 1.0f);
        for (auto pt : { Point<float> (16, 16), Point<float> (r.getWidth() - 16, 16), Point<float> (16, r.getHeight() - 16), Point<float> (r.getWidth() - 16, r.getHeight() - 16) })
        {
            g.setColour (Colour (0xff2a2a2e));
            g.fillEllipse (Rectangle<float> (10, 10).withCentre (pt));
            g.setColour (Colours::black.withAlpha (0.6f));
            g.drawLine (pt.x - 3.5f, pt.y, pt.x + 3.5f, pt.y, 1.2f);
        }
        if (type == 0)
        {
            g.setColour (col::text);
            g.setFont (si::font (18.0f, Weight::Bold));
            g.drawText ("Slot " + String (slot + 1) + " is empty", r.withTrimmedBottom (60.0f), Justification::centred);
            g.setColour (col::label);
            g.setFont (si::font (12.0f));
            g.drawText ("Add a module to start the chain - modules run left to right", r.withTrimmedTop (10.0f), Justification::centred);
            return;
        }
        g.setColour (col::text);
        g.setFont (si::font (30.0f, Weight::ExtraBold, -0.01f));
        g.drawText (d.name, Rectangle<float> (36, 26, 420, 36), Justification::centredLeft);
        g.setColour (tint.brighter (0.9f));
        g.setFont (si::font (9.5f, Weight::Bold, 0.2f));
        g.drawText ("SLOT " + String (slot + 1) + "  -  " + String (d.category).toUpperCase(), Rectangle<float> (37, 62, 420, 14), Justification::centredLeft);
        g.setColour (Colours::white.withAlpha (0.65f));
        g.setFont (si::font (11.5f));
        g.drawText (d.blurb, Rectangle<float> (37, 80, r.getWidth() - 74, 16), Justification::centredLeft, true);
        if (d.hasGR)
        {
            auto m = grArea().toFloat();
            g.setColour (col::label);
            g.setFont (si::font (9.5f, Weight::Bold, 0.15f));
            g.drawText ("GAIN REDUCTION", m.withHeight (12.0f).translated (0.0f, -18.0f), Justification::centredLeft);
            g.setColour (col::text);
            g.setFont (si::font (13.0f, Weight::Bold));
            g.drawText (String (grSm, 1) + " dB", m.withHeight (14.0f).translated (0.0f, -20.0f), Justification::centredRight);
            drawGrMeter (g, m, grSm, true);
        }
    }

    void resized() override
    {
        changeBtn.setBounds (getWidth() - 176, 28, 140, 32);
        removeBtn.setBounds (getWidth() - 280, 28, 96, 32);
        const int n = (int) knobs.size();
        const int kw = n > 4 ? 140 : 160;
        int x = (getWidth() - n * kw) / 2;
        for (auto& k : knobs) { k->setBounds (x, 120, kw, 176); x += kw; }
        int mx = 36;
        for (auto* b : modeButtons) { b->setBounds (mx, getHeight() - 58, 120, 32); mx += 128; }
    }

private:
    Rectangle<int> grArea() const { return { getWidth() - 336, getHeight() - 52, 300, 12 }; }

    void timerCallback() override
    {
        if (proc.slotType (slot) != type) rebuild();
        const int mode = (int) pv (proc.apvts, SIRackProcessor::pid (slot, "mode"));
        for (int i = 0; i < modeButtons.size(); ++i) modeButtons[i]->setToggleState (i == mode, dontSendNotification);
        grSm += (proc.gr[(size_t) slot].load() - grSm) * 0.35f;
        if (moduleDef (type).hasGR) repaint (grArea().expanded (4, 26));
    }

    SIRackProcessor& proc;
    int slot = 0, type = 0;
    float grSm = 0.0f;
    std::vector<std::unique_ptr<si::Knob>> knobs;
    OwnedArray<TextButton> modeButtons;
    TextButton changeBtn { "Change module" }, removeBtn { "Remove" };
};

//============================================================================== root
class Root : public si::ScaledContent, private Timer
{
public:
    explicit Root (SIRackProcessor& p) : si::ScaledContent (W, H), proc (p), s (p.apvts), presetBar (p.presets), plate (p)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &plate, &inK, &outK, &sc }) addAndMakeVisible (c);
        sc.setTooltip ("External sidechain: FET 76, Opto 2A, Bus VCA and Gate listen to the Sidechain input (in Ableton pick a track in the plugin's Sidechain panel)");
        inK.slider.setTooltip ("Input trim into the rack");
        outK.slider.setTooltip ("Output trim after the rack");
        for (int i = 0; i < kSlots; ++i)
        {
            auto* t = tiles.add (new SlotTile (p, i));
            t->onSelect = [this] (int sl) { closeBrowser(); select (sl); };
            t->onAdd = [this] (int sl) { select (sl); openBrowser(); };
            t->slotAt = [this] (Point<int> pt) { for (int k = 0; k < tiles.size(); ++k) if (tiles[k]->getBounds().contains (pt)) return k; return -1; };
            t->onDragStart = [this] (int sl) { dragFrom = sl; };
            t->onDragTo = [this] (int to)
            {
                if (dragFrom < 0 || to == dragFrom) return;
                // move one step at a time so the other modules keep their order
                const int dir = to > dragFrom ? 1 : -1;
                for (int k = dragFrom; k != to; k += dir) proc.swapSlots (k, k + dir);
                dragFrom = to;
                select (to);
            };
            addAndMakeVisible (t);
        }
        browser.onPick = [this] (int type) { proc.loadModule (selected, type); closeBrowser(); select (selected); };
        browser.onClose = [this] { closeBrowser(); };
        plate.onBrowse = [this] { openBrowser(); };
        addChildComponent (browser);
        p.presets.onChange = [this] { presetBar.refresh(); select (0); };
        select (0);
        resized();
        startTimerHz (20);
    }
    ~Root() override { proc.presets.onChange = nullptr; }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 270, 44 }, "SI Rack", "Virtual Mix Rack");
        si::drawPanel (g, { 24, 80, 1052, 132 }, "Rack");
        drawIo (g, inMeter(), "IN", inSm);
        drawIo (g, outMeter(), "OUT", outSm);
        // flow arrows between the tiles
        g.setColour (col::hint);
        for (int i = 0; i + 1 < tiles.size(); ++i)
        {
            const auto a = tiles[i]->getBounds(), b = tiles[i + 1]->getBounds();
            const float x = (a.getRight() + b.getX()) * 0.5f, y = (float) a.getCentreY();
            Path t;
            t.addTriangle (x - 2.5f, y - 4.0f, x - 2.5f, y + 4.0f, x + 3.0f, y);
            g.fillPath (t);
        }
    }

    void resized() override
    {
        presetBar.setBounds (306, 20, 300, 38);
        sc.setBounds (620, 24, 60, 30);
        inK.setBounds (700, 2, 76, 76);
        outK.setBounds (868, 2, 76, 76);
        for (int i = 0; i < tiles.size(); ++i) tiles[i]->setBounds (40 + i * 128, 110, 116, 90);
        plate.setBounds (24, 226, 1052, 410);
        browser.setBounds (plate.getBounds());
    }

private:
    Rectangle<float> inMeter() const { return { 782, 14, 10, 56 }; }
    Rectangle<float> outMeter() const { return { 950, 14, 10, 56 }; }

    static void drawIo (Graphics& g, Rectangle<float> m, const String& label, float level)
    {
        si::drawLevelMeter (g, m, level);
        g.setColour (col::hint);
        g.setFont (si::font (8.5f, Weight::Bold, 0.12f));
        g.drawText (label, Rectangle<float> (m.getRight() + 4.0f, m.getY(), 30.0f, 10.0f), Justification::centredLeft);
        const float db = 20.0f * std::log10 (jmax (1e-5f, level));
        g.setColour (col::label);
        g.drawText (level > 1e-4f ? String (db, 1) : String ("-inf"), Rectangle<float> (m.getRight() + 4.0f, m.getBottom() - 10.0f, 40.0f, 10.0f), Justification::centredLeft);
    }

    void openBrowser() { browser.current = proc.slotType (selected); browser.setVisible (true); browser.toFront (false); browser.repaint(); }
    void closeBrowser() { browser.setVisible (false); }

    void select (int sl)
    {
        selected = sl;
        for (int i = 0; i < tiles.size(); ++i) { tiles[i]->selected = i == sl; tiles[i]->repaint(); }
        plate.setSlot (sl);
    }

    void timerCallback() override
    {
        for (auto* t : tiles) t->refresh();
        inSm = jmax (proc.inLevel.load(), inSm * 0.85f);
        outSm = jmax (proc.outLevel.load(), outSm * 0.85f);
        repaint (inMeter().toNearestInt().expanded (2, 2).withWidth (60));
        repaint (outMeter().toNearestInt().expanded (2, 2).withWidth (60));
    }

    SIRackProcessor& proc;
    si::APVTS& s;
    si::PresetBar presetBar;
    Faceplate plate;
    ModuleBrowser browser;
    OwnedArray<SlotTile> tiles;
    si::Knob inK { s, "input", "Input", true }, outK { s, "output", "Output", true };
    si::Toggle sc { s, "extsc", "SC" };
    int selected = 0, dragFrom = -1;
    float inSm = 0.0f, outSm = 0.0f;
};
} // namespace

SIRackEditor::SIRackEditor (SIRackProcessor& p) : AudioProcessorEditor (p)
{
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (p);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 3 / 2, H * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    si::uiSelfTest (*root, "SI Rack");
}

SIRackEditor::~SIRackEditor() { setLookAndFeel (nullptr); }

void SIRackEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
