#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RiserRenderer.h"
#include "PresetManager.h"

class SIRiserProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::Timer,
                         private juce::Thread
{
public:
    SIRiserProcessor();
    ~SIRiserProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI Riser"; }
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

    // ---- UI API (message thread)
    void toggleCapture();
    bool loadFile (const juce::File&);
    void requestPreview();          // play / stop
    juce::File writeRenderToFile (bool forDrag);
    std::shared_ptr<si::RiserResult> getResult() const;
    bool hasSource() const { return sourceLen > 0; }
    double getSampleRateSafe() const { return sr; }

    enum class Status { Empty, Armed, Recording, Rendering, Ready, Failed };
    std::atomic<Status> status { Status::Empty };
    std::atomic<float> bpm { 120.0f }, recSeconds { 0.0f }, inLevel { 0.0f };
    std::atomic<bool> hostBpmValid { false }, previewPlaying { false }, previewWaiting { false }, hostPlaying { false };
    std::atomic<double> ppqNow { 0.0 };
    std::atomic<int64_t> previewPos { 0 };
    std::atomic<int> resultVersion { 0 };

    juce::AudioProcessorValueTreeState apvts;
    si::PresetManager presets;

private:
    void parameterChanged (const juce::String&, float) override;
    void timerCallback() override;
    void run() override;
    void kickRender();
    si::RiserSettings makeSettings() const;

    double sr = 48000.0;
    juce::AudioFormatManager formats;

    // capture (audio thread writes, message thread reads after stop)
    juce::AudioBuffer<float> capture;
    std::atomic<bool> armed { false }, captureDone { false };
    int capPos = 0, silentFor = 0;
    bool heardSignal = false;

    // source (message + render thread)
    juce::CriticalSection sourceLock;
    juce::AudioBuffer<float> source;
    std::atomic<int> sourceLen { 0 };
    double sourceRate = 48000.0;

    // result
    juce::SpinLock resultLock;
    std::shared_ptr<si::RiserResult> result;
    std::atomic<bool> renderRequested { false }, cancelRender { false };
    std::atomic<juce::uint32> dirtyAt { 0 };
    float lastRenderBpm = 0.0f;

    // preview
    std::atomic<int> previewCmd { 0 };   // 1 = start, 2 = stop
    double waitTargetPpq = -1.0;
    int dragCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIRiserProcessor)
};
