#pragma once
#include <JuceHeader.h>

/**
 * ProjectState
 *
 * Serialises and deserialises the entire UpStage session to/from an XML file
 * (.upstage). Saved data includes:
 *   - Active channel index
 *   - Per-channel VST3 plugin chain (plugin ID + full state blob)
 *   - MIDI translator rules
 *   - Audio device settings (ASIO device name, sample rate, buffer size)
 *   - Recorder settings (output folder, capture mode)
 *   - Input router mode (live vs. loop) + last loop file path
 */

struct PluginSlotState
{
    juce::String pluginIdentifier;   // VST3 plugin identifier string
    juce::String pluginName;
    juce::MemoryBlock stateData;     // raw bytes from getStateInformation()
    bool         isBypassed = false;
};

struct PluginAppearanceState
{
    juce::String pluginName;
    juce::Colour tint { 0x00000000 };
    juce::String nickname;
};

struct ChannelState
{
    juce::String              name;
    juce::Array<PluginSlotState> plugins;
    float                     inputGain  = 1.0f;
    float                     outputGain = 1.0f;
    float                     pan        = 0.0f;
    juce::Array<PluginAppearanceState> pluginAppearances;
};

struct MidiRule
{
    enum class InputType  { CC, PC, NoteOn, NoteOff };
    enum class OutputType { CC, PC, NoteOn, NoteOff };

    InputType   inType    = InputType::CC;
    int         inChannel = 0;    // 0 = any
    int         inNumber  = 0;    // CC number / PC number / note number
    int         inValue   = -1;   // -1 = any value

    OutputType  outType    = OutputType::NoteOn;
    int         outChannel = 1;
    int         outNumber  = 60;
    int         outValue   = 127; // -1 = passthrough input value

    juce::String description;
};

struct ProjectData
{
    juce::String              projectName    = "Untitled";
    int                       activeChannel  = 0;

    // 4 channels
    ChannelState              channels[4];

    // MIDI
    juce::Array<MidiRule>     midiRules;
    juce::String              midiInputDevice;
    juce::String              midiOutputDevice;

    // Audio
    juce::String              asioDeviceName;
    double                    sampleRate   = 44100.0;
    int                       bufferSize   = 256;
    int                       inputChannel = 0;   // which ASIO input channel (guitar)
    int                       outputChannelL = 0;
    int                       outputChannelR = 1;

    // Input Router
    bool                      useLoopFile  = false;
    juce::String              loopFilePath;

    // Recorder
    juce::String              recordOutputFolder;
    bool                      recordInput  = false;
    bool                      recordOutput = true;

    // Tap tempo
    double                    tapTempoBPM  = 120.0;
    bool                      tapTempoClockEnabled = false;

    // Gate
    bool                      gateEnabled  = false;
    float                     gateThreshDb = -60.0f;
    float                     gateAttackMs = 5.0f;
    float                     gateHoldMs   = 50.0f;
    float                     gateReleaseMs = 100.0f;

    // Setlist
    juce::String              setlistFilePath;

    // Input Trim
    // A gain trim (in decibels) applied to the input signal immediately before
    // the noise gate in the processing chain.  Range: −24 dB … +24 dB.
    // Default 0.0 dB = unity gain (no change).
    float                     inputTrimDb  = 0.0f;

    // Input channel (pre-FX) state
    ChannelState              inputChannelState;
    float                     inputDirectMix = 0.0f;

    // Master insert chain state
    struct FxBusState
    {
        bool  bypassed = false;
        juce::Array<PluginSlotState> plugins;
    };
    FxBusState fxBusState;

    // Knob color customizations (component ID → color name)
    std::map<juce::String, juce::String> knobColorMap;

    // Scenes and MIDI learn are saved/loaded via SceneManager/MidiLearnManager
    // and stored in the same XML file as child elements.
};

//==============================================================================
class ProjectState
{
public:
    ProjectState();

    /** Save project to a .upstage XML file. Returns true on success. */
    bool saveToFile (const juce::File& file, const ProjectData& data);

    /** Load project from a .upstage XML file. Returns true on success. */
    bool loadFromFile (const juce::File& file, ProjectData& data);

    // File dialogs moved to MainComponent (async API in JUCE 7+)

private:
    juce::XmlElement* channelToXml   (const ChannelState& ch, int index);
    bool              xmlToChannel   (const juce::XmlElement* el, ChannelState& ch);

    juce::XmlElement* ruleToXml (const MidiRule& rule);
    MidiRule          xmlToRule (const juce::XmlElement* el);

    juce::XmlElement* fxBusStateToXml (const ProjectData::FxBusState& state);
    bool              xmlToFxBusState  (const juce::XmlElement* el,
                                        ProjectData::FxBusState& state);

    static juce::String inputTypeToString  (MidiRule::InputType  t);
    static juce::String outputTypeToString (MidiRule::OutputType t);
    static MidiRule::InputType  stringToInputType  (const juce::String& s);
    static MidiRule::OutputType stringToOutputType (const juce::String& s);
};
