#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace si
{
struct FactoryPreset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values; // paramID -> real (unnormalised) value
};

/** Factory presets in code + user presets as XML files in Documents/SkillIssue/<plugin> Presets. */
class PresetManager
{
public:
    /** Parameters whose ID starts with one of `keep` are left alone when a preset loads (tempo setup, key/scale...). */
    PresetManager (juce::AudioProcessorValueTreeState&, const juce::String& pluginName, std::vector<FactoryPreset> factory,
                   juce::StringArray keep = { "tempo" });

    juce::StringArray getNames() const;
    int getNumFactory() const { return (int) factory.size(); }
    int getCurrent() const { return current; }
    void load (int index);
    void loadNext (int delta);
    bool saveUser (const juce::String& name);
    void refreshUserList();
    juce::File getUserDir() const;
    /** After setStateInformation: re-read the stored preset index. */
    void restoreFromState() { refreshUserList(); current = (int) state.state.getProperty ("presetIndex", 0); }

    std::function<void()> onChange;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String plugin;
    std::vector<FactoryPreset> factory;
    juce::Array<juce::File> userFiles;
    int current = 0;
    juce::StringArray keepPrefixes;
    bool keeps (const juce::String& id) const { for (auto& k : keepPrefixes) if (id.startsWith (k)) return true; return false; }
};
} // namespace si
