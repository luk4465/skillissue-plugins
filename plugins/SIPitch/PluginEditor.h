#pragma once
#include "PluginProcessor.h"
#include "SIWidgets.h"

class SIPitchEditor : public juce::AudioProcessorEditor
{
public:
    explicit SIPitchEditor (SIPitchProcessor&);
    ~SIPitchEditor() override;
    void resized() override;

private:
    si::LookAndFeel lnf;
    std::unique_ptr<juce::Component> root;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIPitchEditor)
};
