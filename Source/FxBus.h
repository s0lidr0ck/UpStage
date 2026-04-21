#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

/**
 * FxBus  —  Master insert chain.
 *
 * Signal flow:
 *
 *   Channel sum ──► [Insert 1] ──► [Insert 2] ──► [Insert 3] ──► [Insert 4] ──► Master Fader ──► Output
 *
 * Up to MAX_FX_SLOTS plugins process the stereo master mix in series.
 * The master fader gain is applied separately in MainComponent.
 */
class FxBus
{
public:
    static constexpr int MAX_FX_SLOTS = 4;

    explicit FxBus (juce::AudioPluginFormatManager& fm);
    ~FxBus();

    //==========================================================================
    void prepare        (double sampleRate, int blockSize);
    void releaseResources();
    void setPlayHead    (juce::AudioPlayHead* ph) { playHead = ph; }

    //==========================================================================
    bool addPlugin   (const juce::PluginDescription& desc,
                      std::function<void(bool success)> callback);
    void removePlugin (int index);
    void openPluginEditor (int index);
    void setPluginBypassed (int index, bool bypassed);
    bool isPluginBypassed  (int index) const;
    int  getNumPlugins() const;
    juce::AudioProcessor* getPlugin (int index) const;

    //==========================================================================
    void setBypassed (bool b);
    bool isBypassed  () const;

    //==========================================================================
    /**
     * Process the master buffer through the insert chain in-place.
     */
    void processBlock (juce::AudioBuffer<float>& buffer, int numSamples);

    //==========================================================================
    struct State
    {
        bool  bypassed = false;
        juce::Array<PluginSlotState> plugins;
    };

    State getState() const;
    void  setState (const State& s);

private:
    struct PluginEntry
    {
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::String identifier;
        bool         bypassed = false;
    };

    juce::AudioPluginFormatManager& formatManager;

    juce::Array<PluginEntry*> pluginChain;
    mutable juce::CriticalSection chainLock;

    std::atomic<bool> bypassed { false };
    juce::AudioPlayHead* playHead = nullptr;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxBus)
};
