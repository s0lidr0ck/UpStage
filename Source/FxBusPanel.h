#pragma once
#include <JuceHeader.h>
#include "FxBus.h"
#include "LevelMeter.h"
#include "VuMeter.h"
#include "StereoSpreadMeter.h"
#include "GoniometerMeter.h"
#include "ChannelStripPanel.h"   // shared plugin clipboard + PluginAppearanceState
#include "ProjectState.h"

class FxBusPanel : public juce::Component,
                   public juce::Slider::Listener
{
public:
    /** Requests a plugin for `slot` (-1 = first free, from the Add Insert button). */
    std::function<void(int slot)> onAddPluginClicked;
    std::function<void(float /*dB*/)> onMasterFaderChanged;
    std::function<void(const juce::String& identifier, const juce::MemoryBlock& state, bool bypassed, int slot)> onPastePlugin;

    juce::Array<PluginAppearanceState> getAppearances() const;
    void setAppearances (const juce::Array<PluginAppearanceState>& appearances);

    explicit FxBusPanel (FxBus& bus);
    ~FxBusPanel() override;

    void refresh();
    int getPreferredHeight() const;
    void syncFromBus();

    void pushMeterLevels (float inL, float inR, float outL, float outR);
    void pushStereo (float left, float right);
    void pushLufs (float lufsDb);
    void pushGoniometerSamples (const float* leftData, const float* rightData, int numSamples);
    void setMasterFaderDb (float db);

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent& e) override;

    void sliderValueChanged (juce::Slider* s) override;

private:
    FxBus& bus;

    juce::Label       headerLabel;
    juce::ToggleButton bypassToggle { "Bypass" };
    juce::TextButton  addFxButton   { "+ Add Insert" };

    juce::Slider  masterKnob;
    juce::Label   masterKnobCaption;  // "VOLUME" caption above the readout
    juce::Label   masterKnobLabel;

    VuMeter vuMeterIn  { VuMeter::Theme::Amber };
    VuMeter vuMeterOut { VuMeter::Theme::Amber };
    VuMeter peakMeter  { VuMeter::Theme::Black };
    VuMeter lufsMeter  { VuMeter::Theme::Blue };
    StereoSpreadMeter spreadMeter;
    GoniometerMeter   goniometer;

    // Plugin slot display (painted, not child components)
    struct SlotInfo
    {
        int index = -1;
        juce::String name;
        juce::String nickname;
        bool bypassed = false;
        bool empty = true;
    };

    SlotInfo slots[FxBus::MAX_FX_SLOTS];

    // Per-plugin nickname keyed by plugin name (mirrors ChannelStripPanel).
    struct PluginAppearance { juce::String nickname; };
    std::map<juce::String, PluginAppearance> pluginAppearance;

    static constexpr int kSlotHeight = 28;
    static constexpr int kSlotPadding = 2;

    void rebuildSlots();
    int getSlotAt (int y) const;
    void showSlotContextMenu (int slotIndex);
    void addSlotMidiItems (juce::PopupMenu& menu, int slotIndex) const;
    void showNicknameDialog (int slotIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxBusPanel)
};
