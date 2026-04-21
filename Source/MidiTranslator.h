#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

/**
 * MidiTranslator
 *
 * Processes incoming MIDI messages and applies user-defined translation rules.
 * Each rule maps an input message type/number/value to an output message.
 *
 * Rules are evaluated in order; first match wins.
 * Non-matching messages are passed through unchanged.
 *
 * Usage:
 *   translator.setRules (projectData.midiRules);
 *   auto outMsg = translator.process (inputMsg, inputChannel);
 */
class MidiTranslator
{
public:
    MidiTranslator();

    /** Replace all rules. Thread-safe (copies atomically). */
    void setRules (const juce::Array<MidiRule>& rules);

    /** Add a single rule. */
    void addRule (const MidiRule& rule);

    /** Remove rule at index. */
    void removeRule (int index);

    /** Get a copy of current rules. */
    juce::Array<MidiRule> getRules() const;

    /**
     * Process one incoming MIDI message.
     * @param msg     The incoming MIDI message.
     * @returns       Translated message (or original if no rule matched).
     *                Returns an empty (invalid) message to suppress/drop.
     */
    juce::MidiMessage process (const juce::MidiMessage& msg);

    /**
     * Process a buffer of MIDI messages in place.
     * Each message is translated; suppressed messages are removed.
     */
    void processBuffer (juce::MidiBuffer& buffer);

private:
    juce::CriticalSection  lock;
    juce::Array<MidiRule>  rules;

    juce::MidiMessage applyRule (const MidiRule& rule,
                                 const juce::MidiMessage& src);
    bool matchesRule (const MidiRule& rule,
                      const juce::MidiMessage& msg);
};
