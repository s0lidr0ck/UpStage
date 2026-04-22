#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

class MidiTranslator
{
public:
    MidiTranslator();

    void setRules (const juce::Array<MidiRule>& rules);
    void addRule (const MidiRule& rule);
    void removeRule (int index);
    juce::Array<MidiRule> getRules() const;

    juce::MidiMessage process (const juce::MidiMessage& msg);
    void processBuffer (juce::MidiBuffer& buffer);

private:
    juce::CriticalSection  lock;
    juce::Array<MidiRule>  rules;

    // Per-rule runtime state for toggle mode
    juce::Array<bool> toggleStates;

    juce::MidiMessage applyRule (int ruleIndex, const MidiRule& rule,
                                 const juce::MidiMessage& src);
    bool matchesRule (const MidiRule& rule,
                      const juce::MidiMessage& msg);
    float applyExpressionCurve (float normalised, MidiRule::Curve curve);
};
