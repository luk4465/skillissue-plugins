#pragma once
#include "PluginProcessor.h"
#include "SIWidgets.h"

class SIRiserEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer
{
public:
    explicit SIRiserEditor (SIRiserProcessor&);
    ~SIRiserEditor() override;
    void resized() override;

private:
    si::LookAndFeel lnf;
    std::unique_ptr<juce::Component> root;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIRiserEditor)
};
