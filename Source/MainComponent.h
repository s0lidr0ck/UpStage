#pragma once
#include <JuceHeader.h>
#include "AppConfig.h"
#include "ChannelStrip.h"
#include "ChannelStripPanel.h"
#include "MidiTranslator.h"
#include "InputRouter.h"
#include "Recorder.h"
#include "ProjectState.h"
#include "SetlistManager.h"
#include "SetlistPanel.h"
#include "SceneManager.h"
#include "TapTempo.h"
#include "TunerPanel.h"
#include "LevelMeter.h"
#include "NoiseGate.h"
#include "MidiLearnManager.h"
#include "UndoActions.h"
#include "FxBus.h"
#include "FxBusPanel.h"
#include "PluginManagerWindow.h"
#include "PluginBrowserWindow.h"
#include "Metronome.h"
#include "Looper.h"
#include "VuMeter.h"
#include "MidiMonitorWindow.h"
#include "MidiRulesPanel.h"

/**
 * MainComponent  v0.4 - Mixer-Style Layout
 *
 * Audio/MIDI host wiring all UpStage systems together.
 *
 * New in v0.3:
 *   - Plugin editor windows  (ChannelStripPanel opens VST3 GUIs)
 *   - Plugin bypass buttons  (per-slot, wired through ChannelStripPanel)
 *   - Input gain trim        (pre-gate, MIDI-learnable, saved per project)
 *   - Soft CC takeover       (MidiLearnManager scenes don't jump knobs)
 *   - A/B channel hotkey     (set A/B bookmarks, toggle or MIDI PC 124/125)
 *   - Send/return FX bus     (FxBus + FxBusPanel, shared reverb/delay)
 *
 * MIDI PC routing:
 *   PC 0–3   → switch active channel
 *   PC 4–11  → load scene 0–7
 *   PC 124   → recall A channel
 *   PC 125   → recall B channel
 *   PC 126   → setlist previous song
 *   PC 127   → setlist next song
 */
class MainComponent : public juce::AudioAppComponent,
                      public juce::AudioPlayHead,
                      public juce::MidiInputCallback,
                      public juce::Timer,
                      public juce::Button::Listener,
                      public juce::MenuBarModel,
                      public MidiLearnManager::Listener,
                      public juce::DragAndDropContainer,
                      public juce::DragAndDropTarget
{
public:
    MainComponent();
    ~MainComponent() override;

    //==========================================================================
    // AudioAppComponent
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // MidiInputCallback
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    // AudioPlayHead
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;

    // Component
    void paint  (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

    // DragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    // Button::Listener
    void buttonClicked (juce::Button* b) override;

    // Timer
    void timerCallback() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int menuIndex, const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // MidiLearnManager::Listener
    void midiLearnParameterChanged (const juce::String& paramID, float value) override;

    //==========================================================================
    // Project management
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void loadProjectData (const ProjectData& data);
    ProjectData collectProjectData() const;

    // Channel switching
    void setActiveChannel (int index);
    int  getActiveChannel() const { return activeChannel; }

    // UI customization
    const std::map<juce::String, juce::String>* getKnobColorMap() const { return &knobColorMap; }

    // Panic
    void sendMidiPanic();

private:
    //==========================================================================
    // State
    int    activeChannel     = 0;
    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 256;
    bool   projectDirty      = false;
    bool   outputMuted       = false;  // true while tuner is active
    int    autosaveCounter   = 0;
    std::atomic<float> masterOutputGain { 1.0f };
    std::atomic<bool>  midiActivityFlag { false };
    int                midiFlashCounter = 0;

    // Undo/redo
    juce::UndoManager undoManager { 30000, 100 };
    double faderDragStartValue[NUM_CHANNELS] = {};

    // Solo/Mute state for parallel processing
    bool   channelMuted[NUM_CHANNELS] = { false, false, false, false };
    bool   channelSoloed[NUM_CHANNELS] = { false, false, false, false };

    // Routing mode: parallel (all channels) vs single (active only)
    bool   parallelRouting = true;

    // Per-channel crossfade gain for smooth switching in single mode
    juce::SmoothedValue<float> channelFadeGain[NUM_CHANNELS];

    // Numpad 9 prefix: press Numpad 9, then Numpad 1-4 to switch channel
    bool   numpad9Prefix = false;

    // UI customization
    bool   uiEditMode = false;
    std::map<juce::String, juce::String> knobColorMap; // knob name -> color

    //==========================================================================
    // A/B channel compare
    int  abChannelA   = 0;
    int  abChannelB   = 1;
    bool abIsShowingA = true;

    /** MIDI PC that instantly recalls the A channel. */
    static constexpr int AB_PC_A = 124;
    /** MIDI PC that instantly recalls the B channel. */
    static constexpr int AB_PC_B = 125;

    //==========================================================================
    // Core objects
    juce::AudioPluginFormatManager    pluginFormatManager;
    juce::KnownPluginList             knownPluginList;

    std::unique_ptr<ChannelStrip>     inputChannel;  // Pre-FX chain before splitting to channels
    std::unique_ptr<ChannelStrip>     channels[NUM_CHANNELS];

    std::unique_ptr<FxBus>            fxBus;

    MidiTranslator     midiTranslator;
    InputRouter        inputRouter;
    Recorder           recorder;
    NoiseGate          noiseGate;
    SetlistManager     setlistManager;
    SceneManager       sceneManager;
    TapTempo           tapTempo;
    MidiLearnManager   midiLearnManager;
    Metronome          metronome;
    Looper             looper;

    //==========================================================================
    // Audio device types (for ASIO support)
    juce::OwnedArray<juce::AudioIODeviceType> deviceTypes;

    //==========================================================================
    // Plugin scan progress
    double scanProgress = 0.0;

    //==========================================================================
    // MIDI output (for MIDI clock)
    std::unique_ptr<juce::MidiOutput> midiOutput;
    juce::String activeMidiInputId;
    juce::String activeMidiOutputId;

    //==========================================================================
    // Project state
    ProjectData  currentProject;
    juce::File   currentProjectFile;
    ProjectState projectState;

    //==========================================================================
    // MIDI queue (audio thread → message thread)
    juce::MidiBuffer  pendingMidi;
    juce::CriticalSection midiLock;

    //==========================================================================
    // UI — Menu
    juce::MenuBarComponent menuBar;

    // Transport / input row
    juce::TextButton  liveInputButton  { "Guitar" };
    juce::TextButton  loopInputButton  { "Loop" };
    juce::TextButton  loadLoopButton   { "Load Loop..." };
    juce::TextButton  playLoopButton   { "Play" };
    juce::Label       loopFileLabel;
    juce::Slider      loopVolumeSlider;

    // Input trim
    juce::Slider      inputTrimSlider;
    juce::Label       inputTrimLabel;

    // Tap tempo
    juce::TextButton  tapButton        { "Tap" };
    juce::Label       bpmLabel;
    juce::ToggleButton clockToggle     { "Clock" };

    // Tuner
    juce::TextButton  tunerButton      { "Tuner" };

    // Panic
    juce::TextButton  panicButton      { "Panic" };

    // Record
    juce::TextButton  recordButton     { "Rec" };
    juce::TextButton  stopRecordButton { "Stop" };
    juce::ToggleButton recDryToggle    { "Dry" };
    juce::ToggleButton recWetToggle    { "Wet" };

    // ASIO
    juce::TextButton  asioButton       { "ASIO" };

    // Metronome
    juce::TextButton  metronomeButton  { "Metro" };
    int               metroFlashCounter = 0;

    // Looper
    juce::TextButton  loopRecButton    { "Loop" };
    juce::Label       looperProgressLabel;
    int               looperFlashCounter = 0;

    // Routing mode toggle
    juce::TextButton  routingModeButton { ">>" };

    // Toolbar label expand
    bool               toolbarLabelsVisible = false;
    juce::TextButton   toolbarExpandButton  { "..." };

    // Help
    juce::TextButton   helpButton  { "?" };

    // Setlist
    juce::TextButton  setlistButton    { "Setlist" };

    // Noise gate
    juce::ToggleButton gateToggle      { "Gate" };
    juce::Slider       gateThreshSlider;
    juce::Label        gateThreshLabel;

    // Scenes row (8 scene buttons)
    juce::TextButton   sceneButtons[NUM_SCENES];
    juce::TextButton   saveSceneButtons[NUM_SCENES];
    int                activeSceneIndex = -1;

    // Input channel strip (pre-FX)
    juce::Label                        inputChannelLabel;
    std::unique_ptr<ChannelStripPanel> inputChannelPanel;
    juce::TextButton                   addInputPluginButton;
    std::unique_ptr<LevelMeter>        inputChannelMeterIn;   // Before FX
    std::unique_ptr<LevelMeter>        inputChannelMeterOut;  // After FX
    juce::Slider                       inputDirectKnob;
    juce::Label                        inputDirectLabel;
    juce::Slider                       inputStripFader;
    juce::Label                        inputStripFaderLabel;
    std::atomic<float>                 inputDirectLevel { 0.0f };

    // Per-channel mixer strips
    std::unique_ptr<LevelMeter>        levelMeters[NUM_CHANNELS];     // output
    std::unique_ptr<LevelMeter>        inputLevelMeters[NUM_CHANNELS]; // input
    std::unique_ptr<ChannelStripPanel> channelStripPanels[NUM_CHANNELS];
    juce::Slider                       outputFaders[NUM_CHANNELS];
    juce::Slider                       outputGainKnobs[NUM_CHANNELS];
    juce::Slider                       inputTrimKnobs[NUM_CHANNELS];
    juce::Label                        channelLabels[NUM_CHANNELS];
    juce::Label                        faderLevelLabels[NUM_CHANNELS];
    juce::Label                        outputGainLabels[NUM_CHANNELS];
    juce::Label                        inputTrimLabels[NUM_CHANNELS];
    juce::TextButton                   addPluginButtons[NUM_CHANNELS];
    juce::TextButton                   soloButtons[NUM_CHANNELS];
    juce::TextButton                   muteButtons[NUM_CHANNELS];
    juce::TextButton                   activeIndicators[NUM_CHANNELS];
    juce::Label                        inMeterLabels[NUM_CHANNELS];
    juce::Label                        outMeterLabels[NUM_CHANNELS];

    // FX Bus panel
    std::unique_ptr<FxBusPanel>       fxBusPanel;

    // Tuner panel (hidden until activated)
    TunerPanel tunerPanel;

    // MIDI activity LED + monitor
    juce::Label midiLedLabel;
    std::unique_ptr<MidiMonitorWindow> midiMonitorWindow;
    MidiRulesPanel* activeMidiRulesPanel = nullptr;

    // Status bar
    juce::Label statusLabel;
    juce::Label statusStateLabel;  // colored state flags
    juce::Label cpuLabel;
    juce::Label ramLabel;

    //==========================================================================
    // Helpers
    void scanForPlugins (bool clearCache = false);
    void showAddPluginMenu (int channelIndex);
    void showAddPluginMenuForFxBus();
    void showKnobColorMenu (juce::Component* knob);
    void openAsioSettings();
    void showSetlistPanel();

    void updateSceneButtonStates();
    void updateTransportUI();
    void updateStatusBar();
    void updateFaderLabel (int channelIndex);
    void updateActiveIndicators();

    // Level meter data written by audio thread
    std::atomic<float> channelLevelL[NUM_CHANNELS] {};
    std::atomic<float> channelLevelR[NUM_CHANNELS] {};
    std::atomic<float> channelInputLevelL[NUM_CHANNELS] {};
    std::atomic<float> channelInputLevelR[NUM_CHANNELS] {};
    std::atomic<float> inputLevelInL {}, inputLevelInR {};   // Input before FX
    std::atomic<float> inputLevelOutL {}, inputLevelOutR {}; // Input after FX
    std::atomic<float> masterLevelInL {}, masterLevelInR {};   // Master before FX bus
    std::atomic<float> masterLevelOutL {}, masterLevelOutR {}; // Master after FX bus
    std::atomic<float> masterStereoL {}, masterStereoR {};     // For stereo spread
    std::atomic<float> masterLufsDb { -60.0f };                // Short-term LUFS
    float lufsAccumulator = 0.0f;
    int   lufsSampleCount = 0;

    // K-weighting biquad filters for LUFS
    struct Biquad {
        float b0=1,b1=0,b2=0,a1=0,a2=0, z1=0,z2=0;
        float process (float x) {
            float y = b0*x + b1*z1 + b2*z2 - a1*z1 - a2*z2;
            // direct form II transposed
            float w = x - a1*z1 - a2*z2;
            y = b0*w + b1*z1 + b2*z2;
            z2 = z1; z1 = w;
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };
    Biquad kShelfL, kShelfR, kHpL, kHpR;

    enum MenuIDs
    {
        MenuNew = 1, MenuOpen, MenuSave, MenuSaveAs,
        MenuPluginManager, MenuAsioSettings, MenuSetlist, MenuMidiRules, MenuQuit,
        MenuLoadLoop, MenuLoopMode, MenuLiveMode, MenuEditUIColors,
        MenuRecentBase = 1000,  // IDs 1000-1009 for recent projects
        MenuMidiInBase = 2000,  // IDs 2000+ for MIDI input devices
        MenuMidiOutBase = 3000  // IDs 3000+ for MIDI output devices
    };

    void showPluginManager();
    void showMetronomeSettings();
    void showShortcutHelp();
    void showMidiRulesEditor();
    void autosave();
    juce::File getAutosaveFile() const;
    void checkAutosaveRecovery();
    void swapChannels (int fromIndex, int toIndex);
    int  dragSourceChannel = -1;
    void saveLastProjectPath();
    juce::StringArray loadRecentProjects();
    void addToRecentProjects (const juce::File& file);
    void openRecentProject (int index);

    static juce::File getSettingsFile();
    void saveAudioDeviceState();
    void restoreAudioDeviceState();

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
