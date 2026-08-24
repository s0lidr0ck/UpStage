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

    /** Translate a buffer in place. Called on the AUDIO thread.

        Takes the rule lock with a TRY-lock: if the message thread is mid-edit
        (setRules copies an Array while holding it), this block passes through
        untranslated rather than stalling the audio thread. One block of
        untranslated MIDI beats a dropout, and rules only change when the user
        edits or imports them. */
    void processBuffer (juce::MidiBuffer& buffer);

private:
    juce::CriticalSection  lock;

    /** Reusable output buffer for processBuffer. A local would heap-allocate
        on every audio block; clear() keeps the capacity. Audio thread only. */
    juce::MidiBuffer       translated;

    /** process() without taking the lock - the caller already holds it. */
    juce::MidiMessage processLocked (const juce::MidiMessage& msg);
    juce::Array<MidiRule>  rules;

    // Per-rule runtime state for toggle mode
    juce::Array<bool> toggleStates;

    juce::MidiMessage applyRule (int ruleIndex, const MidiRule& rule,
                                 const juce::MidiMessage& src);
    bool matchesRule (const MidiRule& rule,
                      const juce::MidiMessage& msg);
    float applyExpressionCurve (float normalised, MidiRule::Curve curve);
};
