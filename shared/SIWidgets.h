#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SIStyle.h"
#include "PresetManager.h"

namespace si
{
using APVTS = juce::AudioProcessorValueTreeState;

inline void setIcon (juce::Button& b, Icon i) { b.getProperties().set ("icon", (int) i); }

/** Ring knob + value + name, bound to a parameter. */
class Knob : public juce::Component
{
public:
    Knob (APVTS&, const juce::String& paramID, const juce::String& name, bool bipolar = false);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() { repaint(); }

    std::function<juce::String()> valueText;   // optional custom readout
    std::function<bool()> highlighted;         // readout in lime (e.g. tempo-synced)
    juce::Slider slider;

private:
    juce::RangedAudioParameter* param = nullptr;
    juce::String name;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

/** Pill toggle bound to a bool parameter. */
class Toggle : public juce::TextButton
{
public:
    Toggle (APVTS&, const juce::String& paramID, const juce::String& text, Icon icon = Icon::None);
private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** Combo bound to a choice parameter. */
class Choice : public juce::ComboBox
{
public:
    Choice (APVTS&, const juce::String& paramID);
private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

/** Pill selector with an animated sliding thumb. */
class SegmentBase : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    void resized() override;
    void paint (juce::Graphics&) override;
    int getIndex() const { return index; }
    void keepCase() { for (auto* b : buttons) b->getProperties().set ("keepCase", true); }
protected:
    void addSegment (const juce::String& text, std::function<void()> onClick);
    void select (int i, bool animate);
    juce::OwnedArray<juce::TextButton> buttons;
    int index = 0;
private:
    void timerCallback() override;
    juce::Rectangle<float> slot (int i) const;
    float thumbX = -1.0f, thumbW = 0.0f;
};

class Segmented : public SegmentBase
{
public:
    Segmented (APVTS&, const juce::String& paramID, juce::StringArray labels = {});
private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

class TabBar : public SegmentBase
{
public:
    explicit TabBar (juce::StringArray tabs);
    void setIndex (int i);
    std::function<void (int)> onChange;
};

/** Preset pill: prev / name / next / save. */
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (PresetManager&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
private:
    PresetManager& pm;
    juce::ComboBox box;
    juce::TextButton prev, next, save;
};

/** Small caps grey label. */
void drawSmallLabel (juce::Graphics&, juce::Rectangle<int>, const juce::String&, juce::Justification = juce::Justification::centredLeft);

/** Dev check: if env SI_UI_SELFTEST=<dir> is set, renders `root` to <dir>/<name>.png and writes a layout lint
    (empty / outside / overlapping children) to <dir>/<name>-lint.txt. No-op otherwise. */
void uiSelfTest (juce::Component& root, const juce::String& name);

/** Base for fixed-design-size editors that scale uniformly. */
class ScaledContent : public juce::Component
{
public:
    ScaledContent (int w, int h) : designW (w), designH (h) { setSize (w, h); }
    void fitInto (juce::Rectangle<int> parent)
    {
        const float s = juce::jmin ((float) parent.getWidth() / (float) designW, (float) parent.getHeight() / (float) designH);
        setTransform (juce::AffineTransform::scale (s));
    }
    const int designW, designH;
};
} // namespace si
