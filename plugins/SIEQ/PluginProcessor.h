#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "EqEngine.h"
#include "PresetManager.h"
#include "Analyzer.h"

class SIEQProcessor : public juce::AudioProcessor
{
public:
    SIEQProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI EQ"; }
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
    static juce::String pid (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }

    /** Reads the parameters of band i (real units). Safe from any thread. */
    si::eq::BandParams readBand (int i) const;
    void toggleAB();
    int getAB() const { return abIndex; }

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;
    si::AnalyzerFifo preFifo, postFifo;
    std::atomic<int> soloBand { -1 };
    std::atomic<float> meterL { 0 }, meterR { 0 }, autoGainDb { 0 };
    double getRate() const { return sr; }
    float getLiveGain (int i) const { return engine.getLiveGain (i); }

private:
    si::eq::EqEngine engine;
    double sr = 48000.0;
    float outSm = 1.0f;
    juce::ValueTree ab[2];
    int abIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIEQProcessor)
};
