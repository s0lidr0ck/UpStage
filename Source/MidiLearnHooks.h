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

    /** Switch mode for a bound on/off target. Momentary = sends 127 on press
        and 0 on release; latching (the default) = one message per press,
        alternating value. Cannot be auto-detected - the streams are identical. */
    static inline std::function<bool (const juce::String& paramID)> isMomentary;
    static inline std::function<void (const juce::String& paramID, bool momentary)> setMomentary;
};
