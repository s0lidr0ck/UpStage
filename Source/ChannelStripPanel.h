#pragma once
#include <JuceHeader.h>
#include "ChannelStrip.h"

class ChannelStripPanel : public juce::Component
{
public:
    explicit ChannelStripPanel (ChannelStrip& strip);
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
    static constexpr int kMaxVisibleSlots = 8;

    int getPreferredHeight() const;

    std::function<void()> onAddPluginClicked;
    std::function<void(const juce::String& identifier, const juce::MemoryBlock& state, bool bypassed)> onPastePlugin;

    juce::Array<PluginAppearanceState> getAppearances() const;
    void setAppearances (const juce::Array<PluginAppearanceState>& appearances);

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
    void showNicknameDialog (int slotIndex);

    int  dragSourceSlot = -1;
    int  dragTargetSlot = -1;
    bool dragging = false;
    bool doubleClicked = false;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripPanel)
};
