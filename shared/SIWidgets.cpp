#include "SIWidgets.h"

namespace si
{
using namespace juce;

void drawSmallLabel (Graphics& g, Rectangle<int> r, const String& t, Justification j)
{
    g.setColour (col::label);
    g.setFont (font (9.5f, Weight::SemiBold, 0.14f));
    g.drawText (t.toUpperCase(), r, j, false);
}

//============================================================================== self test
static void lint (Component& c, const String& path, StringArray& out)
{
    Array<Component*> kids;
    for (auto* k : c.getChildren())
        if (k->isVisible()) kids.add (k);
    for (int i = 0; i < kids.size(); ++i)
    {
        const auto b = kids[i]->getBounds();
        if (b.getWidth() < 4 || b.getHeight() < 4)
            out.add (path + ": visible child has no size " + b.toString());
        if (! c.getLocalBounds().contains (b))
            out.add (path + ": " + b.toString() + " outside " + c.getLocalBounds().toString());
        for (int j = i + 1; j < kids.size(); ++j)
            if (b.intersects (kids[j]->getBounds()))
                out.add (path + ": overlap " + b.toString() + " / " + kids[j]->getBounds().toString());
        lint (*kids[i], path + "/" + String (i), out);
    }
}

void uiSelfTest (Component& root, const String& name)
{
    const auto dir = SystemStats::getEnvironmentVariable ("SI_UI_SELFTEST", {});
    if (dir.isEmpty()) return;
    File d (dir);
    d.createDirectory();
    StringArray issues;
    lint (root, name, issues);
    d.getChildFile (name + "-lint.txt").replaceWithText (issues.isEmpty() ? "OK\n" : issues.joinIntoString ("\n"));
    Image img (Image::ARGB, root.getWidth(), root.getHeight(), true, SoftwareImageType());
    {
        Graphics g (img);
        root.paintEntireComponent (g, true);
    }
    auto f = d.getChildFile (name + ".png");
    f.deleteFile();
    if (auto os = f.createOutputStream())
        PNGImageFormat().writeImageToStream (img, *os);
}

//============================================================================== knob
Knob::Knob (APVTS& s, const String& id, const String& n, bool bipolar) : param (s.getParameter (id)), name (n)
{
    jassert (param != nullptr);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (1.0, 1, 0.0, false, ModifierKeys::altModifier);
    slider.getProperties().set ("bipolar", bipolar);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.setPopupMenuEnabled (false);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (32);
    const int side = jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void Knob::paint (Graphics& g)
{
    auto bottom = getLocalBounds().removeFromBottom (32);
    String v = valueText ? valueText() : String();
    if (v.isEmpty()) v = param->getCurrentValueAsText();
    const bool hl = highlighted && highlighted();
    g.setColour (hl ? col::accentHi : col::text);
    g.setFont (font (12.0f, Weight::Medium));
    g.drawText (v, bottom.removeFromTop (17), Justification::centred, true);
    g.setColour (col::label);
    g.setFont (font (9.0f, Weight::SemiBold, 0.14f));
    g.drawText (name.toUpperCase(), bottom, Justification::centredTop, false);
}

//============================================================================== toggle / choice
Toggle::Toggle (APVTS& s, const String& id, const String& text, Icon icon) : TextButton (text)
{
    setClickingTogglesState (true);
    setIcon (*this, icon);
    attachment = std::make_unique<APVTS::ButtonAttachment> (s, id, *this);
}

Choice::Choice (APVTS& s, const String& id)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (s.getParameter (id)))
        addItemList (p->choices, 1);
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (s, id, *this);
}

//============================================================================== segmented
void SegmentBase::addSegment (const String& text, std::function<void()> onClick)
{
    auto* b = buttons.add (new TextButton (text));
    b->getProperties().set ("seg", true);
    b->onClick = std::move (onClick);
    addAndMakeVisible (b);
}

Rectangle<float> SegmentBase::slot (int i) const
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const float w = r.getWidth() / (float) jmax (1, buttons.size());
    return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
}

void SegmentBase::select (int i, bool animate)
{
    index = i;
    for (int k = 0; k < buttons.size(); ++k)
        buttons[k]->setToggleState (k == i, dontSendNotification);
    if (! animate || thumbX < 0.0f || getWidth() == 0)
    {
        thumbX = slot (i).getX();
        thumbW = slot (i).getWidth();
        repaint();
    }
    else
        startTimerHz (60);
}

void SegmentBase::timerCallback()
{
    const auto t = slot (index);
    thumbX += (t.getX() - thumbX) * 0.3f;
    thumbW += (t.getWidth() - thumbW) * 0.3f;
    if (std::abs (thumbX - t.getX()) < 0.3f) { thumbX = t.getX(); thumbW = t.getWidth(); stopTimer(); }
    repaint();
}

void SegmentBase::resized()
{
    for (int i = 0; i < buttons.size(); ++i)
    {
        const auto sl = slot (i);
        const int x0 = roundToInt (sl.getX()), x1 = roundToInt (sl.getRight());
        buttons[i]->setBounds (x0, roundToInt (sl.getY()), x1 - x0, roundToInt (sl.getHeight()));
    }
    thumbX = slot (index).getX();
    thumbW = slot (index).getWidth();
}

void SegmentBase::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float rad = r.getHeight() * 0.5f;
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);

    auto th = Rectangle<float> (thumbX, r.getY() + 3.0f, thumbW, r.getHeight() - 6.0f);
    const float tr = th.getHeight() * 0.5f;
    g.setGradientFill (accentGradient (th.getTopLeft(), th.getBottomRight()));
    g.fillRoundedRectangle (th, tr);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.4f), 0, th.getY(), Colours::white.withAlpha (0.0f), 0, th.getCentreY(), false));
    g.drawRoundedRectangle (th.reduced (0.5f), tr, 1.0f);
}

Segmented::Segmented (APVTS& s, const String& id, StringArray labels)
{
    auto* p = dynamic_cast<AudioParameterChoice*> (s.getParameter (id));
    jassert (p != nullptr);
    if (labels.isEmpty()) labels = p->choices;
    for (int i = 0; i < labels.size(); ++i)
        addSegment (labels[i], [this, i] { if (attachment) attachment->setValueAsCompleteGesture ((float) i); });
    attachment = std::make_unique<ParameterAttachment> (*p, [this] (float v) { select (roundToInt (v), true); });
    attachment->sendInitialUpdate();
}

TabBar::TabBar (StringArray tabs)
{
    for (int i = 0; i < tabs.size(); ++i)
        addSegment (tabs[i], [this, i] { setIndex (i); });
    select (0, false);
}

void TabBar::setIndex (int i)
{
    select (i, true);
    if (onChange) onChange (i);
}

//============================================================================== presets
PresetBar::PresetBar (PresetManager& p) : pm (p)
{
    for (auto* b : { &prev, &next, &save })
    {
        addAndMakeVisible (b);
        b->getProperties().set ("seg", true);
    }
    setIcon (prev, Icon::Prev);
    setIcon (next, Icon::Next);
    setIcon (save, Icon::Save);
    save.setTooltip ("Save preset");
    addAndMakeVisible (box);
    box.getProperties().set ("ghost", true);
    box.getProperties().set ("preset", true);
    box.setTextWhenNothingSelected ("Init");
    box.setJustificationType (Justification::centred);
    box.onChange = [this]
    {
        const int i = box.getSelectedItemIndex();
        if (i >= 0 && i != pm.getCurrent()) pm.load (i);
    };
    prev.onClick = [this] { pm.loadNext (-1); };
    next.onClick = [this] { pm.loadNext (1); };
    save.onClick = [this]
    {
        auto* w = new AlertWindow ("Save Preset", "Name your preset", MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", box.getText().isNotEmpty() ? box.getText() + " 2" : "My Preset");
        w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1) pm.saveUser (w->getTextEditorContents ("name"));
        }), true);
    };
    refresh();
}

void PresetBar::refresh()
{
    box.clear (dontSendNotification);
    const auto names = pm.getNames();
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == pm.getNumFactory()) box.addSeparator();
        box.addItem (names[i], i + 1);
    }
    box.setSelectedItemIndex (pm.getCurrent(), dontSendNotification);
}

void PresetBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    // dividers
    g.setColour (Colours::white.withAlpha (0.08f));
    for (auto* b : { &prev, &next })
    {
        const float x = b == &prev ? (float) prev.getRight() : (float) next.getX();
        g.fillRect (x, r.getY() + 8.0f, 1.0f, r.getHeight() - 16.0f);
    }
    g.fillRect ((float) save.getX(), r.getY() + 8.0f, 1.0f, r.getHeight() - 16.0f);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int h = r.getHeight();
    save.setBounds (r.removeFromRight (h + 4));
    prev.setBounds (r.removeFromLeft (h));
    next.setBounds (r.removeFromRight (h));
    box.setBounds (r.reduced (4, 2));
}
} // namespace si
