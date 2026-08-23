#pragma once
#include <JuceHeader.h>

/**
 * MidiLearnManager
 *
 * Manages MIDI CC → parameter bindings.
 *
 * Any slider/knob in the UI can call:
 *   1. midiLearn.beginLearning(paramID)  — arms learning for that param
 *   2. User wiggles a MIDI CC knob on their controller
 *   3. processMessage() detects the CC and records the binding
 *   4. Future CC messages on that CC number auto-update the parameter
 *
 * Bindings are stored in the project file.
 *
 * Wire into your right-click context menu on any Slider:
 *   slider.onMouseDown / MouseListener → show "MIDI Learn" menu item
 *
 * Integration:
 *   - Call processMessage() from MainComponent::handleIncomingMidiMessage
 *   - Register parameters with registerParameter()
 *   - MidiLearnManager::Listener gets notified when a parameter value changes
 *
 * ---- Soft Takeover (Feature 4) ----
 *
 * When softTakeoverEnabled = true (default), loading a scene or project will
 * arm soft takeover for all bound parameters.  The physical knob is silently
 * ignored until it "catches up" to the stored value, preventing jarring jumps.
 *
 * State machine per binding:
 *
 *   WAITING   → physical hasn't crossed the target yet   → suppress CC updates
 *   LOCKED    → physical has crossed or matched target   → pass CC updates through
 *
 * Transition rule  (called inside processMessage every tick):
 *   if |incoming_cc - target_cc| < CATCHUP_THRESHOLD  →  LOCKED
 *   if physical crosses target (sign change of delta)  →  LOCKED
 *
 * Call setParameterTarget(paramID, normalised_value) after any scene/project
 * recall to arm the takeover for that parameter.
 */
class MidiLearnManager
{
public:
    //==========================================================================
    // Parameter registration
    struct Binding
    {
        juce::String paramID;
        int          ccNumber    = -1;
        int          midiChannel = 0;   // 0 = any channel
        float        minValue    = 0.0f;
        float        maxValue    = 1.0f;

        //----------------------------------------------------------------------
        // Switch behaviour (only meaningful for on/off targets)

        /** How the bound physical switch reports a press.
            A momentary switch sends 127 on press and 0 on release - two
            messages per press. A latching switch sends ONE message per press,
            alternating 0 / 127, and tracks its own state.
            The two message streams are identical, so this cannot be detected
            automatically - it has to be told.
            Default false (latching), matching most amp-modeller footcontrollers
            and the follow-the-value convention the other switch params use. */
        bool momentarySwitch = false;

        //----------------------------------------------------------------------
        // Soft takeover state (not serialised — re-armed on every recall)

        /** Soft takeover states for this binding. */
        enum class SoftTakeoverState { WAITING, LOCKED };

        /** Current takeover state.  LOCKED after physical knob catches up. */
        SoftTakeoverState softState = SoftTakeoverState::LOCKED;

        /** Last raw CC value (0–127) seen from the physical controller. */
        int lastPhysicalCC = -1;   // -1 = never seen

        /** Target CC value (0–127) set by the last scene / project recall.
            Used only while state == WAITING. */
        int targetCC = -1;         // -1 = no target armed
    };

    /** Callback fired on message thread when a parameter changes via MIDI. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void midiLearnParameterChanged (const juce::String& paramID, float value) = 0;
    };

    //==========================================================================
    MidiLearnManager();

    void addListener    (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    //==========================================================================
    /** Register a parameter so it can be learned.
        minValue/maxValue map the CC range 0–127 to the parameter's range. */
    void registerParameter (const juce::String& paramID,
                            float minValue = 0.0f,
                            float maxValue = 1.0f);

    /** Arm MIDI learn for a specific param. Next CC wiggle binds to it. */
    void beginLearning (const juce::String& paramID);

    /** Cancel any pending learn without making a binding. */
    void cancelLearning();

    bool isLearning() const { return learningParamID.isNotEmpty(); }
    juce::String getLearningParam() const { return learningParamID; }

    //==========================================================================
    /** Process a MIDI message. Returns true if message was consumed by a binding. */
    bool processMessage (const juce::MidiMessage& msg);

    //==========================================================================
    /**
     * Soft takeover: arm takeover for a parameter after a scene / project recall.
     *
     * @param paramID   The parameter identifier (must match a registered binding).
     * @param value     The new stored value in the parameter's own range
     *                  (i.e. the value that was just recalled).
     *
     * This converts `value` back to a CC integer (0–127) and puts the binding
     * into WAITING state.  The physical knob must pass through that CC position
     * before updates are re-enabled.
     *
     * Call this for every bound parameter after loadProjectData() / applyScene().
     * If soft takeover is disabled the call is a no-op.
     */
    void setParameterTarget (const juce::String& paramID, float value);

    //==========================================================================
    /** Enable / disable the soft takeover feature globally.  Default: true. */
    bool softTakeoverEnabled = true;

    //==========================================================================
    // Bindings
    const juce::Array<Binding>& getBindings() const { return bindings; }
    void  clearBinding  (const juce::String& paramID);
    void  clearAll();

    /** CC number bound to paramID, or -1 if not bound. Thread-safe. */
    int getCcForParam (const juce::String& paramID) const;

    /** Switch behaviour for a bound on/off target. See Binding::momentarySwitch.
        Returns false (latching) when paramID isn't bound. Thread-safe. */
    bool isMomentarySwitch (const juce::String& paramID) const;
    void setMomentarySwitch (const juce::String& paramID, bool momentary);
    /** MIDI channel bound to paramID (0 = any), or -1 if not bound. Thread-safe. */
    int getChannelForParam (const juce::String& paramID) const;

    //==========================================================================
    // Serialization
    void saveToXml   (juce::XmlElement& parent) const;
    void loadFromXml (const juce::XmlElement& parent);

private:
    //----------------------------------------------------------------------
    /** CC proximity threshold for snap-to-lock (raw CC units, 0–127). */
    static constexpr int CATCHUP_THRESHOLD = 3;

    //----------------------------------------------------------------------
    struct ParamInfo
    {
        juce::String paramID;
        float        minValue, maxValue;
    };

    juce::Array<ParamInfo>       registeredParams;
    juce::Array<Binding>         bindings;
    juce::String                 learningParamID;
    juce::ListenerList<Listener> listeners;
    mutable juce::CriticalSection lock;

    /** Convert a normalised value (0–1 within the binding's own range) to CC. */
    static int valueToCc (float value, float minValue, float maxValue) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearnManager)
};
