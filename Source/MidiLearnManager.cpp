#include "MidiLearnManager.h"

MidiLearnManager::MidiLearnManager() {}

//==============================================================================
void MidiLearnManager::registerParameter (const juce::String& paramID,
                                           float minValue, float maxValue)
{
    juce::ScopedLock sl (lock);
    for (auto& p : registeredParams)
        if (p.paramID == paramID) return; // already registered
    registeredParams.add ({ paramID, minValue, maxValue });
}

void MidiLearnManager::beginLearning (const juce::String& paramID)
{
    juce::ScopedLock sl (lock);
    learningParamID = paramID;
}

void MidiLearnManager::cancelLearning()
{
    juce::ScopedLock sl (lock);
    learningParamID.clear();
}

//==============================================================================
/*static*/ int MidiLearnManager::valueToCc (float value,
                                             float minValue,
                                             float maxValue) noexcept
{
    if (maxValue <= minValue) return 0;
    float norm = (value - minValue) / (maxValue - minValue);
    norm = juce::jlimit (0.0f, 1.0f, norm);
    return juce::roundToInt (norm * 127.0f);
}

//==============================================================================
void MidiLearnManager::setParameterTarget (const juce::String& paramID, float value)
{
    // No-op when feature is off — physical knobs always pass through immediately.
    if (! softTakeoverEnabled) return;

    juce::ScopedLock sl (lock);

    for (auto& b : bindings)
    {
        if (b.paramID != paramID) continue;

        // Convert the recalled parameter value back to a CC integer.
        b.targetCC  = valueToCc (value, b.minValue, b.maxValue);

        // Arm the WAITING state.  The binding stays suppressed until the
        // physical knob crosses (or snaps to) targetCC.
        b.softState = Binding::SoftTakeoverState::WAITING;

        // Note: lastPhysicalCC is intentionally preserved here.  If the
        // physical knob hasn't moved since the last scene recall the binding
        // will immediately check proximity on the very next CC message.
    }
}

//==============================================================================
bool MidiLearnManager::processMessage (const juce::MidiMessage& msg)
{
    if (! msg.isController()) return false;

    juce::ScopedLock sl (lock);

    const int   cc      = msg.getControllerNumber();
    const int   channel = msg.getChannel();
    const int   ccRaw   = msg.getControllerValue();    // 0–127
    const float norm    = ccRaw / 127.0f;

    // ---- Learning mode: record a new binding ----
    if (learningParamID.isNotEmpty())
    {
        // Remove any existing binding for this param, but carry its switch mode
        // over - re-learning which CC to listen to shouldn't silently reset how
        // the switch is interpreted.
        auto previousType = Binding::SwitchType::Latching;
        for (int i = bindings.size() - 1; i >= 0; --i)
            if (bindings[i].paramID == learningParamID)
            {
                previousType = bindings[i].switchType;
                bindings.remove (i);
            }

        // Find param info.
        float minV = 0.0f, maxV = 1.0f;
        for (auto& p : registeredParams)
            if (p.paramID == learningParamID) { minV = p.minValue; maxV = p.maxValue; break; }

        Binding b;
        b.paramID     = learningParamID;
        b.ccNumber    = cc;
        b.midiChannel = 0; // any
        b.minValue    = minV;
        b.maxValue    = maxV;
        b.switchType  = previousType;

        // Freshly learned — lock immediately (no target to catch up to yet).
        b.softState      = Binding::SoftTakeoverState::LOCKED;
        b.lastPhysicalCC = ccRaw;
        b.targetCC       = -1;

        bindings.add (b);
        learningParamID.clear();

        // Immediately fire the current value.
        float value = minV + norm * (maxV - minV);
        juce::String pid = b.paramID;
        juce::MessageManager::callAsync ([this, pid, value]() {
            listeners.call ([&] (Listener& l) { l.midiLearnParameterChanged (pid, value); });
        });
        return true;
    }

    // ---- Normal mode: apply existing bindings ----
    bool consumed = false;

    for (auto& b : bindings)
    {
        if (b.ccNumber != cc) continue;
        if (b.midiChannel != 0 && b.midiChannel != channel) continue;

        // Always track the physical position regardless of state.
        const int prevPhysicalCC = b.lastPhysicalCC;
        b.lastPhysicalCC = ccRaw;

        // ---- Soft takeover check ----
        if (softTakeoverEnabled && b.softState == Binding::SoftTakeoverState::WAITING)
        {
            // Condition 1 — proximity snap:
            //   The physical knob is within CATCHUP_THRESHOLD CC ticks of the
            //   target.  Lock immediately to avoid a tiny pop.
            const bool nearTarget = (std::abs (ccRaw - b.targetCC) < CATCHUP_THRESHOLD);

            // Condition 2 — crossover:
            //   The physical value has passed through the target between the
            //   previous message and this one (sign change of the delta).
            //   This also handles the case where prevPhysicalCC == -1 (first
            //   message ever) — we skip that check safely.
            bool crossedTarget = false;
            if (prevPhysicalCC >= 0)
            {
                const int prevDelta = prevPhysicalCC - b.targetCC;
                const int currDelta = ccRaw          - b.targetCC;
                // Sign change (or landing exactly on target) means crossover.
                crossedTarget = (prevDelta * currDelta <= 0);
            }

            if (nearTarget || crossedTarget)
            {
                // Promote to LOCKED — from now on every CC message fires through.
                b.softState = Binding::SoftTakeoverState::LOCKED;
            }
            else
            {
                // Still WAITING — suppress this CC message.
                consumed = true; // message was for a known binding, just silenced
                continue;
            }
        }

        // ---- LOCKED: fire the parameter update ----
        const float value = b.minValue + norm * (b.maxValue - b.minValue);
        const juce::String pid = b.paramID;

        juce::MessageManager::callAsync ([this, pid, value]() {
            listeners.call ([&] (Listener& l) { l.midiLearnParameterChanged (pid, value); });
        });
        consumed = true;
    }

    return consumed;
}

//==============================================================================
void MidiLearnManager::clearBinding (const juce::String& paramID)
{
    juce::ScopedLock sl (lock);
    for (int i = bindings.size() - 1; i >= 0; --i)
        if (bindings[i].paramID == paramID)
            bindings.remove (i);
}

void MidiLearnManager::clearAll()
{
    juce::ScopedLock sl (lock);
    bindings.clear();
}

int MidiLearnManager::getCcForParam (const juce::String& paramID) const
{
    juce::ScopedLock sl (lock);
    for (const auto& b : bindings)
        if (b.paramID == paramID)
            return b.ccNumber;
    return -1;
}

MidiLearnManager::Binding::SwitchType
MidiLearnManager::getSwitchType (const juce::String& paramID) const
{
    juce::ScopedLock sl (lock);
    for (const auto& b : bindings)
        if (b.paramID == paramID)
            return b.switchType;
    return Binding::SwitchType::Latching;
}

void MidiLearnManager::setSwitchType (const juce::String& paramID, Binding::SwitchType type)
{
    juce::ScopedLock sl (lock);
    for (auto& b : bindings)
        if (b.paramID == paramID)
            b.switchType = type;
}

int MidiLearnManager::getChannelForParam (const juce::String& paramID) const
{
    juce::ScopedLock sl (lock);
    for (const auto& b : bindings)
        if (b.paramID == paramID)
            return b.midiChannel;
    return -1;
}

//==============================================================================
void MidiLearnManager::saveToXml (juce::XmlElement& parent) const
{
    // processMessage() mutates bindings from the audio thread under this lock;
    // guard the read so a CC arriving mid-save can't invalidate the iterator.
    juce::ScopedLock sl (lock);

    auto* el = parent.createNewChildElement ("MidiLearn");

    // Persist the feature flag so the user's preference survives project reload.
    el->setAttribute ("softTakeover", softTakeoverEnabled ? 1 : 0);

    for (const auto& b : bindings)
    {
        auto* bEl = el->createNewChildElement ("Binding");
        bEl->setAttribute ("paramID",     b.paramID);
        bEl->setAttribute ("cc",          b.ccNumber);
        bEl->setAttribute ("channel",     b.midiChannel);
        bEl->setAttribute ("minValue",    b.minValue);
        bEl->setAttribute ("maxValue",    b.maxValue);
        bEl->setAttribute ("switchType",  (int) b.switchType);
        // Soft takeover runtime state (lastPhysicalCC, softState, targetCC) is
        // intentionally NOT serialised — it is re-armed by setParameterTarget()
        // after project load, matching the freshly restored parameter values.
    }
}

void MidiLearnManager::loadFromXml (const juce::XmlElement& parent)
{
    // Guard against a CC arriving on the audio thread (processMessage) while we
    // clear and rebuild bindings during a project load.
    juce::ScopedLock sl (lock);

    bindings.clear();
    auto* el = parent.getChildByName ("MidiLearn");
    if (el == nullptr) return;

    softTakeoverEnabled = (el->getIntAttribute ("softTakeover", 1) != 0);

    for (auto* bEl : el->getChildWithTagNameIterator ("Binding"))
    {
        Binding b;
        b.paramID     = bEl->getStringAttribute ("paramID");
        b.ccNumber    = bEl->getIntAttribute    ("cc", -1);
        b.midiChannel = bEl->getIntAttribute    ("channel", 0);
        b.minValue    = (float) bEl->getDoubleAttribute ("minValue", 0.0);
        b.maxValue    = (float) bEl->getDoubleAttribute ("maxValue", 1.0);
        // "momentary" is the pre-SingleValue attribute: 0 = latching,
        // 1 = momentary. Read it only when the newer attribute is absent.
        const int legacy = bEl->getIntAttribute ("momentary", 0) != 0 ? 1 : 0;
        b.switchType = (Binding::SwitchType) juce::jlimit (
            0, 2, bEl->getIntAttribute ("switchType", legacy));

        // Runtime soft takeover fields — start LOCKED until caller arms them.
        b.softState      = Binding::SoftTakeoverState::LOCKED;
        b.lastPhysicalCC = -1;
        b.targetCC       = -1;

        if (b.ccNumber >= 0 && b.paramID.isNotEmpty())
            bindings.add (b);
    }
}
