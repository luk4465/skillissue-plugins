#include "PresetManager.h"

namespace si
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, const juce::String& pluginName, std::vector<FactoryPreset> f, juce::StringArray keep)
    : state (s), plugin (pluginName), factory (std::move (f)), keepPrefixes (std::move (keep))
{
    refreshUserList();
}

juce::File PresetManager::getUserDir() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("SkillIssue")
        .getChildFile (plugin + " Presets");
}

void PresetManager::refreshUserList()
{
    userFiles = getUserDir().findChildFiles (juce::File::findFiles, false, "*.sipreset");
    userFiles.sort();
}

juce::StringArray PresetManager::getNames() const
{
    juce::StringArray n;
    for (auto& p : factory) n.add (p.name);
    for (auto& f : userFiles) n.add (f.getFileNameWithoutExtension());
    return n;
}

void PresetManager::load (int index)
{
    const int total = (int) factory.size() + userFiles.size();
    if (total == 0) return;
    index = (index % total + total) % total;

    if (index < (int) factory.size())
    {
        // reset everything to default, then apply the preset's values
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! keeps (rp->getParameterID()))   // keep the user setup (tempo, key...)
                    rp->setValueNotifyingHost (rp->getDefaultValue());
        for (auto& [id, v] : factory[(size_t) index].values)
            if (auto* rp = state.getParameter (id))
                rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    }
    else if (auto xml = juce::XmlDocument::parse (userFiles[index - (int) factory.size()]))
    {
        for (auto* child : xml->getChildIterator())
            if (child->hasTagName ("PARAM"))
            {
                const auto id = child->getStringAttribute ("id");
                if (keeps (id)) continue;
                if (auto* rp = state.getParameter (id))
                    rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child->getDoubleAttribute ("value")));
            }
    }
    current = index;
    state.state.setProperty ("presetIndex", current, nullptr);
    if (onChange) onChange();
}

void PresetManager::loadNext (int delta) { load (current + delta); }

bool PresetManager::saveUser (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = getUserDir();
    dir.createDirectory();
    juce::XmlElement xml ("SIPRESET");
    xml.setAttribute ("plugin", plugin);
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }
    const auto file = dir.getChildFile (clean + ".sipreset");
    if (! xml.writeTo (file)) return false;
    refreshUserList();
    current = (int) factory.size() + userFiles.indexOf (file);
    state.state.setProperty ("presetIndex", current, nullptr);
    if (onChange) onChange();
    return true;
}
} // namespace si
