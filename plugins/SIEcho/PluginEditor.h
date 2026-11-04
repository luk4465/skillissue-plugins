#pragma once
#include "PluginProcessor.h"
#include "SIWidgets.h"

class SIEchoEditor : public juce::AudioProcessorEditor
{
public:
    explicit SIEchoEditor (SIEchoProcessor&);
    ~SIEchoEditor() override;
    void resized() override;

private:
    si::LookAndFeel lnf;
    std::unique_ptr<juce::Component> root;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIEchoEditor)
};
