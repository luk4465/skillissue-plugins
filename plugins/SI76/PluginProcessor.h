#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PresetManager.h"
#include "dsp/FetComp.h"


class SI76Processor : public juce::AudioProcessor
{
public:
    SI76Processor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI 76"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;
    std::atomic<float> grDb { 0.0f }, inLevel { 0.0f }, outLevel { 0.0f };

private:
    FetComp comp;
    juce::AudioBuffer<float> dry;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SI76Processor)
};
