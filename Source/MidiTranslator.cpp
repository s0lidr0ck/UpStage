#include "MidiTranslator.h"

MidiTranslator::MidiTranslator() {}

void MidiTranslator::setRules (const juce::Array<MidiRule>& newRules)
{
    juce::ScopedLock sl (lock);
    rules = newRules;
    toggleStates.clear();
    for (int i = 0; i < rules.size(); ++i)
        toggleStates.add (false);
}

void MidiTranslator::addRule (const MidiRule& rule)
{
    juce::ScopedLock sl (lock);
    rules.add (rule);
    toggleStates.add (false);
}

void MidiTranslator::removeRule (int index)
{
    juce::ScopedLock sl (lock);
    if (juce::isPositiveAndBelow (index, rules.size()))
    {
        rules.remove (index);
        toggleStates.remove (index);
    }
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
    for (int i = 0; i < rules.size(); ++i)
    {
        if (matchesRule (rules[i], msg))
            return applyRule (i, rules[i], msg);
    }
    return msg;
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
    int msgChannel = msg.getChannel();
    if (rule.inChannel != 0 && rule.inChannel != msgChannel)
        return false;

    switch (rule.inType)
    {
        case MidiRule::InputType::CC:
            if (! msg.isController()) return false;
            if (msg.getControllerNumber() != rule.inNumber) return false;
            if (rule.mode == MidiRule::Mode::Normal && rule.inValue >= 0
                && msg.getControllerValue() != rule.inValue)
                return false;
            return true;

        case MidiRule::InputType::PC:
            if (! msg.isProgramChange()) return false;
            if (rule.inNumber >= 0 && msg.getProgramChangeNumber() != rule.inNumber) return false;
            return true;

        case MidiRule::InputType::NoteOn:
            if (! msg.isNoteOn()) return false;
            if (rule.inNumber >= 0 && msg.getNoteNumber() != rule.inNumber) return false;
            if (rule.mode == MidiRule::Mode::Normal && rule.inValue >= 0
                && msg.getVelocity() != rule.inValue)
                return false;
            return true;

        case MidiRule::InputType::NoteOff:
            if (! msg.isNoteOff()) return false;
            if (rule.inNumber >= 0 && msg.getNoteNumber() != rule.inNumber) return false;
            return true;
    }
    return false;
}

float MidiTranslator::applyExpressionCurve (float normalised, MidiRule::Curve curve)
{
    normalised = juce::jlimit (0.0f, 1.0f, normalised);

    switch (curve)
    {
        case MidiRule::Curve::Log:
            return std::log2 (1.0f + normalised);
        case MidiRule::Curve::Exp:
            return normalised * normalised;
        case MidiRule::Curve::Linear:
        default:
            return normalised;
    }
}

juce::MidiMessage MidiTranslator::applyRule (int ruleIndex, const MidiRule& rule,
                                              const juce::MidiMessage& src)
{
    int outVal = (rule.outValue >= 0) ? rule.outValue : src.getVelocity();

    if (rule.mode == MidiRule::Mode::Toggle)
    {
        bool& toggled = toggleStates.getReference (ruleIndex);
        toggled = ! toggled;
        outVal = toggled ? 127 : 0;
    }
    else if (rule.mode == MidiRule::Mode::Expression)
    {
        int rawCC = 0;
        if (src.isController())
            rawCC = src.getControllerValue();
        else if (src.isNoteOn())
            rawCC = src.getVelocity();

        float norm = (float) rawCC / 127.0f;
        float curved = applyExpressionCurve (norm, rule.exprCurve);
        outVal = rule.exprMin + (int)(curved * (float)(rule.exprMax - rule.exprMin) + 0.5f);
        outVal = juce::jlimit (0, 127, outVal);
    }

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
