#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EchoEngine.h"
#include "PresetManager.h"

class SIEchoProcessor : public juce::AudioProcessor
{
public:
    SIEchoProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI Echo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& noteNames();
    static double noteBeats (int i);
    si::echo::EchoParams readParams() const;   // times resolved with the current BPM

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;
    si::echo::EchoEngine engine;
    std::atomic<float> bpm { 120.0f };
    std::atomic<bool> hostBpm { false };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIEchoProcessor)
};
