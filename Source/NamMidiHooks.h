#pragma once

#include <JuceHeader.h>

/**
 * Rendezvous between the pop-out NAM editors and MainComponent's
 * MidiLearnManager. The editors live in their own windows and don't know the
 * app; MainComponent installs these hooks at startup.
 *
 * Param IDs are "nam:<instanceUid>:<knob>" - the uid is stored in each NAM
 * processor's state blob, so bindings saved in a project reconnect to the
 * right amp row on reload regardless of slot position.
 */
struct NamMidiHooks
{
    static inline std::function<void (const juce::String& paramID, float minV, float maxV)> beginLearn;
    static inline std::function<void (const juce::String& paramID)> clearBinding;
    static inline std::function<int (const juce::String& paramID)> getCc;   // -1 = unbound
};
