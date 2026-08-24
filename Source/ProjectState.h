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

    // Which rack slot this plugin sat in. Slots are fixed positions and a chain
    // can have gaps, so position in this array is NOT the slot number.
    // -1 means "not recorded" - a project saved before fixed slots, which loads
    // packed from the top exactly as it used to.
    int          slotIndex = -1;
    juce::MemoryBlock stateData;     // raw bytes from getStateInformation()
    bool         isBypassed = false;
    // Per-slot appearance. Lives on the slot (not keyed by plugin name) so two
    // instances of the same plugin keep separate nicknames/tints.
    juce::Colour tint { 0x00000000 };
    juce::String nickname;
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
    enum class Mode       { Normal, Toggle, Expression };
    enum class Curve      { Linear, Log, Exp };

    InputType   inType    = InputType::CC;
    int         inChannel = 0;    // 0 = any
    int         inNumber  = 0;    // CC number / PC number / note number
    int         inValue   = -1;   // -1 = any value

    OutputType  outType    = OutputType::NoteOn;
    int         outChannel = 1;
    int         outNumber  = 60;
    int         outValue   = 127; // -1 = passthrough input value

    Mode        mode       = Mode::Normal;

    // Expression mode settings
    int         exprMin    = 0;
    int         exprMax    = 127;
    Curve       exprCurve  = Curve::Linear;

    juce::String description;
};

struct ProjectData
{
    juce::String              projectName    = "Untitled";
    int                       activeChannel  = 0;
    bool                      parallelRouting = true;

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
        juce::Array<PluginAppearanceState> pluginAppearances;
    };
    FxBusState fxBusState;

    // Knob color customizations (component ID → color name)
    std::map<juce::String, juce::String> knobColorMap;

    // Scenes and MIDI learn are saved/loaded via SceneManager/MidiLearnManager
    // and stored in the same XML file as child elements.
};

//==============================================================================
class SceneManager;
class MidiLearnManager;

class ProjectState
{
public:
    ProjectState();

    /** Save project to a .upstage XML file. Returns true on success.
        Optionally appends scene and MIDI learn data to the same XML. */
    bool saveToFile (const juce::File& file, const ProjectData& data,
                     SceneManager* scenes = nullptr,
                     MidiLearnManager* midiLearn = nullptr);

    /** Load project from a .upstage XML file. Returns true on success.
        Optionally restores scene and MIDI learn data from the same XML. */
    bool loadFromFile (const juce::File& file, ProjectData& data,
                       SceneManager* scenes = nullptr,
                       MidiLearnManager* midiLearn = nullptr);

    // File dialogs moved to MainComponent (async API in JUCE 7+)

    static juce::XmlElement* ruleToXml (const MidiRule& rule);
    static MidiRule          xmlToRule (const juce::XmlElement* el);

private:
    juce::XmlElement* channelToXml   (const ChannelState& ch, int index);
    bool              xmlToChannel   (const juce::XmlElement* el, ChannelState& ch);

    juce::XmlElement* fxBusStateToXml (const ProjectData::FxBusState& state);
    bool              xmlToFxBusState  (const juce::XmlElement* el,
                                        ProjectData::FxBusState& state);

    static juce::String inputTypeToString  (MidiRule::InputType  t);
    static juce::String outputTypeToString (MidiRule::OutputType t);
    static MidiRule::InputType  stringToInputType  (const juce::String& s);
    static MidiRule::OutputType stringToOutputType (const juce::String& s);

    static juce::String modeToString   (MidiRule::Mode  m);
    static MidiRule::Mode stringToMode (const juce::String& s);
    static juce::String curveToString   (MidiRule::Curve c);
    static MidiRule::Curve stringToCurve (const juce::String& s);
};
