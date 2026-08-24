#include "ProjectState.h"
#include "SceneManager.h"
#include "MidiLearnManager.h"

ProjectState::ProjectState() {}

//==============================================================================
bool ProjectState::saveToFile (const juce::File& file, const ProjectData& data,
                               SceneManager* scenes, MidiLearnManager* midiLearn)
{
    auto root = std::make_unique<juce::XmlElement> ("UpStageProject");
    root->setAttribute ("name",          data.projectName);
    root->setAttribute ("activeChannel",  data.activeChannel);
    root->setAttribute ("parallelRouting", data.parallelRouting);
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

    // ---- Tap tempo ----
    root->setAttribute ("tapTempoBPM",          data.tapTempoBPM);
    root->setAttribute ("tapTempoClockEnabled", data.tapTempoClockEnabled ? 1 : 0);

    // ---- Gate ----
    auto* gateEl = root->createNewChildElement ("Gate");
    gateEl->setAttribute ("enabled",   data.gateEnabled ? 1 : 0);
    gateEl->setAttribute ("threshDb",  data.gateThreshDb);
    gateEl->setAttribute ("attackMs",  data.gateAttackMs);
    gateEl->setAttribute ("holdMs",    data.gateHoldMs);
    gateEl->setAttribute ("releaseMs", data.gateReleaseMs);

    // ---- FX Bus ----
    root->addChildElement (fxBusStateToXml (data.fxBusState));

    {
        auto* masterEl = root->createNewChildElement ("Master");
        masterEl->setAttribute ("faderDb",   data.masterFaderDb);
        masterEl->setAttribute ("tunerSlot", data.tunerSlot);
    }

    // ---- Setlist ----
    if (data.setlistFilePath.isNotEmpty())
        root->setAttribute ("setlistFilePath", data.setlistFilePath);

    // ---- Knob Colors ----
    if (! data.knobColorMap.empty())
    {
        auto* colorsEl = root->createNewChildElement ("KnobColors");
        for (const auto& [id, color] : data.knobColorMap)
        {
            auto* e = colorsEl->createNewChildElement ("Knob");
            e->setAttribute ("id", id);
            e->setAttribute ("color", color);
        }
    }

    // Scenes and MIDI learn
    if (scenes != nullptr)
        scenes->saveToXml (*root);
    if (midiLearn != nullptr)
        midiLearn->saveToXml (*root);

    return root->writeTo (file);
}

//==============================================================================
bool ProjectState::loadFromFile (const juce::File& file, ProjectData& data,
                                 SceneManager* scenes, MidiLearnManager* midiLearn)
{
    if (! file.existsAsFile())
        return false;

    auto root = juce::XmlDocument::parse (file);
    if (root == nullptr || root->getTagName() != "UpStageProject")
        return false;

    data.projectName     = root->getStringAttribute ("name", "Untitled");
    data.activeChannel   = root->getIntAttribute    ("activeChannel", 0);
    data.parallelRouting = root->getBoolAttribute   ("parallelRouting", true);

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

    // ---- Tap tempo ----
    data.tapTempoBPM          = root->getDoubleAttribute ("tapTempoBPM", 120.0);
    data.tapTempoClockEnabled = root->getIntAttribute ("tapTempoClockEnabled", 0) != 0;

    // ---- Gate ----
    if (auto* gateEl = root->getChildByName ("Gate"))
    {
        data.gateEnabled   = gateEl->getIntAttribute ("enabled", 0) != 0;
        data.gateThreshDb  = (float) gateEl->getDoubleAttribute ("threshDb", -60.0);
        data.gateAttackMs  = (float) gateEl->getDoubleAttribute ("attackMs", 5.0);
        data.gateHoldMs    = (float) gateEl->getDoubleAttribute ("holdMs", 50.0);
        data.gateReleaseMs = (float) gateEl->getDoubleAttribute ("releaseMs", 100.0);
    }

    // ---- FX Bus ----
    if (auto* masterEl = root->getChildByName ("Master"))
    {
        data.masterFaderDb = (float) masterEl->getDoubleAttribute ("faderDb", 0.0);
        data.tunerSlot     = masterEl->getStringAttribute ("tunerSlot");
    }

    if (auto* fxEl = root->getChildByName ("FxBus"))
        xmlToFxBusState (fxEl, data.fxBusState);

    // ---- Setlist ----
    data.setlistFilePath = root->getStringAttribute ("setlistFilePath");

    // ---- Knob Colors ----
    data.knobColorMap.clear();
    if (auto* colorsEl = root->getChildByName ("KnobColors"))
    {
        for (auto* e : colorsEl->getChildWithTagNameIterator ("Knob"))
        {
            auto id    = e->getStringAttribute ("id");
            auto color = e->getStringAttribute ("color");
            if (id.isNotEmpty() && color.isNotEmpty())
                data.knobColorMap[id] = color;
        }
    }

    // Scenes and MIDI learn
    if (scenes != nullptr)
        scenes->loadFromXml (*root);
    if (midiLearn != nullptr)
        midiLearn->loadFromXml (*root);

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
        plugEl->setAttribute ("slot",       slot.slotIndex);
        plugEl->setAttribute ("name",       slot.pluginName);
        plugEl->setAttribute ("bypassed",   slot.isBypassed ? 1 : 0);
        plugEl->setAttribute ("tint",       (int) slot.tint.getARGB());
        plugEl->setAttribute ("nickname",   slot.nickname);

        // Encode plugin state as base64
        if (slot.stateData.getSize() > 0)
        {
            juce::String b64 = juce::Base64::toBase64 (slot.stateData.getData(),
                                                       slot.stateData.getSize());
            plugEl->setAttribute ("state", b64);
        }
    }

    for (const auto& pas : ch.pluginAppearances)
    {
        auto* appEl = el->createNewChildElement ("Appearance");
        appEl->setAttribute ("pluginName", pas.pluginName);
        appEl->setAttribute ("tint",       (int) pas.tint.getARGB());
        appEl->setAttribute ("nickname",   pas.nickname);
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

    // Iterate only <Plugin> children — channelToXml also writes <Appearance>
    // children, and treating those as plugins created ghost empty slots.
    for (auto* plugEl : el->getChildWithTagNameIterator ("Plugin"))
    {
        PluginSlotState slot;
        slot.pluginIdentifier = plugEl->getStringAttribute ("identifier");
        // -1 when absent: a project saved before fixed slots, which loads
        // packed from the top exactly as it always did.
        slot.slotIndex        = plugEl->getIntAttribute ("slot", -1);
        slot.pluginName       = plugEl->getStringAttribute ("name");
        slot.isBypassed       = plugEl->getIntAttribute ("bypassed", 0) != 0;
        slot.tint             = juce::Colour ((juce::uint32) plugEl->getIntAttribute ("tint", 0));
        slot.nickname         = plugEl->getStringAttribute ("nickname");

        juce::String b64 = plugEl->getStringAttribute ("state");
        if (b64.isNotEmpty())
        {
            juce::MemoryOutputStream mos;
            juce::Base64::convertFromBase64 (mos, b64);
            slot.stateData = mos.getMemoryBlock();
        }
        ch.plugins.add (slot);
    }

    ch.pluginAppearances.clear();
    for (auto* appEl : el->getChildWithTagNameIterator ("Appearance"))
    {
        PluginAppearanceState pas;
        pas.pluginName = appEl->getStringAttribute ("pluginName");
        pas.tint       = juce::Colour ((juce::uint32) appEl->getIntAttribute ("tint", 0));
        pas.nickname   = appEl->getStringAttribute ("nickname");
        ch.pluginAppearances.add (pas);
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
    el->setAttribute ("mode",       modeToString (rule.mode));
    el->setAttribute ("exprMin",    rule.exprMin);
    el->setAttribute ("exprMax",    rule.exprMax);
    el->setAttribute ("exprCurve",  curveToString (rule.exprCurve));
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
    rule.mode       = stringToMode (el->getStringAttribute ("mode", "Normal"));
    rule.exprMin    = el->getIntAttribute ("exprMin", 0);
    rule.exprMax    = el->getIntAttribute ("exprMax", 127);
    rule.exprCurve  = stringToCurve (el->getStringAttribute ("exprCurve", "Linear"));
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

juce::String ProjectState::modeToString (MidiRule::Mode m)
{
    switch (m)
    {
        case MidiRule::Mode::Toggle:     return "Toggle";
        case MidiRule::Mode::Expression: return "Expression";
        default:                         return "Normal";
    }
}

MidiRule::Mode ProjectState::stringToMode (const juce::String& s)
{
    if (s == "Toggle")     return MidiRule::Mode::Toggle;
    if (s == "Expression") return MidiRule::Mode::Expression;
    return MidiRule::Mode::Normal;
}

juce::String ProjectState::curveToString (MidiRule::Curve c)
{
    switch (c)
    {
        case MidiRule::Curve::Log: return "Log";
        case MidiRule::Curve::Exp: return "Exp";
        default:                   return "Linear";
    }
}

MidiRule::Curve ProjectState::stringToCurve (const juce::String& s)
{
    if (s == "Log") return MidiRule::Curve::Log;
    if (s == "Exp") return MidiRule::Curve::Exp;
    return MidiRule::Curve::Linear;
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
        plugEl->setAttribute ("slot",       slot.slotIndex);
        plugEl->setAttribute ("name",       slot.pluginName);
        plugEl->setAttribute ("bypassed",   slot.isBypassed ? 1 : 0);
        plugEl->setAttribute ("tint",       (int) slot.tint.getARGB());
        plugEl->setAttribute ("nickname",   slot.nickname);
        if (slot.stateData.getSize() > 0)
        {
            juce::String b64 = juce::Base64::toBase64 (slot.stateData.getData(),
                                                       slot.stateData.getSize());
            plugEl->setAttribute ("state", b64);
        }
    }

    for (const auto& pas : s.pluginAppearances)
    {
        auto* appEl = el->createNewChildElement ("Appearance");
        appEl->setAttribute ("pluginName", pas.pluginName);
        appEl->setAttribute ("nickname",   pas.nickname);
    }
    return el;
}

bool ProjectState::xmlToFxBusState (const juce::XmlElement* el,
                                     ProjectData::FxBusState& s)
{
    if (el == nullptr) return false;
    s.bypassed = el->getIntAttribute ("bypassed", 0) != 0;

    s.plugins.clear();
    // Iterate only <Plugin> children — <Appearance> children also live here.
    for (auto* plugEl : el->getChildWithTagNameIterator ("Plugin"))
    {
        PluginSlotState slot;
        slot.pluginIdentifier = plugEl->getStringAttribute ("identifier");
        // -1 when absent: a project saved before fixed slots, which loads
        // packed from the top exactly as it always did.
        slot.slotIndex        = plugEl->getIntAttribute ("slot", -1);
        slot.pluginName       = plugEl->getStringAttribute ("name");
        slot.isBypassed       = plugEl->getIntAttribute    ("bypassed", 0) != 0;
        slot.tint             = juce::Colour ((juce::uint32) plugEl->getIntAttribute ("tint", 0));
        slot.nickname         = plugEl->getStringAttribute ("nickname");

        juce::String b64 = plugEl->getStringAttribute ("state");
        if (b64.isNotEmpty())
        {
            juce::MemoryOutputStream mos;
            juce::Base64::convertFromBase64 (mos, b64);
            slot.stateData = mos.getMemoryBlock();
        }
        s.plugins.add (slot);
    }

    s.pluginAppearances.clear();
    for (auto* appEl : el->getChildWithTagNameIterator ("Appearance"))
    {
        PluginAppearanceState pas;
        pas.pluginName = appEl->getStringAttribute ("pluginName");
        pas.nickname   = appEl->getStringAttribute ("nickname");
        s.pluginAppearances.add (pas);
    }
    return true;
}
