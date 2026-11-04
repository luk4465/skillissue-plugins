#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PitchEngine.h"
#include <array>
#include <memory>

/** SI Pitch: Melodyne-style note editor.
    Transfer: arm, play the song, the plugin records the vocal against the host timeline.
    The take is split into notes; corrections are rendered offline (PSOLA) and played back in sync,
    replacing the input wherever the take lies on the timeline. */
class SIPitchProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SIPitchProcessor();
    ~SIPitchProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SI Pitch"; }
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
    static const juce::StringArray& keyNames();
    static const juce::StringArray& scaleNames();
    si::pitch::Correction currentCorrection() const;

    struct Take
    {
        juce::AudioBuffer<float> audio;
        double sr = 48000.0;
        juce::int64 hostStart = 0;   // samples on the host timeline
        si::pitch::Analysis an;
        juce::File file;
    };

    enum class Status { Empty, Armed, Recording, Analysing, Ready, Error };

    // ---- message thread API (editor)
    void setArmed (bool);
    bool isArmed() const { return armed.load(); }
    Status getStatus() const { return (Status) status.load(); }
    float getProgress() const { return progress.load(); }
    juce::String getMessage() const;
    std::shared_ptr<const Take> getTake() const { return take; }
    std::vector<si::pitch::Note> getNotes() const;
    void setNotes (std::vector<si::pitch::Note>);
    void clearTake();
    juce::File exportRendered();
    bool isRendering() const { return renderBusy.load() || renderDirty; }
    /** Self-test / tools: load an audio file as the take (synchronously) at host position 0. */
    bool loadTakeSync (const juce::File&);
    /** Drag & drop: analyses the file in the background and places it at the current playhead. */
    bool loadFile (const juce::File&);
    /** Tests without a message loop: runs one timer tick (recording hand-over, background results, re-render). */
    void pollForTests() { timerCallback(); }

    // host transport, for the playhead
    std::atomic<double> hostSeconds { 0.0 }, bpm { 120.0 };
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> recordedSamples { 0 };
    double getRate() const { return sr; }

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    void finishRecording();
    void startRender();
    void startAnalysis (juce::AudioBuffer<float>&& audio, double rate, juce::int64 start, juce::File existing, std::vector<si::pitch::Note> restore);
    void analyseTake (juce::AudioBuffer<float>&& audio, double rate, juce::int64 start, const juce::File& existing);   // worker thread
    static void applyRestoredEdits (std::vector<si::pitch::Note>& notes, const std::vector<si::pitch::Note>& saved);

    double sr = 48000.0;

    // recording (audio thread writes, timer allocates ahead)
    static constexpr int kChunk = 32768, kMaxChunks = 4096;
    std::array<std::atomic<float*>, kMaxChunks> chunkL {}, chunkR {};
    int chunksAllocated = 0;   // message thread
    std::atomic<bool> armed { false }, finalizePending { false };
    bool recording = false;    // audio thread
    juce::int64 recStart = 0, recLen = 0;
    std::atomic<juce::int64> recStartShared { 0 };
    void freeChunks();

    // take + edits (message thread)
    std::shared_ptr<const Take> take;
    mutable juce::CriticalSection notesLock;
    std::vector<si::pitch::Note> notes;
    si::pitch::Correction lastCorrection;
    bool renderDirty = false;
    std::vector<si::pitch::Note> savedEdits;   // from setStateInformation, applied once the take is analysed

    // what the audio thread plays
    struct Rendered
    {
        juce::AudioBuffer<float> audio;
        juce::int64 hostStart = 0;
        double sr = 48000.0;
    };
    std::atomic<Rendered*> live { nullptr };
    std::vector<std::pair<std::unique_ptr<Rendered>, double>> owned;   // kept alive a while after being replaced

    // background work
    juce::ThreadPool pool { 1 };
    mutable juce::CriticalSection resultLock;
    std::unique_ptr<Take> pendingTake;
    std::unique_ptr<Rendered> pendingRender;
    std::atomic<bool> renderBusy { false };
    std::atomic<int> status { (int) Status::Empty };
    std::atomic<float> progress { 0.0f };
    juce::String errorText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SIPitchProcessor)
};
