#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

/**
 * ChannelStrip
 *
 * Owns a chain of VST3 plugins (AudioProcessorGraph nodes).
 * Audio flows: input → [vst3_0 → vst3_1 → ... → vst3_N] → output
 *
 * When inactive the strip is bypassed (silence passed to output).
 * Each plugin can be individually bypassed via isBypassed.
 *
 * VST3 plugin states are serialised via getStateInformation / setStateInformation
 * so full round-trip save/load is supported.
 */
class ChannelStrip
{
public:
    explicit ChannelStrip (int index,
                           juce::AudioPluginFormatManager& formatManager);
    ~ChannelStrip();

    //==========================================================================
    // Setup
    void prepare (double sampleRate, int blockSize);
    void releaseResources();
    void setPlayHead (juce::AudioPlayHead* ph) { playHead = ph; }

    //==========================================================================
    // Plugin management
    /** Load a VST3 plugin by its PluginDescription. Appends to chain. */
    bool addPlugin (const juce::PluginDescription& desc,
                    std::function<void(bool success)> callback);

    /** Remove plugin at position in chain. */
    void removePlugin (int chainIndex);

    /** Move plugin within chain. */
    void movePlugin (int fromIndex, int toIndex);

    /** Open the plugin's custom editor window. */
    void openPluginEditor (int chainIndex);

    /** Bypass a plugin in the chain (audio still flows but plugin is skipped). */
    void setPluginBypassed (int chainIndex, bool bypassed);

    /** Returns true if the plugin at chainIndex is currently bypassed.
        Returns false for out-of-range indices. */
    bool isPluginBypassed (int chainIndex) const;

    int getNumPlugins() const;
    juce::AudioProcessor* getPlugin (int chainIndex) const;

    struct PluginEntry
    {
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::String identifier;
        bool         bypassed = false;
    };

    const PluginEntry& getPluginEntry (int chainIndex) const;

    //==========================================================================
    // Processing
    /** Process a block of audio. Call only when active or for monitoring. */
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    //==========================================================================
    // Active state
    void setActive (bool shouldBeActive);
    bool isActive() const { return active; }

    //==========================================================================
    // Gain
    void  setInputGain  (float gain);
    void  setOutputGain (float gain);
    float getInputGain()  const { return inputGainTarget.load();  }
    float getOutputGain() const { return outputGainTarget.load(); }

    // Pan (-1.0 = full left, 0.0 = centre, 1.0 = full right)
    void  setPan (float p)   { pan.store (juce::jlimit (-1.0f, 1.0f, p)); }
    float getPan() const     { return pan.load(); }

    //==========================================================================
    // Name
    void           setName (const juce::String& n) { name = n; }
    juce::String   getName() const                 { return name; }
    int            getIndex() const                { return channelIndex; }

    //==========================================================================
    // Save / Load state
    ChannelState   getState() const;
    void           setState  (const ChannelState& state);

private:
    int    channelIndex;
    bool   active = false;
    juce::String name;

    juce::SmoothedValue<float> inputGainSmoothed  { 1.0f };
    juce::SmoothedValue<float> outputGainSmoothed { 1.0f };
    std::atomic<float> inputGainTarget  { 1.0f };
    std::atomic<float> outputGainTarget { 1.0f };
    std::atomic<float> pan        { 0.0f };

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 256;

    juce::AudioPluginFormatManager& formatManager;

    juce::AudioPlayHead*      playHead = nullptr;
    juce::Array<PluginEntry*> pluginChain;
    juce::CriticalSection     chainLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
