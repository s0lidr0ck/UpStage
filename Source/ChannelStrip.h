#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

/**
 * ChannelStrip
 *
 * Owns a fixed rack of kNumSlots plugin slots, each either holding a plugin or
 * empty. Audio flows through the occupied slots in slot order, skipping empties:
 * input → [slot0 → slot1 → ... → slotN] → output
 *
 * Slots are FIXED positions, not a packed list. Removing the plugin in slot 3
 * empties slot 3; it does not shift slots 4+ up. This matches how the strip is
 * drawn (a rack of slots), and keeps position-addressed MIDI slot bindings
 * pointing at the same spot on the board when a plugin above them is removed.
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
    // Slots
    /** Number of plugin slots per strip. A fixed rack, not a growing list. */
    static constexpr int kNumSlots = 12;

    int  getNumSlots() const { return kNumSlots; }

    /** True when nothing is loaded in that slot (or the index is out of range). */
    bool isSlotEmpty (int slot) const;

    /** Lowest-numbered empty slot, or -1 when the rack is full. */
    int  findFirstFreeSlot() const;

    //==========================================================================
    // Plugin management
    /** Load a VST3 plugin by its PluginDescription into `slot`.
        slot = -1 uses the first free slot. Fails (callback false) when the
        requested slot is taken or the rack is full. */
    bool addPlugin (const juce::PluginDescription& desc,
                    int slot,
                    std::function<void(bool success)> callback);

    /** Place an internal NAM row in `slot` (-1 = first free). Goes through the
        message queue (the same FIFO the async VST3 loads use) so a restore that
        mixes internal rows and VST3s still lands each one in its own slot.
        Kinds: 0 = amp head, 1 = pedal, 2 = cab IR, 3 = space IR. */
    void addInternalRow (int kind, int slot,
                         std::function<void(bool success)> callback = nullptr);

    /** Convenience: addInternalRow (0, -1). Kept for the existing call sites. */
    void addAmp (std::function<void(bool success)> callback = nullptr)
    {
        addInternalRow (0, -1, std::move (callback));
    }

    /** Empty one slot. Neighbouring slots are untouched. */
    void removePlugin (int slot);

    /** Empty every slot. */
    void clearAllPlugins();

    /** Exchange the contents of two slots. Either may be empty. */
    void swapSlots (int slotA, int slotB);

    /** Open the plugin's custom editor window (no-op for an empty slot). */
    void openPluginEditor (int slot);

    /** Close the plugin's editor window if it is open. Message thread only. */
    void closePluginEditor (int slot);

    /** True when that slot's editor window is currently open. */
    bool isPluginEditorOpen (int slot) const;

    /** Bypass a plugin in the chain (audio still flows but plugin is skipped). */
    void setPluginBypassed (int chainIndex, bool bypassed);

    /** Returns true if the plugin at `slot` is currently bypassed.
        Returns false for empty or out-of-range slots. */
    bool isPluginBypassed (int slot) const;

    /** How many slots are occupied. NOT a bound for slot indices - use
        getNumSlots() for that. Occupied slots need not be contiguous. */
    int getNumPlugins() const;

    /** The plugin in `slot`, or nullptr when the slot is empty. */
    juce::AudioProcessor* getPlugin (int slot) const;

    struct PluginEntry
    {
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::String identifier;
        bool         bypassed = false;

        // Per-slot appearance: stored on the entry so two instances of the
        // same plugin keep separate nicknames/tints, and reorder/remove carry
        // them automatically. Message thread only.
        juce::Colour tint { 0x00000000 };
        juce::String nickname;

        // Editor window for this plugin, if currently open. Non-owning safe
        // pointer: the window self-deletes when the user closes it (and this
        // auto-nulls), but we also force it closed before the processor is
        // destroyed so the editor can never outlive the plugin it points at.
        // Lives on the message thread only.
        juce::Component::SafePointer<juce::DocumentWindow> editorWindow;
    };

    /** The entry in `slot`, or nullptr when the slot is empty. */
    const PluginEntry* getPluginEntry (int slot) const;

    /** Sets the per-slot nickname + tint (message thread). */
    void setPluginAppearance (int slot, juce::Colour tint, const juce::String& nickname);

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

    /** Exactly kNumSlots entries at all times; nullptr means an empty slot.
        Never resized after construction, so a slot index is stable. */
    juce::Array<PluginEntry*> pluginChain;
    juce::CriticalSection     chainLock;

    /** Install `entry` into `slot` (-1 = first free) and fire the callback.
        Takes ownership; deletes the entry and reports false when there is no
        room. Message thread only. */
    bool placeEntry (PluginEntry* entry, int slot,
                     const std::function<void(bool success)>& callback);

    /** Close any open editor window, then delete the entry (and its plugin).
        MUST be called on the message thread and with the entry already removed
        from pluginChain so the audio thread can no longer reach it. */
    static void disposeEntry (PluginEntry* entry);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
