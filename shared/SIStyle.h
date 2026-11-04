#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace si
{
namespace col
{
    // skillissue.site palette
    const juce::Colour bg       { 0xff050505 };
    const juce::Colour bg2      { 0xff0c0a0c };
    const juce::Colour card     { 0xff141414 };   // popup / solid panel
    const juce::Colour well     { 0xff1a1a1d };   // insets
    const juce::Colour line     { 0x14ffffff };   // --glass-border
    const juce::Colour text     { 0xffffffff };
    const juce::Colour label    { 0xffa3a3a3 };   // --text-muted
    const juce::Colour hint     { 0xff666666 };
    const juce::Colour accent   { 0xfff43f5e };   // --primary (rose)
    const juce::Colour accentHi { 0xffff8fa3 };   // gradient pink
    const juce::Colour accent2  { 0xffff8fa3 };   // gradient partner
    const juce::Colour deep     { 0xffbe123c };   // --primary-dark
    const juce::Colour onAccent { 0xffffffff };   // text on rose fills
    const juce::Colour warn     { 0xffffb020 };
    const juce::Colour rec      { 0xffff3b30 };
    const juce::Colour info     { 0xff9e9eff };   // lavender
}

enum class Weight { Regular, Medium, SemiBold, Bold, ExtraBold };
juce::Font font (float height, Weight w = Weight::Regular, float tracking = 0.0f);
juce::Font titleFont (float height);
juce::Font uiFont (float height, bool bold = false);
juce::Typeface::Ptr typeface (Weight);

/** Lime -> teal brand gradient between two points. */
juce::ColourGradient accentGradient (juce::Point<float> a, juce::Point<float> b, float alpha = 1.0f);

enum class Icon { None, Record, Play, Stop, Sync, Thru, Folder, Export, Grip, Save, Prev, Next, Freeze, Auto, Chevron, Wave, Headphones, Power, Up, Down };
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);

/** Window background: near-black, soft lime/teal glows, grain. Cached per size. */
void drawBackground (juce::Graphics& g, juce::Rectangle<int> area);
/** Borderless glass panel; small caps title with a lime dot. Returns content area. */
juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title = {}, bool hero = false);
inline juce::Rectangle<int> drawCard (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title = {}) { return drawPanel (g, area, title); }
/** Logo tile + plugin name + subtitle. */
void drawHeaderTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, const juce::String& subtitle);
/** Tempo chip with a beat dot (beatPhase 0..1, <0 = idle). */
void drawTempoChip (juce::Graphics& g, juce::Rectangle<float> r, float bpm, const juce::String& source, juce::Colour sourceColour, float beatPhase);

/** Vertical level meter (linear peak), -60..+6 dB, gradient fill + 0 dB tick. */
void drawLevelMeter (juce::Graphics&, juce::Rectangle<float>, float level);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    void getIdealPopupMenuItemSize (const juce::String&, bool isSeparator, int standardHeight, int& w, int& h) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    juce::Font getAlertWindowTitleFont() override { return font (16.0f, Weight::SemiBold); }
    juce::Font getAlertWindowMessageFont() override { return font (13.0f); }
    juce::Font getAlertWindowFont() override { return font (13.0f); }
};
} // namespace si
