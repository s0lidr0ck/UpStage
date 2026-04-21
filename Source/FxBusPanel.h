#pragma once
#include <JuceHeader.h>
#include "FxBus.h"
#include "LevelMeter.h"

class FxBusPanel : public juce::Component,
                   public juce::Slider::Listener
{
public:
    std::function<void()> onAddPluginClicked;
    std::function<void(float /*dB*/)> onMasterFaderChanged;

    explicit FxBusPanel (FxBus& bus);
    ~FxBusPanel() override;

    void refresh();
    int getPreferredHeight() const;
    void syncFromBus();

    void pushMeterLevels (float inL, float inR, float outL, float outR);
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

    juce::Slider  masterFader;
    juce::Label   masterFaderLabel;

    LevelMeter meterIn  { LevelMeter::Orientation::Vertical, LevelMeter::ColourMode::Amber };
    LevelMeter meterOut { LevelMeter::Orientation::Vertical, LevelMeter::ColourMode::Green };
    juce::Label meterInLabel;
    juce::Label meterOutLabel;

    // Plugin slot display (painted, not child components)
    struct SlotInfo
    {
        int index = -1;
        juce::String name;
        bool bypassed = false;
        bool empty = true;
    };

    SlotInfo slots[FxBus::MAX_FX_SLOTS];

    static constexpr int kSlotHeight = 28;
    static constexpr int kSlotPadding = 2;

    void rebuildSlots();
    int getSlotAt (int y) const;
    void showSlotContextMenu (int slotIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxBusPanel)
};
