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
#include "SongBar.h"
#include "LoadingOverlay.h"
#include "AmpLibrary.h"
#include "CassetteDeckWindow.h"
#include "LoopStationWindow.h"
#include "MetronomeWindow.h"
#include "ReelRecorderWindow.h"
#include "AmpLibraryBrowserWindow.h"

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
                      public juce::DragAndDropTarget,
                      public juce::FileDragAndDropTarget
{
public:
    MainComponent();
    ~MainComponent() override;

    //==========================================================================
    // OS file drag-drop: .nam captures and IR wavs import into the amp library.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

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
    bool isProjectDirty() const { return projectDirty; }
    void loadProjectData (const ProjectData& data);
    void loadProjectPlugins (const ProjectData& data);

    /** Rebuild one strip's plugin chain from a saved ChannelState.
        Queues each slot in order through the strip's async load FIFO (which
        preserves chain order), restoring state blob, bypass and appearance as
        each one lands. onSlotLoaded fires on the message thread once per slot,
        loaded or not, so callers can drive a progress count.
        Does NOT clear the strip first - the caller decides that. */
    void restoreChainInto (ChannelStrip& strip,
                           const ChannelState& state,
                           std::function<void()> onSlotLoaded);
    void loadSongState (const ProjectData& data);
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
    //==========================================================================
    // Tuner: any plugin slot can be marked as the tuner, addressed the same way
    // MIDI slot bindings are ("<strip>:<slot>"). The TUNER button un-bypasses
    // it and opens its editor; pressing again closes and re-bypasses. Audio
    // keeps flowing throughout. Empty = no tuner marked. Saved per project.
    juce::String tunerSlot;
    bool         tunerActive = false;

    juce::String tunerSlotStrip() const;
    int          tunerSlotIndex() const;
    void         toggleTuner();

    bool slotHasPlugin        (const juce::String& stripId, int slotIndex);
    void openPluginEditorFor  (const juce::String& stripId, int slotIndex);
    void closePluginEditorFor (const juce::String& stripId, int slotIndex);

    /** Master output fader position in dB, mirrored into masterOutputGain.
        Kept as a member so it can be saved - the gain itself is a raw
        multiplier and the knob is owned by FxBusPanel. */
    float  masterFaderDb = 0.0f;
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

    // Audio-thread MidiBuffer scratch. Members, not locals: MidiBuffer::clear()
    // keeps its capacity, so after the first few blocks these stop allocating
    // entirely. As locals they heap-allocated ~6 times per block whenever MIDI
    // was flowing - which, with Active Sensing or MIDI clock, is always.
    juce::MidiBuffer   rtIncomingMidi;   // swapped out of pendingMidi
    juce::MidiBuffer   rtInputMidi;      // pre-FX input channel
    juce::MidiBuffer   rtChannelMidi;    // refilled per channel
    juce::MidiBuffer   rtFxMidi;         // master insert chain
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
    std::unique_ptr<SongBar> songBar;

    // Noise gate
    juce::ToggleButton gateToggle      { "Gate" };
    juce::Slider       gateThreshSlider;
    juce::Label        gateThreshLabel;

    // Scenes row (8 scene buttons)
    juce::TextButton   sceneButtons[NUM_SCENES];
    juce::TextButton   saveSceneButtons[NUM_SCENES];
    int                activeSceneIndex = -1;
    int                sceneFlashIndex  = -1;
    int                sceneFlashCounter = 0;

    // Hold-to-save: numpad key hold detection
    int                heldSceneIndex   = -1;
    juce::int64        holdStartMs      = 0;
    float              heldSceneProgress = 0.0f;  // 0..1 numpad hold-to-save (#5)
    int                midiBadgeTick     = 0;      // throttles MIDI tooltip refresh (#6)

    // Scene recall mute (glitch prevention)
    bool               sceneMuteActive  = false;

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
    // Screen bounds of each channel strip, cached in resized() for paint-time
    // overlays (active-channel frame #1, scribble strips #7).
    juce::Rectangle<int>               channelStripBounds[NUM_CHANNELS];
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

    // Hardware module windows opened from toolbar icons
    std::unique_ptr<CassetteDeckWindow> cassetteDeckWindow;
    std::unique_ptr<LoopStationWindow>  loopStationWindow;
    std::unique_ptr<MetronomeWindow>    metronomeWindow;
    std::unique_ptr<ReelRecorderWindow> reelRecorderWindow;
    std::unique_ptr<AmpLibraryBrowserWindow> ampBrowserWindow;
    juce::TextButton ampsButton { "AMPS" };
    AmpLibraryBrowserWindow& ensureAmpBrowser();
    void applyNamMidiParam (const juce::String& paramID, float value);

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
    /** @param slot  target slot, or -1 for the first free one. */
    void showAddPluginMenu (int channelIndex, int slot);
    void showAddPluginMenuForFxBus (int slot);

    /** Tells the user a plugin could not be placed because the rack is full. */
    void warnRackFull();
    void showKnobColorMenu (juce::Component* knob);
    void openAsioSettings();
    void showSetlistPanel();
    void saveSongState();

    void updateSceneButtonStates();
    void flashSceneButton (int sceneIndex);
    void applySceneWithMute (int sceneIndex);
    void captureSceneFromCurrent (int sceneIndex);
    void updateTransportUI();
    void updateRoutingModeButton();  // sync routing pill text/colour/tooltip
    void updateStatusBar();

    // Shared floating value readout shown while turning a knob (#2 usability).
    std::unique_ptr<juce::Label> knobReadout;
    void showKnobReadout (juce::Component& nearComp, const juce::String& text);
    void hideKnobReadout();

    // Maps a learnable on-screen control to its MidiLearnManager paramID (#2/#6).
    std::map<juce::Component*, juce::String> learnableControls;
    juce::String paramIdForComponent (juce::Component* c) const;
    void updateFaderLabel (int channelIndex);
    void updateActiveIndicators();
    void showChannelRenameDialog (int channelIndex);

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
            // Direct form II transposed.
            float w = x - a1*z1 - a2*z2;
            float y = b0*w + b1*z1 + b2*z2;
            z2 = z1; z1 = w;
            // A single NaN/Inf would persist in the filter state and poison all
            // subsequent output (the metering would read silence forever); reset.
            if (! std::isfinite (z1) || ! std::isfinite (z2)) { z1 = z2 = 0; return 0.0f; }
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };
    Biquad kShelfL, kShelfR, kHpL, kHpR;

    // Pre-allocated audio scratch buffers, sized in prepareToPlay(). Allocating
    // these in getNextAudioBlock() would heap-allocate on the real-time thread
    // every block — a classic cause of glitches/dropouts.
    juce::AudioBuffer<float> work, masterMix, directSignal, silentBuffer;
    juce::AudioBuffer<float> channelOutputs[NUM_CHANNELS];

    enum MenuIDs
    {
        MenuNew = 1, MenuOpen, MenuSave, MenuSaveAs,
        MenuPluginManager, MenuAsioSettings, MenuSetlist, MenuMidiRules, MenuQuit,
        MenuLoadLoop, MenuLoopMode, MenuLiveMode, MenuEditUIColors,
        MenuImportMidiMap,
        MenuRecentBase = 1000,  // IDs 1000-1009 for recent projects
        MenuMidiInBase = 2000,  // IDs 2000+ for MIDI input devices
        MenuMidiOutBase = 3000  // IDs 3000+ for MIDI output devices
    };

    /** Replace this project's MIDI learn bindings and translator rules with
        the ones stored in another .upstage file. Everything else in that file
        is ignored - channels, plugins, scenes and device selection are left
        alone. Imports into the session only; it sticks when the project is
        saved. */
    void importMidiMapFromProject();

    void showPluginManager();
    void showShortcutHelp();
    void showMidiRulesEditor();
    void autosave();
    juce::File getAutosaveFile() const;
    void checkAutosaveRecovery();
    void swapChannels (int fromIndex, int toIndex);

    //==========================================================================
    // Whole-channel copy / paste (session-only clipboard, like any clipboard).
    struct ChannelClipboard
    {
        bool         valid = false;
        ChannelState state;              // name, chain + plugin blobs, gains, pan
        bool         muted   = false;
        bool         soloed  = false;
        double       faderDb = 0.0;      // outputFaders value
        double       panKnob = 0.0;      // outputGainKnobs value
        double       trimKnob = 0.0;     // inputTrimKnobs value
    };

    ChannelClipboard channelClipboard;

    void copyChannelToClipboard (int channelIndex);
    void pasteChannelFromClipboard (int channelIndex);
    void showChannelContextMenu (int channelIndex);

    //==========================================================================
    /** Momentary MIDI targets (slot on/off, tap tempo) fire on the rising edge
        of the CC so one footswitch press = one action, whether the switch is
        momentary (127 then 0) or latching (127, 0, 127...).
        Maps paramID -> whether the switch was last seen pressed.
        Message thread only. */
    std::map<juce::String, bool> momentaryPressed;

    /** Returns true when `value` is a 0->1 transition for paramID. */
    bool isRisingEdge (const juce::String& paramID, float value);

    /** Returns true when this message counts as one press of the bound switch,
        honouring the binding's momentary/latching mode. */
    bool isSwitchPress (const juce::String& paramID, float value);

    /** Resolve a slot address to its strip + panel. stripId is "ch0".."ch3" or
        "in"; the FX bus ("fx") is a different type and is handled by the
        callers. Returns false when the address doesn't name a loaded slot. */
    bool resolveSlot (const juce::String& stripId, int slotIndex,
                      ChannelStrip*& strip, ChannelStripPanel*& panel);

    /** Set/read one slot's bypass and refresh its panel. stripId is "ch0".."ch3",
        "in" or "fx". */
    void setSlotBypassed (const juce::String& stripId, int slotIndex, bool bypassed);
    bool isSlotBypassed  (const juce::String& stripId, int slotIndex);

    /** One tap of the tap-tempo clock, from the button, a key or MIDI. */
    void doTap();

    /** juce::Button fires its click on mouse-up for any mouse button, so a
        right-click meant for the TAP button's Learn MIDI menu would also tap.
        Recorded fresh on every mouse-down over the button, so it can't go
        stale, and checked in buttonClicked(). */
    bool tapClickWasRightButton = false;
    int  dragSourceChannel = -1;
    void saveLastProjectPath();
    juce::StringArray loadRecentProjects();
    void addToRecentProjects (const juce::File& file);
    void openRecentProject (int index);

    static juce::File getSettingsFile();
    void saveAudioDeviceState();
    void restoreAudioDeviceState();

    std::unique_ptr<juce::FileChooser> fileChooser;
    LoadingOverlay loadingOverlay;
    int            pendingPluginLoads = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
