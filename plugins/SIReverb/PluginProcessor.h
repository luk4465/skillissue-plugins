#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ReverbEngine.h"
#include "PresetManager.h"

class SIReverbProcessor : public juce::AudioProcessor
{
public:
    SIReverbProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SI Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;

    // values for the UI (written on the audio thread)
    std::atomic<float> bpm { 120.0f }, effPreMs { 20.0f }, effDecayS { 2.5f }, wetEnv { 0.0f };
    std::atomic<int> effPreIdx { 0 }, effDecayIdx { 0 };
    std::atomic<bool> hostBpmValid { false }, playing { false };
    std::atomic<float> inEnv { 0.0f };
    std::atomic<double> ppq { 0.0 };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    si::dsp::ReverbEngine engine;
    juce::AudioBuffer<float> wet;
    float duckEnv = 0.0f, mixSm = 0.3f;
    double sr = 48000.0;
    struct P;
    std::unique_ptr<P> p;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIReverbProcessor)
};
