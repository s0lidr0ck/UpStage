#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

/**
 * FxBus  —  Master insert chain.
 *
 * Signal flow:
 *
 *   Channel sum ──► [Insert 1] ──► ... ──► [Insert 8] ──► Master Fader ──► Output
 *
 * A fixed rack of MAX_FX_SLOTS slots, each holding a plugin or empty. Occupied
 * slots process the stereo master mix in series, in slot order. Removing the
 * plugin in a slot empties that slot; it does not shift the others up.
 * The master fader gain is applied separately in MainComponent.
 */
class FxBus
{
public:
    static constexpr int MAX_FX_SLOTS = 8;

    explicit FxBus (juce::AudioPluginFormatManager& fm);
    ~FxBus();

    //==========================================================================
    void prepare        (double sampleRate, int blockSize);
    void releaseResources();
    void setPlayHead    (juce::AudioPlayHead* ph) { playHead = ph; }

    //==========================================================================
    /** Load into `slot` (-1 = first free). Fails when the slot is taken or the
        rack is full. */
    bool addPlugin   (const juce::PluginDescription& desc,
                      int slot,
                      std::function<void(bool success)> callback);
    /** Empty one slot; neighbouring slots are untouched. */
    void removePlugin (int slot);
    void clearAllPlugins();
    /** Exchange the contents of two slots. Either may be empty. */
    void swapSlots (int slotA, int slotB);
    void openPluginEditor (int slot);
    /** Close the plugin's editor window if open. Message thread only. */
    void closePluginEditor (int slot);
    /** True when that slot's editor window is currently open. */
    bool isPluginEditorOpen (int slot) const;
    void setPluginBypassed (int slot, bool bypassed);
    bool isPluginBypassed  (int slot) const;

    int  getNumSlots() const { return MAX_FX_SLOTS; }
    /** True when nothing is loaded there (or the index is out of range). */
    bool isSlotEmpty (int slot) const;
    /** Lowest-numbered empty slot, or -1 when full. */
    int  findFirstFreeSlot() const;

    /** How many slots are occupied. NOT a bound for slot indices - use
        getNumSlots() for that. */
    int  getNumPlugins() const;
    juce::AudioProcessor* getPlugin (int slot) const;
    juce::String getPluginIdentifier (int slot) const;

    //==========================================================================
    void setBypassed (bool b);
    bool isBypassed  () const;

    //==========================================================================
    /**
     * Process the master buffer through the insert chain in-place.
     */
    void processBlock (juce::AudioBuffer<float>& buffer, int numSamples);
    void processBlock (juce::AudioBuffer<float>& buffer, int numSamples, juce::MidiBuffer& midi);

    //==========================================================================
    struct State
    {
        bool  bypassed = false;
        juce::Array<PluginSlotState> plugins;
    };

    State getState() const;
    void  setState (const State& s);

    /** Sets the per-slot nickname + tint (message thread). */
    void setPluginAppearance (int chainIndex, juce::Colour tint, const juce::String& nickname);
    juce::Colour getPluginTint (int chainIndex) const;
    juce::String getPluginNickname (int chainIndex) const;

private:
    /** Install `entry` into `slot` (-1 = first free); deletes it and reports
        false when there is no room. Message thread only. */
    struct PluginEntry;
    bool placeEntry (PluginEntry* entry, int slot,
                     const std::function<void(bool success)>& callback);

    struct PluginEntry
    {
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::String identifier;
        bool         bypassed = false;

        /** Cached at load - getName() is not audio-thread safe. */
        juce::String cachedName;

        // Per-slot appearance (see ChannelStrip::PluginEntry). Message thread.
        juce::Colour tint { 0x00000000 };
        juce::String nickname;

        // Editor window for this plugin, if open. Non-owning safe pointer: the
        // window self-deletes when closed (auto-nulling this), but we also
        // force it closed before the processor is destroyed so the editor can
        // never outlive the plugin. Message thread only.
        juce::Component::SafePointer<juce::DocumentWindow> editorWindow;
    };

    juce::AudioPluginFormatManager& formatManager;

    juce::Array<PluginEntry*> pluginChain;
    mutable juce::CriticalSection chainLock;

    /** Audio-thread scratch, sized in prepare(). These were locals allocated
        per plugin per block; see processBlock. */
    juce::MidiBuffer         rtPluginMidi;
    juce::AudioBuffer<float> rtPadded;
    static constexpr int     kMaxPluginChannels = 8;


    /** Close any open editor window, then delete the entry (and its plugin).
        Call on the message thread with the entry already removed from
        pluginChain. */
    static void disposeEntry (PluginEntry* entry);

    std::atomic<bool> bypassed { false };
    juce::AudioPlayHead* playHead = nullptr;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxBus)
};
