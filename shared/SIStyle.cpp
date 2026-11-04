#include "SIStyle.h"
#include "BinaryData.h"

namespace si
{
using namespace juce;

//============================================================================== fonts
Typeface::Ptr typeface (Weight w)
{
    static Typeface::Ptr reg = Typeface::createSystemTypefaceFor (BinaryData::PlusJakartaSansRegular_ttf, BinaryData::PlusJakartaSansRegular_ttfSize);
    static Typeface::Ptr med = Typeface::createSystemTypefaceFor (BinaryData::PlusJakartaSansMedium_ttf, BinaryData::PlusJakartaSansMedium_ttfSize);
    static Typeface::Ptr semi = Typeface::createSystemTypefaceFor (BinaryData::PlusJakartaSansSemiBold_ttf, BinaryData::PlusJakartaSansSemiBold_ttfSize);
    static Typeface::Ptr bold = Typeface::createSystemTypefaceFor (BinaryData::PlusJakartaSansBold_ttf, BinaryData::PlusJakartaSansBold_ttfSize);
    static Typeface::Ptr xbold = Typeface::createSystemTypefaceFor (BinaryData::PlusJakartaSansExtraBold_ttf, BinaryData::PlusJakartaSansExtraBold_ttfSize);
    switch (w)
    {
        case Weight::Medium: return med;
        case Weight::SemiBold: return semi;
        case Weight::Bold: return bold;
        case Weight::ExtraBold: return xbold;
        default: return reg;
    }
}

Font font (float height, Weight w, float tracking)
{
    return Font (FontOptions (typeface (w)).withHeight (height).withKerningFactor (tracking));
}

Font titleFont (float height) { return font (height, Weight::SemiBold); }
Font uiFont (float height, bool bold) { return font (height, bold ? Weight::Medium : Weight::Regular); }

ColourGradient accentGradient (Point<float> a, Point<float> b, float alpha)
{
    ColourGradient gr (Colour (0xffe11d48).withMultipliedAlpha (alpha), a, col::accent2.withMultipliedAlpha (alpha), b, false);
    gr.addColour (0.45, col::accent.withMultipliedAlpha (alpha));
    return gr;
}

//============================================================================== icons
void drawIcon (Graphics& g, Icon icon, Rectangle<float> r, Colour c)
{
    const float s = jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (s, s);
    const float sw = jmax (1.4f, s * 0.11f);
    const PathStrokeType stroke (sw, PathStrokeType::curved, PathStrokeType::rounded);
    g.setColour (c);
    Path p;
    auto P = [&] (float x, float y) { return Point<float> (r.getX() + x * s, r.getY() + y * s); };

    switch (icon)
    {
        case Icon::None: return;
        case Icon::Record: g.fillEllipse (r.reduced (s * 0.22f)); return;
        case Icon::Play:
            p.addTriangle (P (0.3f, 0.2f), P (0.3f, 0.8f), P (0.82f, 0.5f));
            g.fillPath (p, AffineTransform());
            return;
        case Icon::Stop: g.fillRoundedRectangle (r.reduced (s * 0.25f), s * 0.08f); return;
        case Icon::Sync:
            p.addCentredArc (r.getCentreX(), r.getCentreY(), s * 0.32f, s * 0.32f, 0.0f, 0.4f, 2.7f, true);
            p.addCentredArc (r.getCentreX(), r.getCentreY(), s * 0.32f, s * 0.32f, 0.0f, 3.55f, 5.85f, true);
            g.strokePath (p, stroke);
            p.clear();
            p.addTriangle (P (0.86f, 0.30f), P (0.62f, 0.30f), P (0.80f, 0.10f));
            p.addTriangle (P (0.14f, 0.70f), P (0.38f, 0.70f), P (0.20f, 0.90f));
            g.fillPath (p);
            return;
        case Icon::Thru:
            p.startNewSubPath (P (0.15f, 0.5f)); p.lineTo (P (0.8f, 0.5f));
            p.startNewSubPath (P (0.58f, 0.28f)); p.lineTo (P (0.82f, 0.5f)); p.lineTo (P (0.58f, 0.72f));
            g.strokePath (p, stroke);
            return;
        case Icon::Folder:
            p.startNewSubPath (P (0.14f, 0.3f)); p.lineTo (P (0.4f, 0.3f)); p.lineTo (P (0.48f, 0.38f)); p.lineTo (P (0.86f, 0.38f));
            p.lineTo (P (0.86f, 0.76f)); p.lineTo (P (0.14f, 0.76f)); p.closeSubPath();
            g.strokePath (p, stroke);
            return;
        case Icon::Export:
            p.startNewSubPath (P (0.5f, 0.14f)); p.lineTo (P (0.5f, 0.6f));
            p.startNewSubPath (P (0.3f, 0.42f)); p.lineTo (P (0.5f, 0.62f)); p.lineTo (P (0.7f, 0.42f));
            p.startNewSubPath (P (0.16f, 0.66f)); p.lineTo (P (0.16f, 0.84f)); p.lineTo (P (0.84f, 0.84f)); p.lineTo (P (0.84f, 0.66f));
            g.strokePath (p, stroke);
            return;
        case Icon::Grip:
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 2; ++j)
                    g.fillEllipse (Rectangle<float> (s * 0.13f, s * 0.13f).withCentre (P (0.38f + j * 0.24f, 0.26f + i * 0.24f)));
            return;
        case Icon::Save:
            p.startNewSubPath (P (0.2f, 0.18f)); p.lineTo (P (0.68f, 0.18f)); p.lineTo (P (0.82f, 0.32f)); p.lineTo (P (0.82f, 0.82f));
            p.lineTo (P (0.2f, 0.82f)); p.closeSubPath();
            p.startNewSubPath (P (0.34f, 0.82f)); p.lineTo (P (0.34f, 0.58f)); p.lineTo (P (0.66f, 0.58f)); p.lineTo (P (0.66f, 0.82f));
            g.strokePath (p, stroke);
            return;
        case Icon::Prev:
            p.startNewSubPath (P (0.6f, 0.25f)); p.lineTo (P (0.36f, 0.5f)); p.lineTo (P (0.6f, 0.75f));
            g.strokePath (p, stroke);
            return;
        case Icon::Next:
            p.startNewSubPath (P (0.4f, 0.25f)); p.lineTo (P (0.64f, 0.5f)); p.lineTo (P (0.4f, 0.75f));
            g.strokePath (p, stroke);
            return;
        case Icon::Chevron:
            p.startNewSubPath (P (0.28f, 0.4f)); p.lineTo (P (0.5f, 0.62f)); p.lineTo (P (0.72f, 0.4f));
            g.strokePath (p, stroke);
            return;
        case Icon::Freeze:
            for (int k = 0; k < 3; ++k)
            {
                const float a = MathConstants<float>::pi * (float) k / 3.0f;
                const auto c0 = r.getCentre();
                const float R = s * 0.38f;
                const Point<float> d (std::cos (a) * R, std::sin (a) * R);
                p.startNewSubPath (c0 - d); p.lineTo (c0 + d);
                for (float sg : { -1.0f, 1.0f })
                {
                    const auto tip = c0 + d * sg * 0.62f;
                    const Point<float> n (-d.y * 0.28f, d.x * 0.28f);
                    p.startNewSubPath (tip + n - d * sg * 0.2f); p.lineTo (tip + d * sg * 0.12f); p.lineTo (tip - n - d * sg * 0.2f);
                }
            }
            g.strokePath (p, PathStrokeType (sw * 0.85f, PathStrokeType::curved, PathStrokeType::rounded));
            return;
        case Icon::Auto:
        {
            auto star = [&] (Point<float> c0, float R)
            {
                Path st;
                st.startNewSubPath (c0.x, c0.y - R);
                st.quadraticTo (c0.x, c0.y, c0.x + R, c0.y);
                st.quadraticTo (c0.x, c0.y, c0.x, c0.y + R);
                st.quadraticTo (c0.x, c0.y, c0.x - R, c0.y);
                st.quadraticTo (c0.x, c0.y, c0.x, c0.y - R);
                g.fillPath (st);
            };
            star (P (0.44f, 0.54f), s * 0.34f);
            star (P (0.78f, 0.22f), s * 0.14f);
            return;
        }
        case Icon::Headphones:
            p.addCentredArc (r.getCentreX(), r.getCentreY() + s * 0.06f, s * 0.32f, s * 0.32f, 0.0f, -1.75f, 1.75f, true);
            g.strokePath (p, stroke);
            g.fillRoundedRectangle (Rectangle<float> (s * 0.16f, s * 0.3f).withCentre (P (0.2f, 0.66f)), s * 0.05f);
            g.fillRoundedRectangle (Rectangle<float> (s * 0.16f, s * 0.3f).withCentre (P (0.8f, 0.66f)), s * 0.05f);
            return;
        case Icon::Power:
            p.addCentredArc (r.getCentreX(), r.getCentreY() + s * 0.04f, s * 0.3f, s * 0.3f, 0.0f, 0.65f, 5.63f, true);
            p.startNewSubPath (P (0.5f, 0.12f)); p.lineTo (P (0.5f, 0.48f));
            g.strokePath (p, stroke);
            return;
        case Icon::Up:
            p.startNewSubPath (P (0.28f, 0.6f)); p.lineTo (P (0.5f, 0.38f)); p.lineTo (P (0.72f, 0.6f));
            g.strokePath (p, stroke);
            return;
        case Icon::Down:
            p.startNewSubPath (P (0.28f, 0.4f)); p.lineTo (P (0.5f, 0.62f)); p.lineTo (P (0.72f, 0.4f));
            g.strokePath (p, stroke);
            return;
        case Icon::Wave:
            p.startNewSubPath (P (0.1f, 0.5f));
            for (int i = 1; i <= 24; ++i)
            {
                const float t = (float) i / 24.0f;
                p.lineTo (P (0.1f + 0.8f * t, 0.5f - 0.28f * std::sin (t * MathConstants<float>::twoPi * 1.5f) * (1.0f - std::abs (t - 0.5f))));
            }
            g.strokePath (p, stroke);
            return;
    }
}

//============================================================================== surfaces
void drawBackground (Graphics& g, Rectangle<int> area)
{
    static Image cache;
    if (cache.getWidth() != area.getWidth() || cache.getHeight() != area.getHeight())
    {
        const int w = area.getWidth(), h = area.getHeight();
        cache = Image (Image::ARGB, w, h, true, SoftwareImageType());
        Graphics ig (cache);
        ig.setGradientFill (ColourGradient (col::bg2, 0, 0, col::bg, 0, (float) h, false));
        ig.fillAll();
        auto glow = [&] (Colour c, float cx, float cy, float rad, float a)
        {
            ColourGradient gr (c.withAlpha (a), cx, cy, c.withAlpha (0.0f), cx + rad, cy, true);
            gr.addColour (0.45, c.withAlpha (a * 0.35f));
            ig.setGradientFill (gr);
            ig.fillRect (0, 0, w, h);
        };
        glow (col::accent, w * 0.08f, -h * 0.12f, w * 0.60f, 0.16f);
        glow (col::deep, w * 1.0f, h * 1.08f, w * 0.55f, 0.14f);
        glow (col::accent, w * 0.6f, h * 0.35f, w * 0.38f, 0.03f);

        // brand pattern, like the website background
        ig.setColour (col::accent.withAlpha (0.028f));
        ig.setFont (font (30.0f, Weight::ExtraBold));
        for (int row = -1; row < h / 90 + 2; ++row)
            for (int c = -1; c < w / 230 + 2; ++c)
            {
                const float x = (float) c * 230.0f + (row % 2) * 115.0f, y = (float) row * 90.0f;
                ig.addTransform (AffineTransform::rotation (-0.09f, x, y));
                ig.drawText ("SKILL", Rectangle<float> (x, y, 200.0f, 32.0f), Justification::left);
                ig.drawText ("ISSUE", Rectangle<float> (x + 40.0f, y + 28.0f, 200.0f, 32.0f), Justification::left);
                ig.addTransform (AffineTransform::rotation (0.09f, x, y));
            }

        // grain
        Random rnd (1234);
        Image::BitmapData bd (cache, Image::BitmapData::readWrite);
        for (int i = 0; i < w * h / 8; ++i)
        {
            const int x = rnd.nextInt (w), y = rnd.nextInt (h);
            const auto px = bd.getPixelColour (x, y);
            bd.setPixelColour (x, y, rnd.nextBool() ? px.brighter (0.08f) : px.darker (0.08f));
        }
        // vignette
        ColourGradient vg (Colours::transparentBlack, w * 0.5f, h * 0.45f, Colours::black.withAlpha (0.5f), 0.0f, 0.0f, true);
        ig.setGradientFill (vg);
        ig.fillRect (0, 0, w, h);
    }
    g.drawImageAt (cache, area.getX(), area.getY());
}

Rectangle<int> drawPanel (Graphics& g, Rectangle<int> area, const String& title, bool hero)
{
    const auto r = area.toFloat();
    const float rad = hero ? 24.0f : 20.0f;
    for (int i = 1; i <= 3; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.14f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f + (float) i * 2.0f).expanded ((float) i), rad + (float) i);
    }
    // .glass: rgba(20,20,20,.6) + linear-gradient(145deg, white 8%, white 1%) + 1px white 8% border
    g.setColour (Colour (0x99141414));
    g.fillRoundedRectangle (r, rad);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.08f), r.getX(), r.getY(), Colours::white.withAlpha (0.01f),
                                       r.getX() + r.getHeight() * 0.7f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.14f), 0, r.getY(), Colours::white.withAlpha (0.05f), 0, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    if (hero)
    {
        ColourGradient top (col::accent.withAlpha (0.0f), r.getX() + rad, r.getY(), Colours::white.withAlpha (0.0f), r.getRight() - rad, r.getY(), false);
        top.addColour (0.3, col::accent.withAlpha (0.7f));
        top.addColour (0.7, col::accentHi.withAlpha (0.4f));
        g.setGradientFill (top);
        g.fillRect (r.getX() + rad, r.getY(), r.getWidth() - rad * 2.0f, 1.2f);
    }

    auto content = area.reduced (18, 14);
    if (title.isNotEmpty())
    {
        auto t = content.removeFromTop (14).toFloat();
        g.setColour (col::accent.withAlpha (0.35f));
        g.fillEllipse (t.getX() - 1.5f, t.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (col::accent);
        g.fillEllipse (t.getX(), t.getCentreY() - 2.5f, 5.0f, 5.0f);
        g.setColour (col::label);
        g.setFont (font (10.0f, Weight::Bold, 0.16f));
        g.drawText (title.toUpperCase(), t.withTrimmedLeft (12.0f), Justification::centredLeft, false);
        content.removeFromTop (8);
    }
    return content;
}

void drawHeaderTitle (Graphics& g, Rectangle<int> area, const String& name, const String& subtitle)
{
    auto r = area.toFloat();
    auto tile = Rectangle<float> (r.getX(), r.getCentreY() - 19.0f, 38.0f, 38.0f);
    g.setGradientFill (accentGradient (tile.getTopLeft(), tile.getBottomRight()));
    g.fillRoundedRectangle (tile, 11.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.45f), 0, tile.getY(), Colours::white.withAlpha (0.0f), 0, tile.getCentreY(), false));
    g.drawRoundedRectangle (tile.reduced (0.5f), 11.0f, 1.0f);
    g.setColour (col::onAccent);
    g.setFont (font (15.0f, Weight::ExtraBold, -0.02f));
    g.drawText ("SI", tile.translated (0.0f, 0.5f), Justification::centred);

    auto txt = r.withTrimmedLeft (52.0f);
    const auto nf = font (21.0f, Weight::ExtraBold, -0.01f);
    const float nw = GlyphArrangement::getStringWidth (nf, name);
    // gradient text like the site headline: white -> pink -> rose
    ColourGradient tg (Colours::white, txt.getX(), 0, col::accent, txt.getX() + nw * 1.25f, 0, false);
    tg.addColour (0.55, col::accentHi);
    g.setGradientFill (tg);
    g.setFont (nf);
    g.drawText (name, txt.withTrimmedBottom (r.getHeight() * 0.40f), Justification::bottomLeft, false);
    g.setColour (col::label);
    g.setFont (font (9.0f, Weight::SemiBold, 0.22f));
    g.drawText (subtitle.toUpperCase(), txt.withTrimmedTop (r.getHeight() * 0.62f), Justification::topLeft, false);
}

void drawTempoChip (Graphics& g, Rectangle<float> r, float bpm, const String& source, Colour sourceColour, float beatPhase)
{
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);

    const auto dc = Point<float> (r.getX() + 18.0f, r.getCentreY());
    const float pulse = beatPhase < 0.0f ? 0.0f : std::pow (1.0f - beatPhase, 3.0f);
    if (pulse > 0.01f)
    {
        g.setColour (sourceColour.withAlpha (0.35f * pulse));
        g.fillEllipse (Rectangle<float> (16.0f + 6.0f * pulse, 16.0f + 6.0f * pulse).withCentre (dc));
    }
    g.setColour (sourceColour.withAlpha (0.45f + 0.55f * (beatPhase < 0.0f ? 1.0f : pulse)));
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (dc));

    const float x = r.getX() + 32.0f, w = r.getWidth() - 46.0f;
    const auto numRow = Rectangle<float> (x, r.getCentreY() - 15.0f, w, 18.0f);
    const auto srcRow = Rectangle<float> (x, r.getCentreY() + 4.0f, w, 11.0f);
    const String num (bpm, 1);
    const auto nf = font (14.0f, Weight::SemiBold);
    g.setColour (col::text);
    g.setFont (nf);
    g.drawText (num, numRow, Justification::centredLeft);
    g.setColour (col::label);
    g.setFont (font (9.0f, Weight::Medium, 0.1f));
    g.drawText ("BPM", numRow.withTrimmedLeft (GlyphArrangement::getStringWidth (nf, num) + 4.0f).translated (0.0f, 1.0f), Justification::centredLeft);
    g.setColour (sourceColour);
    g.setFont (font (8.0f, Weight::SemiBold, 0.18f));
    g.drawText (source.toUpperCase(), srcRow, Justification::centredLeft);
}

//============================================================================== look and feel
LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypeface (typeface (Weight::Regular));
    setColour (ResizableWindow::backgroundColourId, col::bg);
    setColour (Label::textColourId, col::text);
    setColour (ComboBox::textColourId, col::text);
    setColour (ComboBox::backgroundColourId, col::well);
    setColour (ComboBox::arrowColourId, col::label);
    setColour (PopupMenu::backgroundColourId, col::card);
    setColour (PopupMenu::textColourId, col::text);
    setColour (PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.16f));
    setColour (PopupMenu::highlightedTextColourId, col::text);
    setColour (TextButton::textColourOffId, col::label);
    setColour (TextButton::textColourOnId, col::onAccent);
    setColour (TextEditor::textColourId, col::text);
    setColour (TextEditor::highlightColourId, col::accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId, col::accent);
    setColour (AlertWindow::backgroundColourId, col::card);
    setColour (AlertWindow::textColourId, col::text);
    setColour (AlertWindow::outlineColourId, col::line);
    setColour (TooltipWindow::backgroundColourId, col::card);
    setColour (TooltipWindow::textColourId, col::text);
}

void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const float size = (float) jmin (w, h);
    const auto c = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    const float r = size * 0.5f - 1.0f;
    const bool hover = s.isMouseOverOrDragging();
    const bool enabled = s.isEnabled();
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const bool fromEnd = (bool) s.getProperties().getWithDefault ("fromEnd", false);
    const float ang = start + pos * (end - start);
    const float from = bipolar ? (start + end) * 0.5f : (fromEnd ? end : start);
    const float lo = jmin (from, ang), hi = jmax (from, ang);
    auto polar = [&] (float a, float rad) { return Point<float> (c.x + std::sin (a) * rad, c.y - std::cos (a) * rad); };

    // clean ring that fills with the value (no tick marks)
    const float t = jmax (2.5f, size * 0.05f);
    const float arcR = r - t;
    Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.strokePath (track, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));
    if (hi - lo > 0.001f && enabled)
    {
        Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, lo, hi, true);
        const auto grad = accentGradient (polar (lo, arcR), polar (hi, arcR));
        g.setGradientFill (grad);
        g.strokePath (val, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // modulation indicator dot (set by editors that support modulation)
    if (s.getProperties().contains ("modDot"))
    {
        g.setColour (Colour ((uint32) (int64) s.getProperties()["modDot"]));
        g.fillEllipse ((float) x + (float) w - 7.0f, (float) y + 1.0f, 6.0f, 6.0f);
    }

    // optional dynamic-range ring (EQ gain knob): outer arc between two normalised positions
    if (s.getProperties().contains ("dynFrom") && enabled)
    {
        const float f0 = (float) s.getProperties()["dynFrom"], f1 = (float) s.getProperties()["dynTo"];
        const float a0 = start + f0 * (end - start), a1 = start + f1 * (end - start);
        if (std::abs (a1 - a0) > 0.01f)
        {
            Path dynArc;
            dynArc.addCentredArc (c.x, c.y, r - 0.5f, r - 0.5f, 0.0f, jmin (a0, a1), jmax (a0, a1), true);
            g.setColour (Colour ((uint32) (int64) s.getProperties().getWithDefault ("dynColour", (int64) 0xffff3b30)));
            g.strokePath (dynArc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    // disc
    const float discR = arcR - t * 0.5f - jmax (4.0f, size * 0.07f);
    const auto disc = Rectangle<float> (discR * 2.0f, discR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (disc.translated (0.0f, 2.5f).expanded (1.5f));
    g.setGradientFill (ColourGradient (Colour (0xff2a2a2d), disc.getX(), disc.getY(), Colour (0xff0d0d0e), disc.getRight(), disc.getBottom(), false));
    g.fillEllipse (disc);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (hover ? 0.28f : 0.18f), c.x, disc.getY(), Colours::white.withAlpha (0.0f), c.x, disc.getBottom(), false));
    g.drawEllipse (disc.reduced (0.5f), 1.0f);

    // indicator
    const auto ip = polar (ang, discR - jmax (4.5f, size * 0.09f));
    const float dot = jmax (3.5f, size * 0.07f);
    g.setColour (enabled ? col::text : col::hint);
    g.fillEllipse (Rectangle<float> (dot, dot).withCentre (ip));
}

void LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    if ((bool) b.getProperties().getWithDefault ("seg", false)) return;   // drawn by the segmented thumb
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float rad = jmin ((float) b.getProperties().getWithDefault ("radius", 999.0), r.getHeight() * 0.5f);
    const bool on = b.getToggleState();
    const bool cta = (bool) b.getProperties().getWithDefault ("cta", false);
    const bool warn = (bool) b.getProperties().getWithDefault ("warn", false);
    const bool recBtn = (bool) b.getProperties().getWithDefault ("rec", false);
    const float a = b.isEnabled() ? 1.0f : 0.35f;

    if (on)
    {
        const auto base = b.getProperties().contains ("tint") ? Colour ((uint32) (int64) b.getProperties()["tint"]) : (recBtn ? col::rec : (warn ? col::warn : col::accent));
        if (recBtn || warn || b.getProperties().contains ("tint"))
            g.setGradientFill (ColourGradient (base.brighter (0.2f), r.getX(), r.getY(), base.darker (0.15f), r.getX(), r.getBottom(), false));
        else
            g.setGradientFill (accentGradient (r.getTopLeft(), r.getBottomRight(), a));
        g.fillRoundedRectangle (r, rad);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.35f), 0, r.getY(), Colours::white.withAlpha (0.0f), 0, r.getCentreY(), false));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    }
    else
    {
        g.setColour ((cta ? col::accent.withAlpha (over ? 0.16f : 0.10f) : Colours::white.withAlpha (over ? 0.085f : 0.05f)).withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, rad);
        g.setColour ((cta ? col::accent.withAlpha (over ? 0.8f : 0.45f) : Colours::white.withAlpha (over ? 0.16f : 0.09f)).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    }
    if (down)
    {
        g.setColour (Colours::black.withAlpha (0.15f));
        g.fillRoundedRectangle (r, rad);
    }
}

void LookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool over, bool)
{
    const bool on = b.getToggleState();
    const bool cta = (bool) b.getProperties().getWithDefault ("cta", false);
    const bool seg = (bool) b.getProperties().getWithDefault ("seg", false);
    const auto icon = (Icon) (int) b.getProperties().getWithDefault ("icon", 0);
    Colour c = on ? col::onAccent : (cta ? col::accentHi : (over ? col::text : col::label));
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);

    auto r = b.getLocalBounds().toFloat();
    const auto f = getTextButtonFont (b, b.getHeight());
    const bool keepCase = (bool) b.getProperties().getWithDefault ("keepCase", false);
    const String txt = keepCase ? b.getButtonText() : b.getButtonText().toUpperCase();
    const float iconS = jmin (r.getHeight() * 0.44f, 16.0f);
    const auto iconC = (! on && (bool) b.getProperties().getWithDefault ("rec", false)) ? col::rec.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f) : c;
    if (icon != Icon::None && txt.isEmpty())
    {
        drawIcon (g, icon, r.withSizeKeepingCentre (iconS, iconS), iconC);
        return;
    }
    const float tw = GlyphArrangement::getStringWidth (f, txt);
    const float total = tw + (icon != Icon::None ? iconS + 7.0f : 0.0f);
    float x = r.getCentreX() - total * 0.5f;
    if (icon != Icon::None)
    {
        drawIcon (g, icon, Rectangle<float> (x, r.getCentreY() - iconS * 0.5f, iconS, iconS), iconC);
        x += iconS + 7.0f;
    }
    g.setColour (c);
    g.setFont (f);
    g.drawText (txt, Rectangle<float> (x, r.getY(), tw + 4.0f, r.getHeight()).translated (0.0f, 0.5f), Justification::centredLeft, false);
}

Font LookAndFeel::getTextButtonFont (TextButton&, int h)
{
    return font (jlimit (9.5f, 11.5f, (float) h * 0.34f), Weight::SemiBold, 0.08f);
}

void LookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    const bool ghost = (bool) box.getProperties().getWithDefault ("ghost", false);
    const bool over = box.isMouseOver (true);
    if (! ghost)
    {
        g.setColour (Colours::white.withAlpha (over ? 0.085f : 0.05f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Colours::white.withAlpha (over ? 0.16f : 0.09f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }
    else if (over)
    {
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r, 10.0f);
    }
    const bool preset = (bool) box.getProperties().getWithDefault ("preset", false);
    const float cs = preset ? 12.0f : (ghost ? 18.0f : 14.0f);
    drawIcon (g, Icon::Chevron, Rectangle<float> ((float) w - cs - 10.0f, (float) h * 0.5f - cs * 0.5f, cs, cs), col::accent);
}

Font LookAndFeel::getComboBoxFont (ComboBox& b)
{
    if ((bool) b.getProperties().getWithDefault ("preset", false))
        return font (13.0f, Weight::Medium);
    if ((bool) b.getProperties().getWithDefault ("ghost", false))
        return font (jmin (22.0f, (float) b.getHeight() * 0.6f), Weight::SemiBold, -0.01f);
    return font (jlimit (11.0f, 13.0f, (float) b.getHeight() * 0.44f), Weight::Medium);
}

void LookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    const bool ghost = (bool) box.getProperties().getWithDefault ("ghost", false);
    label.setBounds (ghost ? 6 : 10, 0, box.getWidth() - (ghost ? 38 : 32), box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (Label::textColourId, col::text);
}

Font LookAndFeel::getPopupMenuFont() { return font (12.5f, Weight::Regular); }

void LookAndFeel::getIdealPopupMenuItemSize (const String& text, bool isSeparator, int standardHeight, int& w, int& h)
{
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, w, h);
    h = isSeparator ? 9 : 26;
    w += 12;
}

void LookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (col::card);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                     bool isTicked, bool, const String& text, const String&, const Drawable*, const Colour*)
{
    if (isSeparator)
    {
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (col::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (r.toFloat(), 7.0f);
    }
    if (isTicked)
    {
        g.setGradientFill (accentGradient (r.getTopLeft().toFloat(), r.getBottomLeft().toFloat().translated (8, 0)));
        g.fillEllipse ((float) r.getX() + 7.0f, (float) r.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
    g.setColour (isTicked ? col::accentHi : (isActive ? col::text : col::hint));
    g.setFont (isTicked ? font (12.5f, Weight::Medium) : getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (20), Justification::centredLeft, true);
}

void LookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
{
    g.setColour (col::accent);
    g.setFont (font (9.5f, Weight::SemiBold, 0.18f));
    g.drawText (name.toUpperCase(), area.reduced (12, 0), Justification::bottomLeft, true);
}

void LookAndFeel::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor&)
{
    g.setColour (Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 8.0f);
}

void LookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? col::accent : Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 8.0f, 1.0f);
}

Rectangle<int> LookAndFeel::getTooltipBounds (const String& text, Point<int> pos, Rectangle<int> parent)
{
    const auto f = font (12.0f);
    AttributedString s;
    s.append (text, f, col::text);
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 260.0f);
    const int w = (int) tl.getWidth() + 22, h = (int) tl.getHeight() + 14;
    return Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                           pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
}

void LookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (col::card);
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawRect (0, 0, w, h, 1);
    AttributedString s;
    s.append (text, font (12.0f), col::text);
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, (float) w - 22.0f);
    tl.draw (g, Rectangle<float> ((float) w, (float) h).reduced (11.0f, 7.0f));
}
} // namespace si

namespace si
{
void drawLevelMeter (juce::Graphics& g, juce::Rectangle<float> r, float level)
{
    using namespace juce;
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r, 3.0f);
    auto toY = [&] (float db) { return r.getBottom() - r.getHeight() * jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
    const float db = level > 0.001f ? 20.0f * std::log10 (level) : -60.0f;
    auto fill = r.withTop (toY (db));
    ColourGradient gr (col::accent2, 0, r.getBottom(), col::rec, 0, r.getY(), false);
    gr.addColour (0.75, col::accent);
    gr.addColour (0.9, col::warn);
    g.setGradientFill (gr);
    g.fillRoundedRectangle (fill, 3.0f);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillRect (r.getX(), toY (0.0f), r.getWidth(), 1.0f);
}
} // namespace si

namespace si
{
void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    using namespace juce;
    if (style != Slider::LinearVertical) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    const float cx = (float) x + (float) w * 0.5f;
    const float top = (float) y + 6.0f, bot = (float) (y + h) - 6.0f;
    // track
    g.setColour (Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (cx - 1.5f, top, 3.0f, bot - top, 1.5f);
    // fill from the centre (bipolar)
    const float mid = (top + bot) * 0.5f;
    const float py = jlimit (top, bot, pos);
    g.setColour (col::accent.withAlpha (s.isEnabled() ? 0.9f : 0.3f));
    g.fillRoundedRectangle (cx - 1.5f, jmin (mid, py), 3.0f, std::abs (py - mid), 1.5f);
    g.setColour (Colours::white.withAlpha (0.25f));
    g.fillRect (cx - 5.0f, mid, 10.0f, 1.0f);
    // thumb: grey pill
    auto th = Rectangle<float> (jmin (22.0f, (float) w - 2.0f), 10.0f).withCentre ({ cx, py });
    g.setGradientFill (ColourGradient (Colour (0xffd4d4d8), 0, th.getY(), Colour (0xff8a8a90), 0, th.getBottom(), false));
    g.fillRoundedRectangle (th, 3.0f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRect (th.getX() + 4.0f, th.getCentreY() - 0.5f, th.getWidth() - 8.0f, 1.0f);
}
} // namespace si
