#pragma once
#include <JuceHeader.h>

class FaderChangeAction : public juce::UndoableAction
{
public:
    FaderChangeAction (juce::Slider& s, double oldVal, double newVal)
        : slider (s), oldValue (oldVal), newValue (newVal) {}

    bool perform() override
    {
        slider.setValue (newValue, juce::sendNotificationSync);
        return true;
    }

    bool undo() override
    {
        slider.setValue (oldValue, juce::sendNotificationSync);
        return true;
    }

    int getSizeInUnits() override { return 16; }

private:
    juce::Slider& slider;
    double oldValue, newValue;
};
