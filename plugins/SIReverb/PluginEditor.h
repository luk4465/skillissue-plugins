#pragma once
#include "PluginProcessor.h"
#include "SIWidgets.h"

class SIReverbEditor : public juce::AudioProcessorEditor
{
public:
    explicit SIReverbEditor (SIReverbProcessor&);
    ~SIReverbEditor() override;
    void resized() override;

private:
    si::LookAndFeel lnf;
    std::unique_ptr<juce::Component> root;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIReverbEditor)
};
