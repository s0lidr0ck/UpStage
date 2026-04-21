#include "SceneManager.h"

SceneManager::SceneManager() {}

//==============================================================================
void SceneManager::captureScene (int idx,
                                  ChannelStrip** channels,
                                  const juce::String& name)
{
    if (! juce::isPositiveAndBelow (idx, NUM_SCENES)) return;
    scenes[idx].used = true;
    scenes[idx].name = name.isEmpty() ? ("Scene " + juce::String (idx + 1)) : name;
    for (int i = 0; i < NUM_CHANNELS; ++i)
        scenes[idx].channels[i] = channels[i]->getState();
}

bool SceneManager::applyScene (int idx,
                                ChannelStrip** channels)
{
    if (! juce::isPositiveAndBelow (idx, NUM_SCENES)) return false;
    if (! scenes[idx].used) return false;

    for (int i = 0; i < NUM_CHANNELS; ++i)
        channels[i]->setState (scenes[idx].channels[i]);

    return true;
}

//==============================================================================
bool SceneManager::isSceneUsed  (int idx) const
{
    return juce::isPositiveAndBelow (idx, NUM_SCENES) && scenes[idx].used;
}

juce::String SceneManager::getSceneName (int idx) const
{
    if (! juce::isPositiveAndBelow (idx, NUM_SCENES)) return {};
    return scenes[idx].name;
}

void SceneManager::setSceneName (int idx, const juce::String& name)
{
    if (juce::isPositiveAndBelow (idx, NUM_SCENES))
        scenes[idx].name = name;
}

void SceneManager::clearScene (int idx)
{
    if (juce::isPositiveAndBelow (idx, NUM_SCENES))
        scenes[idx] = Scene();
}

//==============================================================================
void SceneManager::saveToXml (juce::XmlElement& parent) const
{
    auto* scenesEl = parent.createNewChildElement ("Scenes");
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        if (! scenes[i].used) continue;
        auto* sceneEl = scenesEl->createNewChildElement ("Scene");
        sceneEl->setAttribute ("index", i);
        sceneEl->setAttribute ("name",  scenes[i].name);

        for (int c = 0; c < NUM_CHANNELS; ++c)
        {
            auto* chEl = sceneEl->createNewChildElement ("Channel");
            chEl->setAttribute ("index",       c);
            chEl->setAttribute ("name",        scenes[i].channels[c].name);
            chEl->setAttribute ("inputGain",   scenes[i].channels[c].inputGain);
            chEl->setAttribute ("outputGain",  scenes[i].channels[c].outputGain);
            chEl->setAttribute ("pan",         scenes[i].channels[c].pan);

            for (const auto& slot : scenes[i].channels[c].plugins)
            {
                auto* plugEl = chEl->createNewChildElement ("Plugin");
                plugEl->setAttribute ("id",       slot.pluginIdentifier);
                plugEl->setAttribute ("name",     slot.pluginName);
                plugEl->setAttribute ("bypassed", slot.isBypassed ? 1 : 0);
                if (slot.stateData.getSize() > 0)
                    plugEl->addTextElement (slot.stateData.toBase64Encoding());
            }
        }
    }
}

void SceneManager::loadFromXml (const juce::XmlElement& parent)
{
    for (int i = 0; i < NUM_SCENES; ++i)
        scenes[i] = Scene();

    auto* scenesEl = parent.getChildByName ("Scenes");
    if (scenesEl == nullptr) return;

    for (auto* sceneEl : scenesEl->getChildIterator())
    {
        int idx = sceneEl->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (idx, NUM_SCENES)) continue;

        scenes[idx].used = true;
        scenes[idx].name = sceneEl->getStringAttribute ("name");

        for (auto* chEl : sceneEl->getChildWithTagNameIterator ("Channel"))
        {
            int c = chEl->getIntAttribute ("index", -1);
            if (! juce::isPositiveAndBelow (c, NUM_CHANNELS)) continue;

            scenes[idx].channels[c].name        = chEl->getStringAttribute ("name");
            scenes[idx].channels[c].inputGain   = (float) chEl->getDoubleAttribute ("inputGain",  1.0);
            scenes[idx].channels[c].outputGain  = (float) chEl->getDoubleAttribute ("outputGain", 1.0);
            scenes[idx].channels[c].pan         = (float) chEl->getDoubleAttribute ("pan", 0.0);

            for (auto* plugEl : chEl->getChildWithTagNameIterator ("Plugin"))
            {
                PluginSlotState slot;
                slot.pluginIdentifier = plugEl->getStringAttribute ("id");
                slot.pluginName       = plugEl->getStringAttribute ("name");
                slot.isBypassed       = plugEl->getBoolAttribute   ("bypassed");
                auto b64 = plugEl->getAllSubText().trim();
                if (b64.isNotEmpty())
                    slot.stateData.fromBase64Encoding (b64);
                scenes[idx].channels[c].plugins.add (slot);
            }
        }
    }
}
