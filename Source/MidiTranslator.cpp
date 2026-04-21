#include "MidiTranslator.h"

MidiTranslator::MidiTranslator() {}

void MidiTranslator::setRules (const juce::Array<MidiRule>& newRules)
{
    juce::ScopedLock sl (lock);
    rules = newRules;
}

void MidiTranslator::addRule (const MidiRule& rule)
{
    juce::ScopedLock sl (lock);
    rules.add (rule);
}

void MidiTranslator::removeRule (int index)
{
    juce::ScopedLock sl (lock);
    if (juce::isPositiveAndBelow (index, rules.size()))
        rules.remove (index);
}

juce::Array<MidiRule> MidiTranslator::getRules() const
{
    juce::ScopedLock sl (lock);
    return rules;
}

//==============================================================================
juce::MidiMessage MidiTranslator::process (const juce::MidiMessage& msg)
{
    juce::ScopedLock sl (lock);
    for (const auto& rule : rules)
    {
        if (matchesRule (rule, msg))
            return applyRule (rule, msg);
    }
    return msg; // pass-through
}

void MidiTranslator::processBuffer (juce::MidiBuffer& buffer)
{
    juce::MidiBuffer translated;
    for (const auto meta : buffer)
    {
        auto out = process (meta.getMessage());
        if (out.getRawDataSize() > 0)
            translated.addEvent (out, meta.samplePosition);
    }
    buffer = translated;
}

//==============================================================================
bool MidiTranslator::matchesRule (const MidiRule& rule, const juce::MidiMessage& msg)
{
    // Channel match (0 = any)
    int msgChannel = msg.getChannel(); // 1-16
    if (rule.inChannel != 0 && rule.inChannel != msgChannel)
        return false;

    switch (rule.inType)
    {
        case MidiRule::InputType::CC:
            if (! msg.isController()) return false;
            if (msg.getControllerNumber() != rule.inNumber) return false;
            if (rule.inValue >= 0 && msg.getControllerValue() != rule.inValue) return false;
            return true;

        case MidiRule::InputType::PC:
            if (! msg.isProgramChange()) return false;
            if (rule.inNumber >= 0 && msg.getProgramChangeNumber() != rule.inNumber) return false;
            return true;

        case MidiRule::InputType::NoteOn:
            if (! msg.isNoteOn()) return false;
            if (rule.inNumber >= 0 && msg.getNoteNumber() != rule.inNumber) return false;
            if (rule.inValue  >= 0 && msg.getVelocity()   != rule.inValue)  return false;
            return true;

        case MidiRule::InputType::NoteOff:
            if (! msg.isNoteOff()) return false;
            if (rule.inNumber >= 0 && msg.getNoteNumber() != rule.inNumber) return false;
            return true;
    }
    return false;
}

juce::MidiMessage MidiTranslator::applyRule (const MidiRule& rule,
                                              const juce::MidiMessage& src)
{
    // outValue == -1 means "passthrough source value"
    int outVal = (rule.outValue >= 0) ? rule.outValue : src.getVelocity();

    switch (rule.outType)
    {
        case MidiRule::OutputType::CC:
            return juce::MidiMessage::controllerEvent (rule.outChannel,
                                                       rule.outNumber,
                                                       juce::jlimit (0, 127, outVal));

        case MidiRule::OutputType::PC:
            return juce::MidiMessage::programChange (rule.outChannel, rule.outNumber);

        case MidiRule::OutputType::NoteOn:
            return juce::MidiMessage::noteOn (rule.outChannel,
                                              rule.outNumber,
                                              (juce::uint8) juce::jlimit (0, 127, outVal));

        case MidiRule::OutputType::NoteOff:
            return juce::MidiMessage::noteOff (rule.outChannel, rule.outNumber);
    }
    return src;
}
