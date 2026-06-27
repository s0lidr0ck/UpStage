#pragma once
#include <JuceHeader.h>
#include "AppConfig.h"
#include "ProjectState.h"
#include "ChannelStrip.h"

/**
 * SceneManager
 *
 * Stores up to NUM_SCENES (8) snapshots of all channel states within a project.
 * Each scene = the complete state of all 4 ChannelStrips at the time of capture.
 *
 * MIDI integration (handled in MainComponent):
 *   PC 4  → load scene 0 (Verse)
 *   PC 5  → load scene 1 (Chorus)
 *   PC 6  → load scene 2 (Solo)
 *   PC 7  → load scene 3 (Bridge)
 *   ... up to PC 11 → scene 7
 *
 * Scenes are saved inside the .upstage project file as <Scenes> children.
 */
class SceneManager
{
public:
    SceneManager();

    struct GlobalState
    {
        bool  gateEnabled   = false;
        float gateThreshDb  = -60.0f;
        float gateAttackMs  = 5.0f;
        float gateHoldMs    = 50.0f;
        float gateReleaseMs = 100.0f;
        float inputTrimDb   = 0.0f;
        ProjectData::FxBusState fxBusState;
        ChannelState inputChannelState;
        float inputDirectMix = 0.0f;
    };

    //==========================================================================
    void captureScene (int sceneIndex,
                       ChannelStrip** channels,
                       const GlobalState& globals = {},
                       const juce::String& name = {});

    struct ApplyResult
    {
        bool  success       = false;
        GlobalState globals;
    };

    ApplyResult applyScene (int sceneIndex,
                            ChannelStrip** channels);

    //==========================================================================
    // Scene metadata
    bool            isSceneUsed  (int index) const;
    juce::String    getSceneName (int index) const;
    void            setSceneName (int index, const juce::String& name);
    void            clearScene   (int index);
    ChannelState    getChannelState (int sceneIndex, int channelIndex) const;

    int  pcBaseOffset = 4; // PC 4 = scene 0, PC 5 = scene 1, ...

    //==========================================================================
    // Serialization (called by ProjectState)
    void saveToXml   (juce::XmlElement& parent) const;
    void loadFromXml (const juce::XmlElement& parent);

private:
    struct Scene
    {
        bool         used = false;
        juce::String name;
        ChannelState channels[NUM_CHANNELS];
        GlobalState  globals;
    };

    Scene scenes[NUM_SCENES];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneManager)
};
