#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** Optional external sidechain: a second (aux) stereo input. Hosts show it as "Sidechain" (in Ableton the
    panel with the input picker, Gain and Mix). The plugin decides when to listen to it (the "SC" switch). */
// BusesProperties is protected in juce::AudioProcessor, so this is used inside the processor constructor
#define SI_SIDECHAIN_BUSES BusesProperties() \
        .withInput ("Input", juce::AudioChannelSet::stereo(), true) \
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true) \
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)

namespace si::sc
{
inline bool layoutOk (const juce::AudioProcessor::BusesLayout& l)
{
    using juce::AudioChannelSet;
    const auto out = l.getMainOutputChannelSet();
    if (! ((out == AudioChannelSet::stereo() || out == AudioChannelSet::mono()) && l.getMainInputChannelSet() == out)) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto s = l.inputBuses[1];
        return s.isDisabled() || s == AudioChannelSet::mono() || s == AudioChannelSet::stereo();
    }
    return true;
}

/** Sidechain channels of this block (mono is doubled), empty when unused. */
struct Key
{
    const float* l = nullptr;
    const float* r = nullptr;
    explicit operator bool() const { return l != nullptr; }
};

inline Key get (juce::AudioProcessor& p, juce::AudioBuffer<float>& buffer, bool wanted)
{
    if (! wanted || p.getBusCount (true) < 2) return {};
    auto* bus = p.getBus (true, 1);
    if (bus == nullptr || ! bus->isEnabled()) return {};
    const int first = p.getChannelIndexInProcessBlockBuffer (true, 1, 0);
    const int num = bus->getNumberOfChannels();
    if (num <= 0 || first < 0 || first + num > buffer.getNumChannels()) return {};
    return { buffer.getReadPointer (first), buffer.getReadPointer (first + (num > 1 ? 1 : 0)) };
}

constexpr const char* kTip = "External sidechain: the detector listens to the Sidechain input (in Ableton pick a track in the plugin's Sidechain panel)";
} // namespace si::sc
