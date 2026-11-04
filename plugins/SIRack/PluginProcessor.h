#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Modules.h"
#include "PresetManager.h"

class SIRackProcessor : public juce::AudioProcessor
{
public:
    SIRackProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI Rack"; }
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
    static juce::String pid (int slot, const juce::String& name) { return "r" + juce::String (slot + 1) + "_" + name; }
    int slotType (int s) const { return (int) apvts.getRawParameterValue (pid (s, "type"))->load(); }

    /** UI: put a module in a slot with its default settings. */
    void loadModule (int slot, int type);
    /** UI: swap two slots (move up / down). */
    void swapSlots (int a, int b);

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;
    std::array<std::atomic<float>, si::rack::kSlots> gr {};
    std::atomic<float> inLevel { 0 }, outLevel { 0 };

private:
    std::array<std::array<std::unique_ptr<si::rack::Processor>, si::rack::kNumModules>, si::rack::kSlots> procs;
    std::array<int, si::rack::kSlots> lastType {};
    float inSm = 1.0f, outSm = 1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIRackProcessor)
};
