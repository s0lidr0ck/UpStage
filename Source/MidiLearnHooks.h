#pragma once

#include <JuceHeader.h>

/**
 * Rendezvous between UI that lives outside MainComponent and MainComponent's
 * MidiLearnManager. The pop-out NAM editors and the channel/FX slot panels
 * don't know the app; MainComponent installs these hooks at startup.
 *
 * Param ID conventions:
 *   "nam:<instanceUid>:<knob>"  - a NAM editor knob. The uid is stored in each
 *                                 NAM processor's state blob, so bindings saved
 *                                 in a project reconnect to the right amp row
 *                                 on reload regardless of slot position.
 *   "slotBypass:<strip>:<slot>" - on/off for one effect slot, addressed by
 *                                 position. <strip> is "ch0".."ch3", "in" or
 *                                 "fx". Position-based (not plugin-based) so a
 *                                 footswitch keeps controlling the same spot on
 *                                 the board no matter what is loaded there.
 *   "tapTempo"                  - momentary: each press counts as one tap.
 */
struct MidiLearnHooks
{
    static inline std::function<void (const juce::String& paramID, float minV, float maxV)> beginLearn;
    static inline std::function<void (const juce::String& paramID)> clearBinding;
    static inline std::function<int (const juce::String& paramID)> getCc;   // -1 = unbound

    /** Switch type for a bound on/off target, as an int matching
        MidiLearnManager::Binding::SwitchType:
          0 = Latching     - one message per press, alternating 0 / 127
          1 = Momentary    - 127 held, 0 on release
          2 = SingleValue  - the same value every press
        Cannot be auto-detected between 0 and 1 - those streams are identical. */
    static inline std::function<int (const juce::String& paramID)> getSwitchType;
    static inline std::function<void (const juce::String& paramID, int type)> setSwitchType;

    /** The slot marked as the tuner, as "<strip>:<slot>", or empty when none
        is marked. Marking a slot clears any previous mark - one per project. */
    static inline std::function<juce::String ()> getTunerSlot;
    static inline std::function<void (const juce::String& slotAddress)> setTunerSlot;
};
