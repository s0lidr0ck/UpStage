#pragma once
#include <JuceHeader.h>
#include "ChannelStrip.h"

class ChannelStripPanel : public juce::Component
{
public:
    /** @param stripId  Short identifier used to build MIDI-learn param IDs for
                        this strip's slots: "ch0".."ch3" for the channel strips,
                        "in" for the pre-FX input channel. */
    ChannelStripPanel (ChannelStrip& strip, const juce::String& stripId);
    ~ChannelStripPanel() override;

    void refresh();

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void mouseDown       (const juce::MouseEvent& e) override;
    void mouseDrag       (const juce::MouseEvent& e) override;
    void mouseUp         (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    static constexpr int kSlotHeight = 26;
    static constexpr int kSlotPadding = 2;
    /** Rack size. Mirrors ChannelStrip::kNumSlots - the panel draws one row per
        slot, empty or not. */
    static constexpr int kMaxVisibleSlots = ChannelStrip::kNumSlots;

    /** MIDI-learn param ID for one slot of one strip. Slots are addressed by
        position, so a footswitch keeps controlling the same spot on the board
        regardless of which plugin is loaded there. */
    static juce::String slotBypassParamId (const juce::String& stripId, int slotIndex)
    {
        return "slotBypass:" + stripId + ":" + juce::String (slotIndex);
    }

    int getPreferredHeight() const;

    /** Requests a plugin for `slot` (-1 = first free, from the strip's button). */
    std::function<void(int slot)> onAddPluginClicked;
    /** kind: 0 = NAM amp, 1 = NAM pedal, 2 = cab IR, 3 = space IR.
        slot: -1 = first free. */
    std::function<void(int kind, int slot)> onAddInternalRow;
    std::function<void(const juce::String& identifier, const juce::MemoryBlock& state, bool bypassed, int slot)> onPastePlugin;

    juce::Array<PluginAppearanceState> getAppearances() const;
    void setAppearances (const juce::Array<PluginAppearanceState>& appearances);

    // Plugin copy/paste clipboard — shared across all strip panels (channels and
    // the master FX bus) so a plugin can be copied from one and pasted into another.
    struct ClipboardEntry
    {
        juce::String pluginIdentifier;
        juce::String pluginName;
        juce::MemoryBlock stateData;
        bool bypassed = false;
    };

    static ClipboardEntry& getClipboard()
    {
        static ClipboardEntry clipboard;
        return clipboard;
    }

private:
    struct SlotInfo
    {
        int index = -1;
        juce::String name;
        juce::String nickname;
        bool bypassed = false;
        bool empty = true;
        juce::Colour tintColour { 0x00000000 };
    };

    ChannelStrip& strip;
    juce::String  stripId;
    SlotInfo slots[kMaxVisibleSlots];
    int numSlots = 0;

    struct PluginAppearance
    {
        juce::Colour tint { 0x00000000 };
        juce::String nickname;
    };

    std::map<juce::String, PluginAppearance> pluginAppearance;

    void rebuildSlots();
    int getSlotAt (int y) const;
    void showSlotContextMenu (int slotIndex);
    void addSlotMidiItems (juce::PopupMenu& menu, int slotIndex) const;
    void addTunerItem (juce::PopupMenu& menu, int slotIndex) const;
    void showNicknameDialog (int slotIndex);

    int  dragSourceSlot = -1;
    int  dragTargetSlot = -1;
    bool dragging = false;
    bool doubleClicked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripPanel)
};
