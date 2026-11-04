#include "PluginEditor.h"

using namespace juce;
using namespace si::sat;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 1000, H = 660;
const Colour kModCol[kModSources] = { Colour (0xff34d399), Colour (0xff22d3ee), Colour (0xff60a5fa), Colour (0xffa78bfa) };
const char* kBandIds[16] = { "on", "mute", "solo", "style", "drive", "dpan", "mix", "fb", "fbfreq", "dyn", "bass", "mid", "treble", "pres", "level", "lpan" };
const char* kModTypeNames[] = { "None", "XLFO", "Envelope", "Follower", "Transients", "Slider" };

float pv (si::APVTS& s, const String& id) { return s.getRawParameterValue (id)->load(); }
void setParam (si::APVTS& s, const String& id, float v)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}
int targetIndex (int band, int p) { return 1 + band * 10 + p; }   // p: 0 drive 1 mix 2 fb 3 fbfreq 4 dyn 5 bass 6 mid 7 treble 8 pres 9 level

//============================================================================== band split / remove
struct BandOps
{
    static void read (SISaturnProcessor& p, int& nb, std::vector<std::pair<float, int>>& xo, std::vector<std::array<float, 16>>& vals)
    {
        auto& s = p.apvts;
        nb = (int) pv (s, "bands") + 1;
        xo.clear();
        for (int i = 0; i < nb - 1; ++i) xo.push_back ({ pv (s, "xo" + String (i + 1)), (int) pv (s, "sl" + String (i + 1)) });
        std::sort (xo.begin(), xo.end());
        vals.assign ((size_t) nb, {});
        for (int b = 0; b < nb; ++b)
            for (int k = 0; k < 16; ++k) vals[(size_t) b][(size_t) k] = pv (s, SISaturnProcessor::bid (b, kBandIds[k]));
    }
    static void write (SISaturnProcessor& p, const std::vector<std::pair<float, int>>& xo, const std::vector<std::array<float, 16>>& vals)
    {
        auto& s = p.apvts;
        for (int b = 0; b < (int) vals.size(); ++b)
            for (int k = 0; k < 16; ++k) setParam (s, SISaturnProcessor::bid (b, kBandIds[k]), vals[(size_t) b][(size_t) k]);
        for (int i = 0; i < (int) xo.size(); ++i)
        {
            setParam (s, "xo" + String (i + 1), xo[(size_t) i].first);
            setParam (s, "sl" + String (i + 1), (float) xo[(size_t) i].second);
        }
        setParam (s, "bands", (float) vals.size() - 1);
    }
    static void remapSlots (SISaturnProcessor& p, int band, bool inserted)
    {
        auto& s = p.apvts;
        for (int j = 0; j < kModSlots; ++j)
        {
            const int t = (int) pv (s, SISaturnProcessor::sid (j, "tgt"));
            if (t < 1 || t > kMaxBands * 10) continue;
            const int b = (t - 1) / 10;
            int nt = t;
            if (inserted && b > band) nt = b + 1 < kMaxBands ? t + 10 : 0;
            if (! inserted && b == band) nt = 0;
            if (! inserted && b > band) nt = t - 10;
            if (nt != t) setParam (s, SISaturnProcessor::sid (j, "tgt"), (float) nt);
        }
    }
    static int split (SISaturnProcessor& p, int band, float freq)
    {
        int nb; std::vector<std::pair<float, int>> xo; std::vector<std::array<float, 16>> vals;
        read (p, nb, xo, vals);
        if (nb >= kMaxBands) return band;
        xo.insert (xo.begin() + band, { freq, 2 });
        vals.insert (vals.begin() + band + 1, vals[(size_t) band]);   // new band copies the one that was split
        remapSlots (p, band, true);
        write (p, xo, vals);
        return band + 1;
    }
    static int remove (SISaturnProcessor& p, int band)
    {
        int nb; std::vector<std::pair<float, int>> xo; std::vector<std::array<float, 16>> vals;
        read (p, nb, xo, vals);
        if (nb <= 1) return 0;
        xo.erase (xo.begin() + (band == nb - 1 ? band - 1 : band));
        vals.erase (vals.begin() + band);
        remapSlots (p, band, false);
        write (p, xo, vals);
        return jmax (0, band - 1);
    }
};

//============================================================================== floating band panel
class BandPanel : public Component
{
public:
    explicit BandPanel (SISaturnProcessor& p) : proc (p)
    {
        addAndMakeVisible (removeBtn);
        removeBtn.getProperties().set ("radius", 6.0);
        removeBtn.setTooltip ("Remove band");
        styleBox.setTooltip ("Distortion style");
        for (int i = 0; i < 4; ++i)
        {
            tone[i].setSliderStyle (Slider::LinearVertical);
            tone[i].setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            tone[i].setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (tone[i]);
        }
    }
    std::function<void()> onRemove;

    void setBand (int b)
    {
        band = b;
        auto& s = proc.apvts;
        toneAtt.clear();
        onBtn = std::make_unique<si::Toggle> (s, SISaturnProcessor::bid (b, "on"), "", si::Icon::Power);
        onBtn->getProperties().set ("radius", 6.0);
        onBtn->setTooltip ("Band on / bypass");
        mix = std::make_unique<si::Knob> (s, SISaturnProcessor::bid (b, "mix"), "Mix");
        fb = std::make_unique<si::Knob> (s, SISaturnProcessor::bid (b, "fb"), "Feedback");
        fbf = std::make_unique<si::Knob> (s, SISaturnProcessor::bid (b, "fbfreq"), "Freq");
        dyn = std::make_unique<si::Knob> (s, SISaturnProcessor::bid (b, "dyn"), "Dynamics", true);
        drive = std::make_unique<si::Knob> (s, SISaturnProcessor::bid (b, "drive"), "Drive");
        dpan = std::make_unique<si::Knob> (s, SISaturnProcessor::bid (b, "dpan"), "Pan", true);
        level = std::make_unique<si::Knob> (s, SISaturnProcessor::bid (b, "level"), "Level", true);
        dyn->slider.setTooltip ("Right: pumping compression. Left: gating / expansion.");
        fb->slider.setTooltip ("Feeds the distorted band back into itself; Freq sets the ringing frequency.");
        const int tp[] = { 0, 1, 2, 3, 4 };
        ignoreUnused (tp);
        mix->slider.getProperties().set ("modTarget", targetIndex (b, 1));
        fb->slider.getProperties().set ("modTarget", targetIndex (b, 2));
        fbf->slider.getProperties().set ("modTarget", targetIndex (b, 3));
        dyn->slider.getProperties().set ("modTarget", targetIndex (b, 4));
        drive->slider.getProperties().set ("modTarget", targetIndex (b, 0));
        level->slider.getProperties().set ("modTarget", targetIndex (b, 9));
        const char* toneIds[4] = { "bass", "mid", "treble", "pres" };
        for (int i = 0; i < 4; ++i)
        {
            toneAtt.push_back (std::make_unique<si::APVTS::SliderAttachment> (s, SISaturnProcessor::bid (b, toneIds[i]), tone[i]));
            tone[i].getProperties().set ("modTarget", targetIndex (b, 5 + i));
        }
        styleAtt.reset();
        styleBox.clear (dontSendNotification);
        String cat;
        for (int st = 0; st < kNumStyles; ++st)
        {
            if (cat != styleCategory (st)) { cat = styleCategory (st); styleBox.addSectionHeading (cat); }
            styleBox.addItem (styleName (st), st + 1);
        }
        styleAtt = std::make_unique<si::APVTS::ComboBoxAttachment> (s, SISaturnProcessor::bid (b, "style"), styleBox);
        addAndMakeVisible (styleBox);
        for (Component* c : std::initializer_list<Component*> { onBtn.get(), mix.get(), fb.get(), fbf.get(), dyn.get(), drive.get(), dpan.get(), level.get() })
            addAndMakeVisible (c);
        removeBtn.onClick = [this] { if (onRemove) onRemove(); };
        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 4.0f), 14.0f);
        g.setColour (Colour (0xf2121216));
        g.fillRoundedRectangle (r, 14.0f);
        g.setGradientFill (ColourGradient (col::accent.withAlpha (0.18f), 0, r.getY(), col::accent.withAlpha (0.0f), 0, r.getY() + 40.0f, false));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        g.setColour (col::accent);
        g.fillRoundedRectangle (r.getCentreX() - 30.0f, r.getY(), 60.0f, 2.0f, 1.0f);
        // tone labels
        const char* t[4] = { "BASS", "MID", "TREB", "PRES" };
        g.setFont (si::font (8.0f, Weight::Bold, 0.08f));
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (col::label);
            g.drawText (t[i], tone[i].getBounds().withY (tone[i].getBottom() + 2).withHeight (10).expanded (6, 0), Justification::centred);
        }
        g.setColour (col::hint);
        g.drawText ("TONE", Rectangle<int> (toneX, 34, 4 * 24, 10), Justification::centred);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRect ((float) toneX - 8.0f, 44.0f, 1.0f, 88.0f);
        g.fillRect ((float) toneX + 4 * 24 + 6.0f, 44.0f, 1.0f, 88.0f);
    }

    void resized() override
    {
        if (onBtn == nullptr) return;
        onBtn->setBounds (10, 8, 28, 24);
        styleBox.setBounds (44, 8, 180, 24);
        removeBtn.setBounds (getWidth() - 34, 8, 24, 24);
        Rectangle<int> k (8, 40, 4 * 62, 96);
        for (auto* kn : { mix.get(), fb.get(), fbf.get(), dyn.get() }) kn->setBounds (k.removeFromLeft (62).reduced (2, 0));
        drive->setBounds (258, 30, 104, 110);
        dpan->setBounds (362, 70, 52, 66);
        toneX = 418;
        for (int i = 0; i < 4; ++i) tone[i].setBounds (toneX + i * 24, 46, 24, 78);
        level->setBounds (522, 40, 62, 96);
    }

    Slider tone[4];
private:
    SISaturnProcessor& proc;
    int band = 0, toneX = 426;
    std::unique_ptr<si::Toggle> onBtn;
    std::unique_ptr<si::Knob> mix, fb, fbf, dyn, drive, dpan, level;
    ComboBox styleBox;
    std::unique_ptr<si::APVTS::ComboBoxAttachment> styleAtt;
    std::vector<std::unique_ptr<si::APVTS::SliderAttachment>> toneAtt;
    TextButton removeBtn { "x" };
};

//============================================================================== main display
class SaturnDisplay : public Component, private Timer
{
public:
    explicit SaturnDisplay (SISaturnProcessor& p) : proc (p), panel (p)
    {
        addAndMakeVisible (panel);
        panel.onRemove = [this] { select (BandOps::remove (proc, selected)); };
        startTimerHz (30);
    }
    std::function<void (int)> onSelect;
    int selected = 0;

    void select (int b)
    {
        selected = jlimit (0, kMaxBands - 1, b);
        panel.setBand (selected);
        placePanel();
        if (onSelect) onSelect (selected);
        repaint();
    }

    void resized() override
    {
        plot = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        placePanel();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff0b0a0c));
        g.fillRoundedRectangle (r, 14.0f);
        const auto s = proc.readSettings();
        const int nb = s.bands;
        const auto xs = sortedXo (s);

        // selected band region
        {
            auto reg = bandRect (selected, nb, xs);
            g.setGradientFill (ColourGradient (col::accent.withAlpha (0.16f), 0, reg.getY(), col::accent.withAlpha (0.03f), 0, reg.getBottom(), false));
            g.fillRect (reg);
            g.setColour (col::accent.withAlpha (0.8f));
            g.fillRect (reg.getX(), reg.getY(), reg.getWidth(), 2.0f);
        }

        // analyzer
        Path spec;
        const int N = 220;
        for (int i = 0; i <= N; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / N);
            const float y = jlimit (plot.getY(), plot.getBottom(), plot.getBottom() - plot.getHeight() * (spectrum.dbAt (f) + 84.0f) / 90.0f);
            i == 0 ? spec.startNewSubPath (fx (f), y) : spec.lineTo (fx (f), y);
        }
        Path fill (spec);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (ColourGradient (col::accent.withAlpha (0.28f), 0, plot.getY(), col::deep.withAlpha (0.06f), 0, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (col::accentHi.withAlpha (0.55f));
        g.strokePath (spec, PathStrokeType (1.1f));

        // frequency labels
        g.setFont (si::font (9.0f, Weight::Medium));
        g.setColour (col::hint);
        for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
            g.drawText (f >= 1000.f ? String ((int) (f / 1000)) + "k" : String ((int) f), Rectangle<float> (fx (f) - 16.0f, plot.getBottom() - 12.0f, 32.0f, 10.0f), Justification::centred);

        // crossovers + slope buttons
        for (int i = 0; i < nb - 1; ++i)
        {
            const float x = fx (xs[(size_t) i].first);
            const bool hot = i == hoverXo || i == dragXo;
            g.setColour (Colours::white.withAlpha (hot ? 0.8f : 0.28f));
            g.fillRect (x - 0.5f, plot.getY(), 1.0f, plot.getHeight());
            auto sb = slopeRect (x);
            g.setColour (Colour (0xff1c1c20));
            g.fillRoundedRectangle (sb, 4.0f);
            g.setColour (Colours::white.withAlpha (0.25f));
            g.drawRoundedRectangle (sb, 4.0f, 1.0f);
            const char* sl[] = { "6", "12", "24", "48" };
            g.setColour (col::text);
            g.setFont (si::font (8.5f, Weight::Bold));
            g.drawText (sl[jlimit (0, 3, xs[(size_t) i].second)], sb, Justification::centred);
            if (hot)
            {
                const float f = xs[(size_t) i].first;
                g.setFont (si::font (10.0f, Weight::SemiBold));
                g.drawText (f >= 1000.f ? String (f / 1000.f, 2) + " kHz" : String (roundToInt (f)) + " Hz", Rectangle<float> (x - 40.0f, sb.getBottom() + 4.0f, 80.0f, 14.0f), Justification::centred);
            }
        }

        // per band: label, solo/mute, level handle, activity
        for (int b = 0; b < nb; ++b)
        {
            const auto& bs = s.band[(size_t) b];
            auto reg = bandRect (b, nb, xs);
            g.setColour (bs.on ? (b == selected ? col::text : col::label) : col::hint);
            g.setFont (si::font (10.0f, Weight::SemiBold));
            g.drawText (bs.on ? styleName (bs.style) : "Bypassed", reg.reduced (8.0f, 6.0f).withHeight (14.0f).translated (nb > 1 ? 40.0f : 0.0f, 0.0f), Justification::centredLeft, true);
            if (nb > 1 && (mouseInside || bs.solo || bs.mute))
            {
                auto sr = soloRect (reg), mr = muteRect (reg);
                g.setColour (bs.solo ? col::warn : Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (sr, 3.0f);
                g.setColour (bs.mute ? col::rec : Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (mr, 3.0f);
                g.setColour (bs.solo ? col::bg : col::label);
                g.setFont (si::font (8.5f, Weight::Bold));
                g.drawText ("S", sr, Justification::centred);
                g.setColour (bs.mute ? col::onAccent : col::label);
                g.drawText ("M", mr, Justification::centred);
            }
            const float act = proc.engine.activity[(size_t) b].load();
            g.setColour (col::accent.withAlpha (0.25f + 0.6f * act));
            g.fillRect (reg.getX() + 2.0f, reg.getBottom() - 2.0f, (reg.getWidth() - 4.0f) * (0.15f + 0.85f * act), 2.0f);

            auto h = handleRect (b, nb, xs, bs.levelDb);
            g.setGradientFill (ColourGradient (b == selected ? Colours::white : Colour (0xffd4d4d8), 0, h.getY(), Colour (0xff8a8a90), 0, h.getBottom(), false));
            g.fillRoundedRectangle (h, 4.0f);
            if (b == selected)
            {
                g.setColour (col::accent);
                g.drawRoundedRectangle (h.expanded (1.5f), 5.0f, 1.5f);
            }
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillRect (h.getCentreX() - 10.0f, h.getCentreY() - 0.5f, 20.0f, 1.0f);
            if (b == hoverHandle || b == dragHandle)
            {
                g.setColour (col::text);
                g.setFont (si::font (9.5f, Weight::SemiBold));
                g.drawText (altDrag ? "Drive " + String (bs.driveDb / 0.36f, 1) + "%" : "Level " + String (bs.levelDb, 1) + " dB",
                            h.withY (h.getY() - 18.0f).withHeight (14.0f).expanded (40.0f, 0.0f), Justification::centred);
            }
        }

        // split "+"
        if (mouseInside && nb < kMaxBands && mousePos.y < plot.getY() + 24.0f && hitXo (mousePos.x) < 0)
        {
            auto pc = Rectangle<float> (18.0f, 18.0f).withCentre ({ mousePos.x, plot.getY() + 12.0f });
            g.setColour (col::accent);
            g.fillEllipse (pc);
            g.setColour (col::onAccent);
            g.fillRect (pc.getCentreX() - 4.0f, pc.getCentreY() - 0.75f, 8.0f, 1.5f);
            g.fillRect (pc.getCentreX() - 0.75f, pc.getCentreY() - 4.0f, 1.5f, 8.0f);
        }
        if (nb == 1 && ! mouseInside)
        {
            g.setColour (col::hint);
            g.setFont (si::font (10.5f));
            g.drawText ("One band = saturation on the whole signal.  Hover the top edge and click + to split into bands.",
                        plot.withHeight (16.0f).translated (0.0f, 26.0f), Justification::centred);
        }
    }

    // ---- mouse
    void mouseMove (const MouseEvent& e) override
    {
        mousePos = e.position;
        mouseInside = true;
        const auto s = proc.readSettings();
        const auto xs = sortedXo (s);
        hoverXo = hitXo (e.position.x);
        hoverHandle = hitHandle (e.position, s, xs);
        altDrag = e.mods.isAltDown();
        setMouseCursor (hoverXo >= 0 ? MouseCursor::LeftRightResizeCursor : (hoverHandle >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor));
        repaint();
    }
    void mouseExit (const MouseEvent&) override { mouseInside = false; hoverXo = hoverHandle = -1; repaint(); }

    void mouseDown (const MouseEvent& e) override
    {
        const auto s = proc.readSettings();
        const auto xs = sortedXo (s);
        const int nb = s.bands;
        // slope buttons
        for (int i = 0; i < nb - 1; ++i)
            if (slopeRect (fx (xs[(size_t) i].first)).contains (e.position))
            {
                const int param = paramOfXo (s, xs[(size_t) i].first);
                PopupMenu m;
                const char* sl[] = { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
                for (int k = 0; k < 4; ++k) m.addItem (k + 1, sl[k], true, xs[(size_t) i].second == k);
                m.showMenuAsync (PopupMenu::Options().withMousePosition(), [this, param] (int r) { if (r > 0) setParam (proc.apvts, "sl" + String (param + 1), (float) (r - 1)); });
                return;
            }
        // solo / mute
        for (int b = 0; b < nb && nb > 1; ++b)
        {
            auto reg = bandRect (b, nb, xs);
            if (soloRect (reg).contains (e.position)) { setParam (proc.apvts, SISaturnProcessor::bid (b, "solo"), s.band[(size_t) b].solo ? 0.0f : 1.0f); return; }
            if (muteRect (reg).contains (e.position)) { setParam (proc.apvts, SISaturnProcessor::bid (b, "mute"), s.band[(size_t) b].mute ? 0.0f : 1.0f); return; }
        }
        // crossover drag
        dragXo = hitXo (e.position.x);
        if (dragXo >= 0) { dragParam = paramOfXo (s, xs[(size_t) dragXo].first); return; }
        // level handle
        dragHandle = hitHandle (e.position, s, xs);
        if (dragHandle >= 0)
        {
            if (dragHandle != selected) select (dragHandle);
            startLevel = s.band[(size_t) dragHandle].levelDb;
            startDrive = s.band[(size_t) dragHandle].driveDb;
            startLo = dragHandle > 0 ? xs[(size_t) dragHandle - 1].first : 0.0f;
            startHi = dragHandle < nb - 1 ? xs[(size_t) dragHandle].first : 0.0f;
            altDrag = e.mods.isAltDown();
            return;
        }
        // split
        if (nb < kMaxBands && e.position.y < plot.getY() + 24.0f)
        {
            const float f = xf (e.position.x);
            select (BandOps::split (proc, bandAt (e.position.x, nb, xs), jlimit (25.0f, 18000.0f, f)));
            return;
        }
        select (bandAt (e.position.x, nb, xs));
    }

    void mouseDrag (const MouseEvent& e) override
    {
        auto& st = proc.apvts;
        if (dragXo >= 0)
        {
            setParam (st, "xo" + String (dragParam + 1), jlimit (25.0f, 18000.0f, xf (e.position.x)));
            repaint();
            return;
        }
        if (dragHandle < 0) return;
        const float dyPx = (float) e.getDistanceFromDragStartY();
        const float dxPx = (float) e.getDistanceFromDragStartX();
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        if (std::abs (dyPx) > std::abs (dxPx) || (startLo <= 0.0f && startHi <= 0.0f))
        {
            const float ddb = -dyPx / (plot.getHeight() * 0.42f) * 24.0f * fine;
            if (altDrag) setParam (st, SISaturnProcessor::bid (dragHandle, "drive"), jlimit (0.0f, 100.0f, (startDrive + ddb) / 0.36f));
            else setParam (st, SISaturnProcessor::bid (dragHandle, "level"), jlimit (-36.0f, 24.0f, startLevel + ddb));
        }
        else
        {
            // horizontal: move both crossovers of this band
            const float ratio = std::pow (1000.0f, dxPx * fine / plot.getWidth());
            const auto s = proc.readSettings();
            if (startLo > 0.0f) setParam (st, "xo" + String (paramOfXo (s, curLo (s)) + 1), jlimit (25.0f, 18000.0f, startLo * ratio));
            if (startHi > 0.0f) setParam (st, "xo" + String (paramOfXo (s, curHi (s)) + 1), jlimit (25.0f, 18000.0f, startHi * ratio));
        }
        repaint();
    }
    void mouseUp (const MouseEvent&) override { dragXo = dragHandle = -1; }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const auto s = proc.readSettings();
        const int b = bandAt (e.position.x, s.bands, sortedXo (s));
        const String id = SISaturnProcessor::bid (b, e.mods.isAltDown() ? "drive" : "level");
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.05f));
            p->endChangeGesture();
        }
    }

private:
    static std::vector<std::pair<float, int>> sortedXo (const Settings& s)
    {
        std::vector<std::pair<float, int>> xs;
        for (int i = 0; i < s.bands - 1; ++i) xs.push_back ({ s.xo[(size_t) i], s.slope[(size_t) i] });
        std::sort (xs.begin(), xs.end());
        return xs;
    }
    static int paramOfXo (const Settings& s, float f)
    {
        for (int i = 0; i < s.bands - 1; ++i) if (std::abs (s.xo[(size_t) i] - f) < 0.001f) return i;
        return 0;
    }
    float curLo (const Settings& s) const { const auto xs = sortedXo (s); return dragHandle > 0 ? xs[(size_t) dragHandle - 1].first : 0.0f; }
    float curHi (const Settings& s) const { const auto xs = sortedXo (s); return dragHandle < s.bands - 1 ? xs[(size_t) dragHandle].first : 0.0f; }
    float fx (float f) const { return plot.getX() + plot.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
    float xf (float x) const { return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth())); }
    float levelY (float db) const { return plot.getCentreY() - 30.0f - jlimit (-36.0f, 24.0f, db) / 24.0f * plot.getHeight() * 0.36f; }
    Rectangle<float> bandRect (int b, int nb, const std::vector<std::pair<float, int>>& xs) const
    {
        const float x0 = b == 0 ? plot.getX() : fx (xs[(size_t) b - 1].first);
        const float x1 = b >= nb - 1 ? plot.getRight() : fx (xs[(size_t) b].first);
        return { x0, plot.getY(), x1 - x0, plot.getHeight() };
    }
    Rectangle<float> handleRect (int b, int nb, const std::vector<std::pair<float, int>>& xs, float levelDb) const
    {
        auto reg = bandRect (b, nb, xs);
        return Rectangle<float> (jmin (56.0f, reg.getWidth() - 8.0f), 10.0f).withCentre ({ reg.getCentreX(), levelY (levelDb) });
    }
    Rectangle<float> slopeRect (float x) const { return Rectangle<float> (22.0f, 14.0f).withCentre ({ x, plot.getY() + plot.getHeight() * 0.18f }); }
    Rectangle<float> soloRect (Rectangle<float> reg) const { return { reg.getX() + 6.0f, reg.getY() + 6.0f, 15.0f, 14.0f }; }
    Rectangle<float> muteRect (Rectangle<float> reg) const { return { reg.getX() + 24.0f, reg.getY() + 6.0f, 15.0f, 14.0f }; }
    int bandAt (float x, int nb, const std::vector<std::pair<float, int>>& xs) const
    {
        int b = 0;
        for (int i = 0; i < nb - 1; ++i) if (x > fx (xs[(size_t) i].first)) b = i + 1;
        return b;
    }
    int hitXo (float x) const
    {
        const auto s = proc.readSettings();
        const auto xs = sortedXo (s);
        for (int i = 0; i < s.bands - 1; ++i) if (std::abs (fx (xs[(size_t) i].first) - x) < 5.0f) return i;
        return -1;
    }
    int hitHandle (Point<float> p, const Settings& s, const std::vector<std::pair<float, int>>& xs) const
    {
        for (int b = 0; b < s.bands; ++b)
            if (handleRect (b, s.bands, xs, s.band[(size_t) b].levelDb).expanded (4.0f, 6.0f).contains (p)) return b;
        return -1;
    }
    void placePanel()
    {
        const auto s = proc.readSettings();
        auto reg = bandRect (jmin (selected, s.bands - 1), s.bands, sortedXo (s));
        const int pw = 590, ph = 146;
        const int x = jlimit (8, getWidth() - pw - 8, (int) reg.getCentreX() - pw / 2);
        panel.setBounds (x, getHeight() - ph - 22, pw, ph);
    }
    void timerCallback() override
    {
        spectrum.update (proc.fifo, proc.getRate());
        const auto s = proc.readSettings();
        if (selected >= s.bands) select (s.bands - 1);
        if (s.bands != lastBands || s.xo != lastXo) { lastBands = s.bands; lastXo = s.xo; placePanel(); }
        repaint();
    }

    SISaturnProcessor& proc;
    BandPanel panel;
    si::Spectrum spectrum;
    Rectangle<float> plot;
    Point<float> mousePos;
    bool mouseInside = false, altDrag = false;
    int hoverXo = -1, dragXo = -1, dragParam = 0, hoverHandle = -1, dragHandle = -1, lastBands = -1;
    std::array<float, kMaxBands - 1> lastXo {};
    float startLevel = 0, startDrive = 0, startLo = 0, startHi = 0;
};

//============================================================================== modulation tiles
class ModTile : public Component, public SettableTooltipClient
{
public:
    ModTile (SISaturnProcessor& p, int k) : proc (p), index (k) { setTooltip ("Drag the coloured dot onto any knob to modulate it"); }
    std::function<void (int)> onSelect;
    bool selected = false;

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto type = (ModType) (int) pv (proc.apvts, SISaturnProcessor::mid (index, "type"));
        const auto c = kModCol[index];
        g.setColour (Colours::white.withAlpha (selected ? 0.08f : 0.04f));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (selected ? c.withAlpha (0.8f) : Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, selected ? 1.5f : 1.0f);
        if (type == ModType::None)
        {
            g.setColour (col::hint);
            g.setFont (si::font (26.0f, Weight::Medium));
            g.drawText ("+", r.withTrimmedBottom (14.0f), Justification::centred);
            g.setFont (si::font (9.0f, Weight::Bold, 0.1f));
            g.drawText ("ADD SOURCE", r.withTrimmedTop (r.getHeight() * 0.62f), Justification::centredTop);
            return;
        }
        // drag dot
        auto dot = dotRect();
        g.setColour (c);
        g.fillEllipse (dot);
        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawEllipse (dot.reduced (1.5f), 1.0f);
        g.setColour (col::text);
        g.setFont (si::font (10.0f, Weight::Bold));
        g.drawText (String (kModTypeNames[(int) type]) + " " + String (index + 1), r.reduced (26.0f, 0.0f).withHeight (28.0f), Justification::centredLeft);

        // graphic
        auto gr = r.reduced (12.0f).withTrimmedTop (22.0f).withTrimmedBottom (14.0f);
        const auto sp = proc.readSource (index);
        Path p;
        for (int i = 0; i <= 60; ++i)
        {
            const float t = (float) i / 60.0f;
            float v = 0.0f;
            switch (type)
            {
                case ModType::XLFO:
                    switch (sp.shape)
                    {
                        case 0: v = 0.5f + 0.5f * std::sin (t * 12.566f); break;
                        case 1: v = 1.0f - std::abs (std::fmod (t * 2.0f, 1.0f) * 2.0f - 1.0f); break;
                        case 2: v = std::fmod (t * 2.0f, 1.0f); break;
                        case 3: v = 1.0f - std::fmod (t * 2.0f, 1.0f); break;
                        case 4: v = std::fmod (t * 2.0f, 1.0f) < 0.5f ? 1.0f : 0.0f; break;
                        default: v = 0.5f + 0.4f * std::sin (t * 9.0f) * std::cos (t * 23.0f); break;
                    }
                    break;
                case ModType::Envelope: v = t < 0.15f ? t / 0.15f : (t < 0.35f ? 1.0f - (1.0f - sp.sustain) * (t - 0.15f) / 0.2f : (t < 0.75f ? sp.sustain : sp.sustain * (1.0f - (t - 0.75f) / 0.25f))); break;
                case ModType::Follower: v = std::abs (std::sin (t * 7.0f)) * (0.4f + 0.6f * std::abs (std::sin (t * 2.0f))); break;
                case ModType::Transients: v = std::fmod (t * 4.0f, 1.0f) < 0.08f ? 1.0f : std::exp (-std::fmod (t * 4.0f, 1.0f) * 12.0f); break;
                case ModType::Slider: v = sp.value; break;
                default: break;
            }
            const auto pt = Point<float> (gr.getX() + gr.getWidth() * t, gr.getBottom() - gr.getHeight() * jlimit (0.0f, 1.0f, v));
            i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
        }
        if (type == ModType::Slider)
        {
            auto bar = gr.withTrimmedTop (gr.getHeight() * (1.0f - sp.value));
            g.setColour (c.withAlpha (0.8f));
            g.fillRoundedRectangle (bar, 4.0f);
        }
        else
        {
            g.setColour (c);
            g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // live value
        const float v = proc.modValue[(size_t) index].load();
        const float vn = type == ModType::XLFO ? 0.5f + 0.5f * v : v;
        auto meter = Rectangle<float> (r.getX() + 12.0f, r.getBottom() - 9.0f, r.getWidth() - 24.0f, 3.0f);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (meter, 1.5f);
        g.setColour (c);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * jlimit (0.0f, 1.0f, vn)), 1.5f);
    }

    void mouseDown (const MouseEvent& e) override
    {
        const auto type = (int) pv (proc.apvts, SISaturnProcessor::mid (index, "type"));
        if (type == 0 || e.mods.isPopupMenu())
        {
            PopupMenu m;
            for (int t = 1; t < 6; ++t) m.addItem (t, kModTypeNames[t], true, t == type);
            if (type != 0) { m.addSeparator(); m.addItem (100, "Remove source"); }
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r >= 1 && r <= 5) setParam (proc.apvts, SISaturnProcessor::mid (index, "type"), (float) r);
                if (r == 100)
                {
                    setParam (proc.apvts, SISaturnProcessor::mid (index, "type"), 0.0f);
                    for (int j = 0; j < kModSlots; ++j)
                        if ((int) pv (proc.apvts, SISaturnProcessor::sid (j, "src")) == index + 1)
                            setParam (proc.apvts, SISaturnProcessor::sid (j, "src"), 0.0f);
                }
                if (onSelect) onSelect (index);
            });
            return;
        }
        dragFromDot = dotRect().expanded (4.0f).contains (e.position);
        if (onSelect) onSelect (index);
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (dragFromDot && e.getDistanceFromDragStart() > 4)
            if (auto* dc = DragAndDropContainer::findParentDragContainerFor (this))
                if (! dc->isDragAndDropActive())
                {
                    Image img (Image::ARGB, 16, 16, true);
                    { Graphics g (img); g.setColour (kModCol[index]); g.fillEllipse (1, 1, 14, 14); }
                    dc->startDragging ("mod:" + String (index), this, ScaledImage (img), false, nullptr, &e.source);
                }
    }

private:
    Rectangle<float> dotRect() const { return { 9.0f, 8.0f, 13.0f, 13.0f }; }
    SISaturnProcessor& proc;
    int index;
    bool dragFromDot = false;
};

/** Settings of the selected source + its connections. */
class ModEditor : public Component
{
public:
    explicit ModEditor (SISaturnProcessor& p) : proc (p) {}

    void setSource (int k)
    {
        src = k;
        type = (int) pv (proc.apvts, SISaturnProcessor::mid (k, "type"));
        knobs.clear();
        combos.clear();
        auto& s = proc.apvts;
        auto addKnob = [&] (const char* id, const char* name) { knobs.push_back (std::make_unique<si::Knob> (s, SISaturnProcessor::mid (k, id), name)); };
        auto addCombo = [&] (const char* id) { combos.push_back (std::make_unique<si::Choice> (s, SISaturnProcessor::mid (k, id))); };
        switch ((ModType) type)
        {
            case ModType::XLFO: addKnob ("rate", "Rate"); addCombo ("sync"); addCombo ("shape"); break;
            case ModType::Envelope: addKnob ("atk", "Attack"); addKnob ("dec", "Decay"); addKnob ("sus", "Sustain"); addKnob ("rel", "Release"); addKnob ("thr", "Threshold"); addCombo ("input"); break;
            case ModType::Follower:
            case ModType::Transients: addKnob ("atk", "Attack"); addKnob ("rel", "Release"); addCombo ("input"); break;
            case ModType::Slider: addKnob ("value", "Value"); break;
            default: break;
        }
        for (auto& kn : knobs) addAndMakeVisible (*kn);
        for (auto& c : combos) addAndMakeVisible (*c);
        resized();
        repaint();
    }
    int getType() const { return type; }

    void paint (Graphics& g) override
    {
        if (type == 0)
        {
            g.setColour (col::hint);
            g.setFont (si::font (11.0f));
            g.drawText ("Add a source with +, then drag its coloured dot onto any knob (Drive, Mix, Tone...).", getLocalBounds(), Justification::centred);
            return;
        }
        g.setColour (kModCol[src]);
        g.setFont (si::font (11.0f, Weight::Bold, 0.1f));
        g.drawText (String (kModTypeNames[type]).toUpperCase() + " " + String (src + 1), Rectangle<int> (0, 0, 200, 22), Justification::centredLeft);
        // connections
        int x = 0, y = 26;
        g.setFont (si::font (10.0f, Weight::SemiBold));
        chipRects.clear();
        for (int j = 0; j < kModSlots; ++j)
        {
            if ((int) pv (proc.apvts, SISaturnProcessor::sid (j, "src")) != src + 1) continue;
            const int t = (int) pv (proc.apvts, SISaturnProcessor::sid (j, "tgt"));
            if (t <= 0) continue;
            const float amt = pv (proc.apvts, SISaturnProcessor::sid (j, "amt"));
            auto chip = Rectangle<float> ((float) x, (float) y, 150.0f, 22.0f);
            if (chip.getRight() > (float) getWidth()) break;
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (chip, 11.0f);
            // amount as a thin bar along the bottom edge (drag the chip sideways to change it)
            auto bar = Rectangle<float> (chip.getX() + 10.0f, chip.getBottom() - 4.0f, chip.getWidth() - 28.0f, 2.0f);
            const float cx = bar.getCentreX();
            const float w = bar.getWidth() * 0.5f * std::abs (amt) / 100.0f;
            g.setColour (kModCol[src]);
            g.fillRect (amt >= 0 ? Rectangle<float> (cx, bar.getY(), w, 2.0f) : Rectangle<float> (cx - w, bar.getY(), w, 2.0f));
            g.setColour (col::text);
            g.drawText (SISaturnProcessor::targetNames()[t] + "  " + String (roundToInt (amt)) + "%", chip.withTrimmedLeft (10.0f).withTrimmedRight (18.0f), Justification::centredLeft, true);
            g.setColour (col::label);
            g.drawText ("x", chip.withTrimmedLeft (chip.getWidth() - 18.0f), Justification::centred);
            chipRects.push_back ({ chip, j });
            x += 156;
            if (x + 150 > getWidth()) { x = 0; y += 26; if (y > 52) break; }
        }
        if (chipRects.empty())
        {
            g.setColour (col::hint);
            g.setFont (si::font (10.0f));
            g.drawText ("Not connected - drag the dot onto a knob", Rectangle<int> (0, 26, getWidth(), 22), Justification::centredLeft);
        }
    }

    void resized() override
    {
        int x = 0;
        for (auto& kn : knobs) { kn->setBounds (x, getHeight() - 92, 70, 92); x += 72; }
        for (auto& c : combos) { c->setBounds (x + 4, getHeight() - 70, 110, 26); x += 118; }
    }

    void mouseDown (const MouseEvent& e) override
    {
        for (auto& [r, j] : chipRects)
            if (r.contains (e.position))
            {
                if (e.position.x > r.getRight() - 18.0f) { setParam (proc.apvts, SISaturnProcessor::sid (j, "src"), 0.0f); setParam (proc.apvts, SISaturnProcessor::sid (j, "tgt"), 0.0f); repaint(); return; }
                dragSlot = j;
                startAmt = pv (proc.apvts, SISaturnProcessor::sid (j, "amt"));
                return;
            }
        dragSlot = -1;
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (dragSlot < 0) return;
        setParam (proc.apvts, SISaturnProcessor::sid (dragSlot, "amt"), jlimit (-100.0f, 100.0f, startAmt + (float) e.getDistanceFromDragStartX() * 1.0f));
        repaint();
    }
    void mouseDoubleClick (const MouseEvent&) override { if (dragSlot >= 0) setParam (proc.apvts, SISaturnProcessor::sid (dragSlot, "amt"), 50.0f); }

private:
    SISaturnProcessor& proc;
    int src = 0, type = 0, dragSlot = -1;
    float startAmt = 0;
    std::vector<std::unique_ptr<si::Knob>> knobs;
    std::vector<std::unique_ptr<si::Choice>> combos;
    std::vector<std::pair<Rectangle<float>, int>> chipRects;
};

//============================================================================== compact knob for the bottom bar
class MiniKnob : public Component
{
public:
    MiniKnob (si::APVTS& s, const String& id, const String& n) : name (n), param (s.getParameter (id))
    {
        slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
        att = std::make_unique<si::APVTS::SliderAttachment> (s, id, slider);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }
    void resized() override { slider.setBounds (0, 0, getHeight(), getHeight()); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().withTrimmedLeft (getHeight() + 6);
        g.setColour (col::label);
        g.setFont (si::font (8.5f, Weight::Bold, 0.12f));
        g.drawText (name.toUpperCase(), r.removeFromTop (getHeight() / 2), Justification::bottomLeft);
        g.setColour (col::text);
        g.setFont (si::font (11.0f, Weight::SemiBold));
        g.drawText (param->getCurrentValueAsText(), r, Justification::topLeft);
    }
    Slider slider;
private:
    String name;
    RangedAudioParameter* param;
    std::unique_ptr<si::APVTS::SliderAttachment> att;
};

//============================================================================== root
class Root : public si::ScaledContent, public DragAndDropTarget, private Timer
{
public:
    explicit Root (SISaturnProcessor& p) : si::ScaledContent (W, H), proc (p), s (p.apvts), presetBar (p.presets), display (p), editor (p),
                                           inK (s, "input", "Input"), outK (s, "output", "Output"), mixK (s, "mix", "Mix")
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &display, &editor, &inK, &outK, &mixK, &hqSeg, &msSeg, &autoBtn })
            addAndMakeVisible (c);
        for (int k = 0; k < kModSources; ++k)
        {
            auto* t = tiles.add (new ModTile (p, k));
            t->onSelect = [this] (int i) { selectSource (i); };
            addAndMakeVisible (t);
        }
        inK.slider.getProperties().set ("modTarget", SISaturnProcessor::targetIds().indexOf ("input"));
        outK.slider.getProperties().set ("modTarget", SISaturnProcessor::targetIds().indexOf ("output"));
        mixK.slider.getProperties().set ("modTarget", SISaturnProcessor::targetIds().indexOf ("mix"));
        hqSeg.setTooltip ("High Quality: oversampling for cleaner heavy distortion (adds a little latency)");
        msSeg.setTooltip ("Stereo or Mid/Side: the Pan rings then balance Mid against Side");
        autoBtn.setTooltip ("Auto Level: keeps each band as loud as before the drive");
        p.presets.onChange = [this] { presetBar.refresh(); display.select (0); selectSource (firstActiveSource()); };
        display.select (0);
        selectSource (firstActiveSource());
        resized();
        startTimerHz (15);
    }
    ~Root() override { proc.presets.onChange = nullptr; }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 8, 270, 44 }, "SI Saturn", "Multiband Distortion");
        si::drawPanel (g, { 16, 420, 968, 182 }, "Modulation");
        // bottom bar
        auto bar = Rectangle<float> (16.0f, 610.0f, 968.0f, 40.0f);
        g.setColour (Colour (0x99141414));
        g.fillRoundedRectangle (bar, 12.0f);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (bar.reduced (0.5f), 12.0f, 1.0f);
        auto m = Rectangle<float> (942.0f, 616.0f, 7.0f, 28.0f);
        si::drawLevelMeter (g, m, proc.inLevel.load());
        si::drawLevelMeter (g, m.translated (11.0f, 0.0f), proc.outLevel.load());
    }

    void paintOverChildren (Graphics& g) override
    {
        if (dropHover != nullptr)
        {
            auto r = getLocalArea (dropHover, dropHover->getLocalBounds()).toFloat();
            g.setColour (kModCol[dragSrc].withAlpha (0.9f));
            g.drawRoundedRectangle (r.expanded (2.0f), 8.0f, 2.0f);
        }
    }

    void resized() override
    {
        presetBar.setBounds (350, 12, 300, 36);
        display.setBounds (16, 60, 968, 352);
        for (int k = 0; k < kModSources; ++k) tiles[k]->setBounds (32 + k * 112, 452, 104, 136);
        editor.setBounds (488, 448, 480, 146);
        inK.setBounds (28, 616, 110, 28);
        outK.setBounds (146, 616, 110, 28);
        mixK.setBounds (264, 616, 110, 28);
        hqSeg.setBounds (430, 615, 210, 30);
        msSeg.setBounds (650, 615, 170, 30);
        autoBtn.setBounds (830, 615, 100, 30);
    }

    // ---- drag a modulation source onto a knob
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("mod:"); }
    void itemDragMove (const SourceDetails& d) override
    {
        dragSrc = d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
        auto* t = targetAt (d.localPosition);
        if (t != dropHover) { dropHover = t; repaint(); }
    }
    void itemDragExit (const SourceDetails&) override { dropHover = nullptr; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        const int k = d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
        dropHover = nullptr;
        if (auto* t = targetAt (d.localPosition))
        {
            const int tgt = (int) t->getProperties()["modTarget"];
            int slot = -1;
            for (int j = 0; j < kModSlots && slot < 0; ++j)
                if ((int) pv (s, SISaturnProcessor::sid (j, "src")) == k + 1 && (int) pv (s, SISaturnProcessor::sid (j, "tgt")) == tgt) slot = j;   // already connected
            for (int j = 0; j < kModSlots && slot < 0; ++j)
                if ((int) pv (s, SISaturnProcessor::sid (j, "src")) == 0) slot = j;
            if (slot < 0) slot = kModSlots - 1;
            setParam (s, SISaturnProcessor::sid (slot, "src"), (float) (k + 1));
            setParam (s, SISaturnProcessor::sid (slot, "tgt"), (float) tgt);
            setParam (s, SISaturnProcessor::sid (slot, "amt"), 50.0f);
            selectSource (k);
        }
        repaint();
    }

    void selfTestSetup()
    {
        display.select (1);
        selectSource (firstActiveSource());
    }

private:
    Component* targetAt (Point<int> pos)
    {
        for (auto* c = getComponentAt (pos); c != nullptr && c != this; c = c->getParentComponent())
            if (c->getProperties().contains ("modTarget")) return c;
        return nullptr;
    }
    int firstActiveSource() const
    {
        for (int k = 0; k < kModSources; ++k) if (pv (proc.apvts, SISaturnProcessor::mid (k, "type")) > 0.5f) return k;
        return 0;
    }
    void selectSource (int k)
    {
        selectedSrc = k;
        for (int i = 0; i < kModSources; ++i) tiles[i]->selected = i == k;
        editor.setSource (k);
        repaint();
    }
    void timerCallback() override
    {
        for (auto* t : tiles) t->repaint();
        if ((int) pv (s, SISaturnProcessor::mid (selectedSrc, "type")) != editor.getType()) editor.setSource (selectedSrc);
        editor.repaint();
        repaint (936, 612, 40, 36);
        // modulation dots on modulated knobs
        std::array<int, 80> modBy {};
        modBy.fill (-1);
        for (int j = 0; j < kModSlots; ++j)
        {
            const int src = (int) pv (s, SISaturnProcessor::sid (j, "src")) - 1;
            const int tgt = (int) pv (s, SISaturnProcessor::sid (j, "tgt"));
            if (src >= 0 && tgt > 0 && tgt < 80 && pv (s, SISaturnProcessor::mid (src, "type")) > 0.5f) modBy[(size_t) tgt] = src;
        }
        std::function<void (Component&)> walk = [&] (Component& c)
        {
            if (c.getProperties().contains ("modTarget"))
            {
                const int t = (int) c.getProperties()["modTarget"];
                const bool has = c.getProperties().contains ("modDot");
                if (modBy[(size_t) t] >= 0) { c.getProperties().set ("modDot", (int64) kModCol[modBy[(size_t) t]].getARGB()); c.repaint(); }
                else if (has) { c.getProperties().remove ("modDot"); c.repaint(); }
            }
            for (auto* ch : c.getChildren()) walk (*ch);
        };
        walk (*this);
    }

    SISaturnProcessor& proc;
    si::APVTS& s;
    si::PresetBar presetBar;
    SaturnDisplay display;
    OwnedArray<ModTile> tiles;
    ModEditor editor;
    MiniKnob inK, outK, mixK;
    si::Segmented hqSeg { s, "hq", { "HQ Off", "2x", "4x", "8x" } };
    si::Segmented msSeg { s, "msmode", { "Stereo", "Mid/Side" } };
    si::Toggle autoBtn { s, "autogain", "Auto Level" };
    Component* dropHover = nullptr;
    int dragSrc = 0, selectedSrc = 0;
};
} // namespace

//==============================================================================
SISaturnEditor::SISaturnEditor (SISaturnProcessor& p) : AudioProcessorEditor (p)
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
        p.presets.load (5);   // dev snapshot: 2 bands + follower modulation
        static_cast<Root*> (root.get())->selfTestSetup();
        si::uiSelfTest (*root, "SI Saturn");
    }
}

SISaturnEditor::~SISaturnEditor() { setLookAndFeel (nullptr); }

void SISaturnEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
