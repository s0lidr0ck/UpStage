#include "ProjectState.h"

ProjectState::ProjectState() {}

//==============================================================================
bool ProjectState::saveToFile (const juce::File& file, const ProjectData& data)
{
    auto root = std::make_unique<juce::XmlElement> ("UpStageProject");
    root->setAttribute ("name",          data.projectName);
    root->setAttribute ("activeChannel", data.activeChannel);
    root->setAttribute ("version",       "1");

    // ---- Audio settings ----
    auto* audio = root->createNewChildElement ("AudioSettings");
    audio->setAttribute ("asioDevice",     data.asioDeviceName);
    audio->setAttribute ("sampleRate",     data.sampleRate);
    audio->setAttribute ("bufferSize",     data.bufferSize);
    audio->setAttribute ("inputChannel",   data.inputChannel);
    audio->setAttribute ("outputChannelL", data.outputChannelL);
    audio->setAttribute ("outputChannelR", data.outputChannelR);

    // ---- MIDI settings ----
    auto* midi = root->createNewChildElement ("MidiSettings");
    midi->setAttribute ("inputDevice",  data.midiInputDevice);
    midi->setAttribute ("outputDevice", data.midiOutputDevice);

    auto* rules = midi->createNewChildElement ("Rules");
    for (const auto& rule : data.midiRules)
        rules->addChildElement (ruleToXml (rule));

    // ---- Input Router ----
    auto* router = root->createNewChildElement ("InputRouter");
    router->setAttribute ("useLoopFile",  data.useLoopFile ? 1 : 0);
    router->setAttribute ("loopFilePath", data.loopFilePath);

    // ---- Recorder ----
    auto* rec = root->createNewChildElement ("Recorder");
    rec->setAttribute ("outputFolder", data.recordOutputFolder);
    rec->setAttribute ("recordInput",  data.recordInput  ? 1 : 0);
    rec->setAttribute ("recordOutput", data.recordOutput ? 1 : 0);

    // ---- Channels ----
    auto* channels = root->createNewChildElement ("Channels");
    for (int i = 0; i < 4; ++i)
        channels->addChildElement (channelToXml (data.channels[i], i));

    // ---- Input channel ----
    auto* inputChEl = channelToXml (data.inputChannelState, -1);
    inputChEl->setTagName ("InputChannel");
    inputChEl->setAttribute ("directMix", data.inputDirectMix);
    root->addChildElement (inputChEl);

    // ---- Input trim ----
    root->setAttribute ("inputTrimDb", data.inputTrimDb);

    // ---- FX Bus ----
    root->addChildElement (fxBusStateToXml (data.fxBusState));

    return root->writeTo (file);
}

//==============================================================================
bool ProjectState::loadFromFile (const juce::File& file, ProjectData& data)
{
    if (! file.existsAsFile())
        return false;

    auto root = juce::XmlDocument::parse (file);
    if (root == nullptr || root->getTagName() != "UpStageProject")
        return false;

    data.projectName   = root->getStringAttribute ("name", "Untitled");
    data.activeChannel = root->getIntAttribute    ("activeChannel", 0);

    // ---- Audio ----
    if (auto* audio = root->getChildByName ("AudioSettings"))
    {
        data.asioDeviceName  = audio->getStringAttribute ("asioDevice");
        data.sampleRate      = audio->getDoubleAttribute ("sampleRate", 44100.0);
        data.bufferSize      = audio->getIntAttribute    ("bufferSize", 256);
        data.inputChannel    = audio->getIntAttribute    ("inputChannel", 0);
        data.outputChannelL  = audio->getIntAttribute    ("outputChannelL", 0);
        data.outputChannelR  = audio->getIntAttribute    ("outputChannelR", 1);
    }

    // ---- MIDI ----
    if (auto* midi = root->getChildByName ("MidiSettings"))
    {
        data.midiInputDevice  = midi->getStringAttribute ("inputDevice");
        data.midiOutputDevice = midi->getStringAttribute ("outputDevice");

        if (auto* rules = midi->getChildByName ("Rules"))
        {
            data.midiRules.clear();
            for (auto* ruleEl : rules->getChildIterator())
                data.midiRules.add (xmlToRule (ruleEl));
        }
    }

    // ---- Input Router ----
    if (auto* router = root->getChildByName ("InputRouter"))
    {
        data.useLoopFile  = router->getIntAttribute ("useLoopFile", 0) != 0;
        data.loopFilePath = router->getStringAttribute ("loopFilePath");
    }

    // ---- Recorder ----
    if (auto* rec = root->getChildByName ("Recorder"))
    {
        data.recordOutputFolder = rec->getStringAttribute ("outputFolder");
        data.recordInput        = rec->getIntAttribute ("recordInput",  0) != 0;
        data.recordOutput       = rec->getIntAttribute ("recordOutput", 1) != 0;
    }

    // ---- Channels ----
    if (auto* channels = root->getChildByName ("Channels"))
    {
        int idx = 0;
        for (auto* chEl : channels->getChildIterator())
        {
            if (idx >= 4) break;
            xmlToChannel (chEl, data.channels[idx]);
            ++idx;
        }
    }

    // ---- Input channel ----
    if (auto* inputChEl = root->getChildByName ("InputChannel"))
    {
        xmlToChannel (inputChEl, data.inputChannelState);
        data.inputDirectMix = (float) inputChEl->getDoubleAttribute ("directMix", 0.0);
    }

    // ---- Input trim ----
    data.inputTrimDb = (float) root->getDoubleAttribute ("inputTrimDb", 0.0);

    // ---- FX Bus ----
    if (auto* fxEl = root->getChildByName ("FxBus"))
        xmlToFxBusState (fxEl, data.fxBusState);

    return true;
}

//==============================================================================
// saveWithDialog / loadWithDialog removed — file dialogs are now async in JUCE 7+.
// Open/save is handled directly in MainComponent::openProject() / saveProjectAs().

//==============================================================================
// Private helpers
//==============================================================================

juce::XmlElement* ProjectState::channelToXml (const ChannelState& ch, int index)
{
    auto* el = new juce::XmlElement ("Channel");
    el->setAttribute ("index",       index);
    el->setAttribute ("name",        ch.name.isEmpty() ? ("Channel " + juce::String (index + 1)) : ch.name);
    el->setAttribute ("inputGain",   ch.inputGain);
    el->setAttribute ("outputGain",  ch.outputGain);
    el->setAttribute ("pan",         ch.pan);

    for (const auto& slot : ch.plugins)
    {
        auto* plugEl = el->createNewChildElement ("Plugin");
        plugEl->setAttribute ("identifier", slot.pluginIdentifier);
        plugEl->setAttribute ("name",       slot.pluginName);
        plugEl->setAttribute ("bypassed",   slot.isBypassed ? 1 : 0);

        // Encode plugin state as base64
        if (slot.stateData.getSize() > 0)
        {
            juce::String b64 = juce::Base64::toBase64 (slot.stateData.getData(),
                                                       slot.stateData.getSize());
            plugEl->setAttribute ("state", b64);
        }
    }
    return el;
}

bool ProjectState::xmlToChannel (const juce::XmlElement* el, ChannelState& ch)
{
    if (el == nullptr) return false;
    ch.name        = el->getStringAttribute ("name");
    ch.inputGain   = (float) el->getDoubleAttribute ("inputGain",  1.0);
    ch.outputGain  = (float) el->getDoubleAttribute ("outputGain", 1.0);
    ch.pan         = (float) el->getDoubleAttribute ("pan", 0.0);
    ch.plugins.clear();

    for (auto* plugEl : el->getChildIterator())
    {
        PluginSlotState slot;
        slot.pluginIdentifier = plugEl->getStringAttribute ("identifier");
        slot.pluginName       = plugEl->getStringAttribute ("name");
        slot.isBypassed       = plugEl->getIntAttribute ("bypassed", 0) != 0;

        juce::String b64 = plugEl->getStringAttribute ("state");
        if (b64.isNotEmpty())
        {
            juce::MemoryOutputStream mos;
            juce::Base64::convertFromBase64 (mos, b64);
            slot.stateData = mos.getMemoryBlock();
        }
        ch.plugins.add (slot);
    }
    return true;
}

juce::XmlElement* ProjectState::ruleToXml (const MidiRule& rule)
{
    auto* el = new juce::XmlElement ("Rule");
    el->setAttribute ("inType",     inputTypeToString (rule.inType));
    el->setAttribute ("inChannel",  rule.inChannel);
    el->setAttribute ("inNumber",   rule.inNumber);
    el->setAttribute ("inValue",    rule.inValue);
    el->setAttribute ("outType",    outputTypeToString (rule.outType));
    el->setAttribute ("outChannel", rule.outChannel);
    el->setAttribute ("outNumber",  rule.outNumber);
    el->setAttribute ("outValue",   rule.outValue);
    el->setAttribute ("desc",       rule.description);
    return el;
}

MidiRule ProjectState::xmlToRule (const juce::XmlElement* el)
{
    MidiRule rule;
    rule.inType     = stringToInputType  (el->getStringAttribute ("inType",  "CC"));
    rule.inChannel  = el->getIntAttribute ("inChannel",  0);
    rule.inNumber   = el->getIntAttribute ("inNumber",   0);
    rule.inValue    = el->getIntAttribute ("inValue",   -1);
    rule.outType    = stringToOutputType (el->getStringAttribute ("outType", "NoteOn"));
    rule.outChannel = el->getIntAttribute ("outChannel", 1);
    rule.outNumber  = el->getIntAttribute ("outNumber",  60);
    rule.outValue   = el->getIntAttribute ("outValue",  127);
    rule.description = el->getStringAttribute ("desc");
    return rule;
}

juce::String ProjectState::inputTypeToString (MidiRule::InputType t)
{
    switch (t)
    {
        case MidiRule::InputType::CC:      return "CC";
        case MidiRule::InputType::PC:      return "PC";
        case MidiRule::InputType::NoteOn:  return "NoteOn";
        case MidiRule::InputType::NoteOff: return "NoteOff";
        default:                           return "CC";
    }
}

juce::String ProjectState::outputTypeToString (MidiRule::OutputType t)
{
    switch (t)
    {
        case MidiRule::OutputType::CC:      return "CC";
        case MidiRule::OutputType::PC:      return "PC";
        case MidiRule::OutputType::NoteOn:  return "NoteOn";
        case MidiRule::OutputType::NoteOff: return "NoteOff";
        default:                            return "NoteOn";
    }
}

MidiRule::InputType ProjectState::stringToInputType (const juce::String& s)
{
    if (s == "PC")      return MidiRule::InputType::PC;
    if (s == "NoteOn")  return MidiRule::InputType::NoteOn;
    if (s == "NoteOff") return MidiRule::InputType::NoteOff;
    return MidiRule::InputType::CC;
}

MidiRule::OutputType ProjectState::stringToOutputType (const juce::String& s)
{
    if (s == "CC")      return MidiRule::OutputType::CC;
    if (s == "PC")      return MidiRule::OutputType::PC;
    if (s == "NoteOff") return MidiRule::OutputType::NoteOff;
    return MidiRule::OutputType::NoteOn;
}

//==============================================================================
// FX Bus serialisation helpers
//==============================================================================

juce::XmlElement* ProjectState::fxBusStateToXml (const ProjectData::FxBusState& s)
{
    auto* el = new juce::XmlElement ("FxBus");
    el->setAttribute ("bypassed", s.bypassed ? 1 : 0);

    for (const auto& slot : s.plugins)
    {
        auto* plugEl = el->createNewChildElement ("Plugin");
        plugEl->setAttribute ("identifier", slot.pluginIdentifier);
        plugEl->setAttribute ("name",       slot.pluginName);
        plugEl->setAttribute ("bypassed",   slot.isBypassed ? 1 : 0);
        if (slot.stateData.getSize() > 0)
        {
            juce::String b64 = juce::Base64::toBase64 (slot.stateData.getData(),
                                                       slot.stateData.getSize());
            plugEl->setAttribute ("state", b64);
        }
    }
    return el;
}

bool ProjectState::xmlToFxBusState (const juce::XmlElement* el,
                                     ProjectData::FxBusState& s)
{
    if (el == nullptr) return false;
    s.bypassed = el->getIntAttribute ("bypassed", 0) != 0;

    s.plugins.clear();
    for (auto* plugEl : el->getChildIterator())
    {
        PluginSlotState slot;
        slot.pluginIdentifier = plugEl->getStringAttribute ("identifier");
        slot.pluginName       = plugEl->getStringAttribute ("name");
        slot.isBypassed       = plugEl->getIntAttribute    ("bypassed", 0) != 0;

        juce::String b64 = plugEl->getStringAttribute ("state");
        if (b64.isNotEmpty())
        {
            juce::MemoryOutputStream mos;
            juce::Base64::convertFromBase64 (mos, b64);
            slot.stateData = mos.getMemoryBlock();
        }
        s.plugins.add (slot);
    }
    return true;
}
