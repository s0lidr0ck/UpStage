#include "SceneManager.h"

SceneManager::SceneManager() {}

//==============================================================================
void SceneManager::captureScene (int idx,
                                  ChannelStrip** channels,
                                  const GlobalState& globals,
                                  const juce::String& name)
{
    if (! juce::isPositiveAndBelow (idx, NUM_SCENES)) return;
    scenes[idx].used = true;
    scenes[idx].name = name.isEmpty() ? ("Scene " + juce::String (idx + 1)) : name;
    for (int i = 0; i < NUM_CHANNELS; ++i)
        scenes[idx].channels[i] = channels[i]->getState();
    scenes[idx].globals = globals;
}

SceneManager::ApplyResult SceneManager::applyScene (int idx,
                                                     ChannelStrip** channels)
{
    ApplyResult result;
    if (! juce::isPositiveAndBelow (idx, NUM_SCENES)) return result;
    if (! scenes[idx].used) return result;

    for (int i = 0; i < NUM_CHANNELS; ++i)
        channels[i]->setState (scenes[idx].channels[i]);

    result.success = true;
    result.globals = scenes[idx].globals;
    return result;
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

ChannelState SceneManager::getChannelState (int sceneIdx, int chanIdx) const
{
    if (juce::isPositiveAndBelow (sceneIdx, NUM_SCENES) &&
        juce::isPositiveAndBelow (chanIdx, NUM_CHANNELS))
        return scenes[sceneIdx].channels[chanIdx];
    return {};
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

        // Global state (gate, trim, FxBus)
        auto* globEl = sceneEl->createNewChildElement ("Globals");
        globEl->setAttribute ("gateEnabled",  scenes[i].globals.gateEnabled ? 1 : 0);
        globEl->setAttribute ("gateThreshDb", scenes[i].globals.gateThreshDb);
        globEl->setAttribute ("inputTrimDb",  scenes[i].globals.inputTrimDb);
        globEl->setAttribute ("fxBypassed",   scenes[i].globals.fxBusState.bypassed ? 1 : 0);
        for (const auto& slot : scenes[i].globals.fxBusState.plugins)
        {
            auto* fxPlugEl = globEl->createNewChildElement ("FxPlugin");
            fxPlugEl->setAttribute ("id",       slot.pluginIdentifier);
            fxPlugEl->setAttribute ("name",     slot.pluginName);
            fxPlugEl->setAttribute ("bypassed", slot.isBypassed ? 1 : 0);
            if (slot.stateData.getSize() > 0)
                fxPlugEl->addTextElement (slot.stateData.toBase64Encoding());
        }

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

        if (auto* globEl = sceneEl->getChildByName ("Globals"))
        {
            scenes[idx].globals.gateEnabled  = globEl->getIntAttribute ("gateEnabled", 0) != 0;
            scenes[idx].globals.gateThreshDb = (float) globEl->getDoubleAttribute ("gateThreshDb", -60.0);
            scenes[idx].globals.inputTrimDb  = (float) globEl->getDoubleAttribute ("inputTrimDb", 0.0);
            scenes[idx].globals.fxBusState.bypassed = globEl->getIntAttribute ("fxBypassed", 0) != 0;
            scenes[idx].globals.fxBusState.plugins.clear();
            for (auto* fxPlugEl : globEl->getChildWithTagNameIterator ("FxPlugin"))
            {
                PluginSlotState slot;
                slot.pluginIdentifier = fxPlugEl->getStringAttribute ("id");
                slot.pluginName       = fxPlugEl->getStringAttribute ("name");
                slot.isBypassed       = fxPlugEl->getBoolAttribute   ("bypassed");
                auto b64 = fxPlugEl->getAllSubText().trim();
                if (b64.isNotEmpty())
                    slot.stateData.fromBase64Encoding (b64);
                scenes[idx].globals.fxBusState.plugins.add (slot);
            }
        }

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
