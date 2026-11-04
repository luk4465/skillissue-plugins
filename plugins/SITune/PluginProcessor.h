#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "TuneEngine.h"
#include "PresetManager.h"

class SITuneProcessor : public juce::AudioProcessor
{
public:
    SITuneProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI Tune"; }
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
    static const juce::StringArray& scaleNames();
    static int scaleMask (int key, int scale);   // scale index < Custom
    int currentMask() const;

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;
    si::tune::TuneEngine engine;
    double getRate() const { return sr; }

private:
    double sr = 48000.0;
    bool lastLowLat = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SITuneProcessor)
};
