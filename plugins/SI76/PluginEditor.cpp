#include "PluginEditor.h"
#include "Sidechain.h"
#include "Dial.h"

using namespace juce;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 920, H = 540;

//==============================================================================
/** Back-lit analogue VU: GR mode swings left from 0, +4/+8 modes show output level. */
class VuMeter : public Component, private Timer
{
public:
    explicit VuMeter (SI76Processor& p) : proc (p) { startTimerHz (60); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        // bezel
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 16.0f);
        auto face = r.reduced (8.0f);
        ColourGradient fg (Colour (0xff1f1a1b), face.getCentreX(), face.getBottom() + 40.0f, Colour (0xff0b0a0a), face.getCentreX(), face.getY(), true);
        g.setGradientFill (fg);
        g.fillRoundedRectangle (face, 11.0f);
        // lime back-light
        ColourGradient bl (col::accent.withAlpha (0.22f), face.getCentreX(), face.getBottom(), col::accent.withAlpha (0.0f), face.getCentreX(), face.getY() - 10.0f, true);
        g.setGradientFill (bl);
        g.fillRoundedRectangle (face, 11.0f);

        const int mode = (int) proc.apvts.getRawParameterValue ("meter")->load();
        const auto pivot = Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.5f);
        const float rad = face.getHeight() * 1.15f;
        const float a0 = -0.85f, a1 = 0.85f;   // radians from vertical
        auto ang = [&] (float vu) { return a0 + (a1 - a0) * jlimit (0.0f, 1.0f, (vu + 20.0f) / 23.0f); };   // -20..+3 VU
        auto pt = [&] (float a, float rr) { return Point<float> (pivot.x + std::sin (a) * rr, pivot.y - std::cos (a) * rr); };

        // scale
        const float marks[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
        Path arcBase, arcRed;
        arcBase.addCentredArc (pivot.x, pivot.y, rad * 0.86f, rad * 0.86f, 0.0f, a0, ang (0.0f), true);
        arcRed.addCentredArc (pivot.x, pivot.y, rad * 0.86f, rad * 0.86f, 0.0f, ang (0.0f), a1, true);
        g.setColour (col::text.withAlpha (0.75f));
        g.strokePath (arcBase, PathStrokeType (1.4f));
        g.setColour (col::rec);
        g.strokePath (arcRed, PathStrokeType (4.0f));
        g.setFont (si::font (10.0f, Weight::SemiBold));
        for (float m : marks)
        {
            const float a = ang (m);
            const bool major = m == -20 || m == -10 || m == -5 || m == 0 || m == 3 || m == -3;
            g.setColour (m > 0 ? col::rec : col::text.withAlpha (0.8f));
            g.drawLine (Line<float> (pt (a, rad * 0.86f), pt (a, rad * (major ? 0.94f : 0.91f))), major ? 1.6f : 1.0f);
            if (major)
            {
                const String lab = m > 0 ? "+" + String ((int) m) : String ((int) m);
                auto c = pt (a, rad * 1.02f);
                g.drawText (lab, Rectangle<float> (30.0f, 14.0f).withCentre (c), Justification::centred);
            }
        }
        g.setColour (col::text.withAlpha (0.55f));
        g.setFont (si::font (13.0f, Weight::SemiBold, 0.3f));
        g.drawText (mode == 0 ? "GAIN REDUCTION" : "OUTPUT VU", face.withTrimmedTop (12.0f).withHeight (16.0f), Justification::centred);
        g.setColour (col::accent.withAlpha (0.8f));
        g.setFont (si::font (9.0f, Weight::SemiBold, 0.3f));
        g.drawText (String (CharPointer_UTF8 ("SKILLISSUE  \xc2\xb7  SI 76")), face.withTrimmedTop (face.getHeight() * 0.80f).withHeight (14.0f), Justification::centred);

        // needle
        const float a = ang (needle);
        Path nd;
        nd.startNewSubPath (pt (a, rad * 0.18f));
        nd.lineTo (pt (a, rad * 0.95f));
        g.setColour (Colours::black.withAlpha (0.45f));
        g.strokePath (nd, PathStrokeType (3.0f), AffineTransform::translation (2.0f, 3.0f));
        g.setColour (Colour (0xfff7f2e8));
        g.strokePath (nd, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        // pivot cover
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (face.toNearestInt());
        g.setColour (Colour (0xff050605));
        g.fillEllipse (Rectangle<float> (rad * 0.5f, rad * 0.5f).withCentre (pivot));
        // glass
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), face.getX(), face.getY(), Colours::white.withAlpha (0.0f), face.getX() + face.getWidth() * 0.4f, face.getCentreY(), false));
        g.fillRoundedRectangle (face, 11.0f);
    }

private:
    void timerCallback() override
    {
        const int mode = (int) proc.apvts.getRawParameterValue ("meter")->load();
        float target;
        if (mode == 0) target = jlimit (-20.0f, 3.0f, proc.grDb.load());            // 0 at rest, swings left
        else
        {
            const float db = Decibels::gainToDecibels (proc.outLevel.load(), -60.0f) - 3.0f;   // peak -> ~VU
            target = jlimit (-20.0f, 3.0f, db + 18.0f - (mode == 1 ? 4.0f : 8.0f));
        }
        if (mode == 0 && proc.grDb.load() > -0.05f) target = 0.0f;
        // VU ballistics (~300 ms) with a little overshoot
        const float k = 1.0f - std::exp (-1.0f / (0.065f * 60.0f));
        vel += (target - needle) * k * 0.9f;
        vel *= 0.72f;
        needle += vel;
        repaint();
    }
    SI76Processor& proc;
    float needle = 0.0f, vel = 0.0f;
};

//==============================================================================
/** BLUE / BLACK model switch (radio pair bound to the "model" choice). */
class ModelSwitch : public Component
{
public:
    explicit ModelSwitch (si::APVTS& s)
    {
        blue.getProperties().set ("tint", (int64) 0xff2f6bff);
        black.getProperties().set ("tint", (int64) 0xff2a2a2e);
        blue.setTooltip ("BLUE: smooth classic 1176 - soft knee, gentle colour, natural release");
        black.setTooltip ("BLACK: aggressive - faster attack, hard knee, more grit and bite. Made for rap vocals");
        for (auto* b : { &blue, &black }) { addAndMakeVisible (b); b->getProperties().set ("radius", 12.0); }
        auto* p = dynamic_cast<AudioParameterChoice*> (s.getParameter ("model"));
        att = std::make_unique<ParameterAttachment> (*p, [this] (float v)
        {
            blue.setToggleState (v < 0.5f, dontSendNotification);
            black.setToggleState (v >= 0.5f, dontSendNotification);
        });
        blue.onClick = [this] { att->setValueAsCompleteGesture (0.0f); };
        black.onClick = [this] { att->setValueAsCompleteGesture (1.0f); };
        att->sendInitialUpdate();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        blue.setBounds (r.removeFromLeft (r.getWidth() / 2 - 4));
        r.removeFromLeft (8);
        black.setBounds (r);
    }
private:
    TextButton blue { "Blue" }, black { "Black" };
    std::unique_ptr<ParameterAttachment> att;
};

//==============================================================================
class Root : public si::ScaledContent
{
public:
    explicit Root (SI76Processor& p) : si::ScaledContent (W, H), proc (p), s (p.apvts), presetBar (p.presets), vu (p)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &vu, &model, &meterSeg, &input, &output, &ratio, &attack, &release, &mix, &color,
                                                            &schpf, &link, &sc })
            addAndMakeVisible (c);
        ratio.keepCase();
        meterSeg.keepCase();
        input.slider.setTooltip ("Drives the signal into the compressor: more input = more compression");
        output.slider.setTooltip ("Make-up gain");
        sc.setTooltip (si::sc::kTip);
        attack.slider.setTooltip ("7 = fastest (20 us), 1 = slowest (800 us) - same direction as the hardware");
        release.slider.setTooltip ("7 = fastest (50 ms), 1 = slowest (1.1 s)");

        p.presets.onChange = [this] { presetBar.refresh(); };
        resized();
    }
    ~Root() override { proc.presets.onChange = nullptr; }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 260, 44 }, "SI 76", "FET Compressor");
        si::drawPanel (g, { 24, 80, 872, 268 }, {}, true);
        si::drawPanel (g, { 24, 360, 872, 164 }, "Character");
        si::drawSmallLabel (g, { 40, 392, 300, 14 }, "Ratio");
        si::drawSmallLabel (g, { 776, 392, 104, 14 }, "Sidechain HPF");
        drawDialScale (g, input.getBounds());
        drawDialScale (g, output.getBounds());
    }

    // the printed 1176 scale around a dial: inf 48 36 30 24 18 12 6 0
    static void drawDialScale (Graphics& g, Rectangle<int> knobBounds)
    {
        auto r = knobBounds.toFloat();
        r.removeFromBottom (32.0f);
        const float side = jmin (r.getWidth(), r.getHeight());
        const auto c = r.getCentre();
        const float start = MathConstants<float>::pi * 1.25f, end = MathConstants<float>::pi * 2.75f;
        g.setFont (si::font (10.0f, Weight::SemiBold));
        for (int i = 0; i < si76::kDialMarks; ++i)
        {
            const float a = start + si76::dialPos()[i] * (end - start);
            const float rt = side * 0.5f + 3.0f, rl = side * 0.5f + 14.0f;
            g.setColour (Colours::white.withAlpha (0.35f));
            g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre ({ c.x + std::sin (a) * rt, c.y - std::cos (a) * rt }));
            g.setColour (col::label);
            g.drawText (si76::dialLabel (i), Rectangle<float> (24.0f, 12.0f).withCentre ({ c.x + std::sin (a) * rl, c.y - std::cos (a) * rl }), Justification::centred);
        }
    }

    void resized() override
    {
        presetBar.setBounds (310, 20, 300, 38);
        sc.setBounds (622, 24, 66, 30);
        vu.setBounds (40, 96, 420, 200);
        model.setBounds (40, 304, 196, 34);
        meterSeg.setBounds (248, 306, 212, 30);
        input.setBounds (506, 118, 160, 210);
        output.setBounds (712, 118, 160, 210);
        ratio.setBounds (40, 412, 320, 40);
        Rectangle<int> k (372, 396, 392, 112);
        for (auto* kn : { &attack, &release, &mix, &color }) kn->setBounds (k.removeFromLeft (98).reduced (6, 0));
        schpf.setBounds (776, 412, 104, 30);
        link.setBounds (776, 460, 104, 30);
    }

private:
    SI76Processor& proc;
    si::APVTS& s;
    si::PresetBar presetBar;
    VuMeter vu;
    ModelSwitch model { s };
    si::Segmented meterSeg { s, "meter" };
    si::Knob input { s, "input", "Input" }, output { s, "output", "Output" };
    si::Segmented ratio { s, "ratio", { "4:1", "8:1", "12:1", "20:1", "ALL" } };
    si::Knob attack { s, "attack", "Attack" }, release { s, "release", "Release" }, mix { s, "mix", "Mix" }, color { s, "color", "Color" };
    si::Choice schpf { s, "schpf" };
    si::Toggle link { s, "link", "Link" };
    si::Toggle sc { s, "extsc", "SC" };
};
} // namespace

//==============================================================================
SI76Editor::SI76Editor (SI76Processor& p) : AudioProcessorEditor (p)
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
        p.presets.load (2);   // All Good Vocal
        si::uiSelfTest (*root, "SI 76");
    }
}

SI76Editor::~SI76Editor() { setLookAndFeel (nullptr); }

void SI76Editor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
