#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SaturnEngine.h"
#include "Modulation.h"
#include "PresetManager.h"
#include "Analyzer.h"

class SISaturnProcessor : public juce::AudioProcessor
{
public:
    SISaturnProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI Saturn"; }
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
    static juce::String bid (int band, const char* name) { return "s" + juce::String (band + 1) + "_" + name; }
    static juce::String mid (int src, const char* name) { return "m" + juce::String (src + 1) + "_" + name; }
    static juce::String sid (int slot, const char* name) { return "ms" + juce::String (slot + 1) + "_" + name; }

    /** Modulation targets: index 0 = none; returns the parameter ID and a short display name. */
    static const juce::StringArray& targetIds();
    static const juce::StringArray& targetNames();

    /** Settings straight from the parameters (no modulation) - for the UI. */
    si::sat::Settings readSettings() const;
    si::sat::ModSourceParams readSource (int k) const;

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;
    si::sat::SaturnEngine engine;
    si::AnalyzerFifo fifo;
    std::atomic<float> inLevel { 0 }, outLevel { 0 };
    std::array<std::atomic<float>, si::sat::kModSources> modValue {};
    double getRate() const { return sr; }

private:
    double sr = 48000.0;
    int blockSize = 512, osIndex = -1;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[4];
    std::array<si::sat::ModSource, si::sat::kModSources> sources;
    float inSm = 1.0f, outSm = 1.0f, mainEnv = 0.0f, scEnv = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SISaturnProcessor)
};
