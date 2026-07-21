#include "MainComponent.h"
#include "MixerLookAndFeel.h"
#include <windows.h>
#include <psapi.h>

//==============================================================================
// Format a -1..+1 pan value as L/C/R text for the knob readout (#2).
static juce::String panText (double v)
{
    int p = juce::roundToInt (v * 100.0);
    if (p == 0) return "C";
    return (p < 0 ? "L" : "R") + juce::String (std::abs (p));
}

//==============================================================================
MainComponent::MainComponent() : menuBar (this)
{
#if JUCE_DEBUG
    // TEMP (Task 1 verification): amp-library self-test, removed once a real A2
    // capture has confirmed the version predicate.
    {
        juce::String report;
        auto scratch = juce::File (R"(C:\Users\alex\AppData\Local\Temp\claude\C--projects-A18-UpStage\81cf921b-2c4c-41a0-9cc5-8cd02c1222d3\scratchpad)");
        juce::String err;
        const bool a1Accepted = AmpLibrary::isA2NamFile (scratch.getChildFile ("fake_a1.nam"), err);
        report << "A1 reject test: " << (a1Accepted ? "FAIL (accepted!)" : "PASS - " + err) << "\n";
        err.clear();
        const bool a2Accepted = AmpLibrary::isA2NamFile (scratch.getChildFile ("fake_a2.nam"), err);
        report << "A2 accept test: " << (a2Accepted ? "PASS" : "FAIL - " + err) << "\n";
        AmpLibrary::instance().rescan();
        report << "Library root: " << AmpLibrary::instance().getRootFolder().getFullPathName() << "\n";
        report << "Entries: " << AmpLibrary::instance().getEntries().size() << "\n";
        scratch.getChildFile ("amp_library_selftest.txt").replaceWithText (report);
    }
#endif

    pluginFormatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    DBG ("Plugin formats registered: " + juce::String (pluginFormatManager.getNumFormats()));
    for (int i = 0; i < pluginFormatManager.getNumFormats(); ++i)
    {
        DBG ("Format " + juce::String(i) + ": " + pluginFormatManager.getFormat(i)->getName());
    }

    // Create input channel (pre-FX)
    inputChannel = std::make_unique<ChannelStrip> (-1, pluginFormatManager);
    inputChannel->setName ("Input");
    inputChannel->setActive (true);

    // Create channel strips and FX bus
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        channels[i] = std::make_unique<ChannelStrip> (i, pluginFormatManager);
        channels[i]->setActive (i == 0);
        levelMeters[i] = std::make_unique<LevelMeter> (LevelMeter::Orientation::Vertical, LevelMeter::ColourMode::Green);
        inputLevelMeters[i] = std::make_unique<LevelMeter> (LevelMeter::Orientation::Vertical, LevelMeter::ColourMode::Amber);
    }

    fxBus = std::make_unique<FxBus> (pluginFormatManager);

    // Set play head for tempo sync on all plugin hosts
    inputChannel->setPlayHead (this);
    for (auto& ch : channels)
        ch->setPlayHead (this);
    fxBus->setPlayHead (this);

    // Register MIDI-learnable parameters
    midiLearnManager.addListener (this);
    midiLearnManager.registerParameter ("loopVolume",  0.0f,  1.0f);
    midiLearnManager.registerParameter ("gateThresh", -80.0f, 0.0f);
    midiLearnManager.registerParameter ("inputTrim",  -24.0f, 24.0f);

    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        midiLearnManager.registerParameter ("chFader" + juce::String (i), -60.0f, 12.0f);
        midiLearnManager.registerParameter ("chPan"   + juce::String (i), -1.0f,  1.0f);
        midiLearnManager.registerParameter ("chMute"  + juce::String (i),  0.0f,  1.0f);
    }
    midiLearnManager.registerParameter ("masterFader",  -60.0f, 12.0f);
    midiLearnManager.registerParameter ("fxBypass",       0.0f,  1.0f);
    midiLearnManager.registerParameter ("metroToggle",    0.0f,  1.0f);

    //==========================================================================
    addAndMakeVisible (menuBar);

    // Transport buttons - console style
    for (auto* b : { &tapButton, &tunerButton, &panicButton,
                     &recordButton,
                     &metronomeButton, &loopRecButton,
                     &routingModeButton, &toolbarExpandButton })
    {
        b->addListener (this);
        addAndMakeVisible (b);
    }

    tunerButton.setComponentID ("icon_tuner");
    panicButton.setComponentID ("icon_panic");
    recordButton.setComponentID ("icon_rec");
    playLoopButton.setComponentID ("icon_play");
    metronomeButton.setComponentID ("icon_metro");
    loopRecButton.setComponentID ("icon_loop");
    tapButton.setComponentID ("icon_tap");
    toolbarExpandButton.setComponentID ("icon_expand");

    // Style individual transport buttons
    tapButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a2a));
    tapButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88cc88));

    tunerButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a3a));
    tunerButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff8888cc));
    tunerButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4444aa));

    panicButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a2a2a));
    panicButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc8888));

    recordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a1a1a));
    recordButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc4444));
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffcc2222));
    recordButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffff8888));
    recordButton.setTooltip ("Open the Reel Recorder: choose dry/wet/both, folder, then REC/STOP.");

    stopRecordButton.setVisible (false);  // recording stop now lives in the Reel Recorder window

    metronomeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a3a));
    metronomeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9999cc));
    metronomeButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffccccff));
    metronomeButton.setTooltip ("Open the Metronome: start/stop, BPM, time sig, subdivision, sound, volume.");

    loopRecButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a2a));
    loopRecButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88cc88));
    loopRecButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffbbffbb));
    loopRecButton.setTooltip ("Open the Loop Station: record, overdub, play, and loop settings.");

    looperProgressLabel.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
    looperProgressLabel.setJustificationType (juce::Justification::centred);
    looperProgressLabel.setColour (juce::Label::textColourId, juce::Colour (0xff88cc88));
    looperProgressLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x00000000));
    addAndMakeVisible (looperProgressLabel);

    looper.onRecordingStarted = [this]()
    {
        juce::MessageManager::callAsync ([this]()
        {
            if (looper.getCountInBeats() > 0 && ! metronome.isEnabled())
            {
                metronome.setBPM (tapTempo.getBPM());
                metronome.setTimeSignature (looper.getMeterNum(), looper.getMeterDen());
                metronome.setEnabled (true);  // toolbar icon lit state follows in timerCallback
            }
        });
    };
    looper.onRecordingStopped = [this]()
    {
        juce::MessageManager::callAsync ([this]()
        {
            if (looper.getCountInBeats() > 0 && metronome.isEnabled())
            {
                metronome.setEnabled (false);  // toolbar icon lit state follows in timerCallback
            }
        });
    };

    updateRoutingModeButton();

    toolbarExpandButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252525));
    toolbarExpandButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff888888));

    helpButton.addListener (this);
    addAndMakeVisible (helpButton);
    helpButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252535));
    helpButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff8888cc));
    helpButton.setTooltip ("Keyboard shortcuts");

    asioButton.setVisible (false);

    // Hide unused buttons - accessed via menu instead
    liveInputButton.setVisible (false);
    loopInputButton.setVisible (false);
    loadLoopButton.setVisible (false);
    recDryToggle.setVisible (false);
    recWetToggle.setVisible (false);
    gateToggle.setVisible (false);
    gateThreshSlider.setVisible (false);
    gateThreshLabel.setVisible (false);
    clockToggle.setVisible (false);

    playLoopButton.addListener (this);
    addAndMakeVisible (playLoopButton);
    playLoopButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a3a1a));
    playLoopButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff44cc44));
    playLoopButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff66ff66));
    playLoopButton.setTooltip ("Cassette deck: play an audio file through your chain to audition tones.");
    stopRecordButton.setEnabled (false);

    loopFileLabel.setText ("No file", juce::dontSendNotification);
    loopFileLabel.setFont (juce::Font(juce::FontOptions().withHeight(11.0f)));
    addAndMakeVisible (loopFileLabel);

    loopVolumeSlider.setRange (0.0, 1.0, 0.01);
    loopVolumeSlider.setValue (1.0);
    loopVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    loopVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    loopVolumeSlider.onValueChange = [this] {
        inputRouter.loopVolume = (float) loopVolumeSlider.getValue();
    };
    // Right-click MIDI Learn is handled in MainComponent::mouseDown
    addAndMakeVisible (loopVolumeSlider);

    // Input trim slider (-24 dB … +24 dB)
    inputTrimSlider.setRange (-24.0, 24.0, 0.1);
    inputTrimSlider.setValue (0.0, juce::dontSendNotification);
    inputTrimSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    inputTrimSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    inputTrimSlider.setTextValueSuffix (" dB");
    inputTrimSlider.setTooltip ("Input trim applied before the noise gate. Range: -24 dB to +24 dB. Right-click for MIDI Learn.");
    inputTrimSlider.onValueChange = [this] { projectDirty = true; };
    // Right-click MIDI Learn is handled in MainComponent::mouseDown
    inputTrimLabel.setText ("Trim", juce::dontSendNotification);
    inputTrimLabel.setFont (juce::Font(juce::FontOptions().withHeight(12.0f)));
    addAndMakeVisible (inputTrimSlider);
    addAndMakeVisible (inputTrimLabel);

    // Tap tempo
    bpmLabel.setText ("120 BPM", juce::dontSendNotification);
    bpmLabel.setFont (juce::Font(juce::FontOptions().withHeight(12.0f)));
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (bpmLabel);
    tapTempo.onBPMChanged = [this] (double bpm) {
        metronome.setBPM (bpm);
        juce::MessageManager::callAsync ([this, bpm] {
            bpmLabel.setText (juce::String ((int) bpm) + " BPM",
                              juce::dontSendNotification);
        });
    };
    clockToggle.addListener (this);

    // Noise gate
    gateToggle.addListener (this);
    gateThreshSlider.setRange (-80.0, 0.0, 0.5);
    gateThreshSlider.setValue (-60.0);
    gateThreshSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gateThreshSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 45, 18);
    gateThreshSlider.setTextValueSuffix (" dB");
    gateThreshSlider.onValueChange = [this] {
        noiseGate.thresholdDb = (float) gateThreshSlider.getValue();
    };
    gateThreshLabel.setText ("Thresh", juce::dontSendNotification);
    gateThreshLabel.setFont (juce::Font(juce::FontOptions().withHeight(11.0f)));
    addAndMakeVisible (gateThreshSlider);
    addAndMakeVisible (gateThreshLabel);

    // Scenes - right-click to save, hardware snapshot style
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        sceneButtons[i].setButtonText ("S" + juce::String (i + 1));
        sceneButtons[i].setComponentID ("scene_btn");  // dot-matrix LCD rendering
        sceneButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1a1a));
        sceneButtons[i].setColour (juce::TextButton::textColourOffId, juce::Colour (0xff555555));
        sceneButtons[i].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a5a2a));
        sceneButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff88ff88));
        sceneButtons[i].setTooltip ("Click: recall scene. Right-click: save / rename / clear. "
                                    "Hold numpad 1-8 (3s): save.");
        sceneButtons[i].addListener (this);
        sceneButtons[i].addMouseListener (this, false);
        addAndMakeVisible (sceneButtons[i]);

        saveSceneButtons[i].setVisible (false);
    }

    // MIXER-STYLE CHANNEL STRIPS
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        // Channel label - now clickable and editable
        channelLabels[i].setText ("CH " + juce::String(i + 1), juce::dontSendNotification);
        channelLabels[i].setComponentID ("strip_label");  // dot-matrix LCD rendering
        channelLabels[i].setFont (juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
        channelLabels[i].setJustificationType (juce::Justification::centred);
        channelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
        // Transparent: the backlit-LCD scribble-strip backing is drawn in paint() (#7).
        channelLabels[i].setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        channelLabels[i].setEditable (false, true, false); // single-click to activate, double-click to edit
        channelLabels[i].setMouseCursor (juce::MouseCursor::PointingHandCursor);
        channelLabels[i].setTooltip ("Click: select | Double-click: rename | Drag: reorder");
        channelLabels[i].onTextChange = [this, i] {
            channels[i]->setName (channelLabels[i].getText().toStdString());
        };
        channelLabels[i].addMouseListener (this, false);
        addAndMakeVisible (channelLabels[i]);

        // Level meter
        addAndMakeVisible (*levelMeters[i]);
        addAndMakeVisible (*inputLevelMeters[i]);

        // Output fader (vertical)
        outputFaders[i].setName ("fader_ch" + juce::String(i+1));
        outputFaders[i].setComponentID ("fader_ch" + juce::String(i+1));
        outputFaders[i].setSliderStyle (juce::Slider::LinearVertical);
        outputFaders[i].setRange (-60.0, 12.0, 0.1);
        outputFaders[i].setValue (0.0);
        outputFaders[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        outputFaders[i].setSkewFactorFromMidPoint (-12.0);
        outputFaders[i].addMouseListener (this, false);
        outputFaders[i].setDoubleClickReturnValue (true, 0.0);
        outputFaders[i].onDragStart = [this, i] {
            faderDragStartValue[i] = outputFaders[i].getValue();
            undoManager.beginNewTransaction ("Fader CH" + juce::String (i + 1));
        };
        outputFaders[i].onDragEnd = [this, i] {
            double endVal = outputFaders[i].getValue();
            if (std::abs (endVal - faderDragStartValue[i]) > 0.01)
                undoManager.perform (new FaderChangeAction (outputFaders[i], faderDragStartValue[i], endVal));
        };
        outputFaders[i].onValueChange = [this, i] {
            float gainDb = (float) outputFaders[i].getValue();
            channels[i]->setOutputGain (juce::Decibels::decibelsToGain (gainDb));
            updateFaderLabel (i);
        };
        addAndMakeVisible (outputFaders[i]);

        // Fader level label - tiny and subtle
        faderLevelLabels[i].setText ("0.0 dB", juce::dontSendNotification);
        faderLevelLabels[i].setFont (juce::Font(juce::FontOptions().withHeight(11.0f)));
        faderLevelLabels[i].setComponentID ("readout");   // recessed LCD readout
        faderLevelLabels[i].setJustificationType (juce::Justification::centred);
        faderLevelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffbfe8c8));
        faderLevelLabels[i].setColour (juce::Label::backgroundColourId, juce::Colour (0xff1a1a1a));
        faderLevelLabels[i].setColour (juce::Label::outlineColourId, juce::Colour (0xff333333));
        faderLevelLabels[i].setEditable (false, true, false);
        faderLevelLabels[i].onTextChange = [this, i] {
            auto text = faderLevelLabels[i].getText().trimCharactersAtEnd (" dB").trim();
            double db = text.getDoubleValue();
            db = juce::jlimit (-60.0, 12.0, db);
            outputFaders[i].setValue (db);
        };
        addAndMakeVisible (faderLevelLabels[i]);

        // Meter labels
        inMeterLabels[i].setText ("IN", juce::dontSendNotification);
        inMeterLabels[i].setFont (juce::Font (juce::FontOptions().withHeight (8.0f)));
        inMeterLabels[i].setJustificationType (juce::Justification::centred);
        inMeterLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xff998844));
        addAndMakeVisible (inMeterLabels[i]);

        outMeterLabels[i].setText ("OUT", juce::dontSendNotification);
        outMeterLabels[i].setFont (juce::Font (juce::FontOptions().withHeight (8.0f)));
        outMeterLabels[i].setJustificationType (juce::Justification::centred);
        outMeterLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xff449944));
        addAndMakeVisible (outMeterLabels[i]);

        // Output gain knob label - subtle, small
        outputGainLabels[i].setText ("PAN", juce::dontSendNotification);
        outputGainLabels[i].setFont (juce::Font(juce::FontOptions().withHeight(9.0f)));
        outputGainLabels[i].setJustificationType (juce::Justification::centred);
        outputGainLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xff666666));
        addAndMakeVisible (outputGainLabels[i]);

        // Pan knob (-1.0 left to +1.0 right)
        outputGainKnobs[i].setName ("ch" + juce::String(i+1) + "_pan");
        outputGainKnobs[i].setComponentID ("ch" + juce::String(i+1) + "_pan");
        outputGainKnobs[i].addMouseListener (this, false);
        outputGainKnobs[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        outputGainKnobs[i].setRange (-1.0, 1.0, 0.01);
        outputGainKnobs[i].setValue (0.0);
        outputGainKnobs[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        outputGainKnobs[i].setTooltip ("Pan (L/R)");
        outputGainKnobs[i].setDoubleClickReturnValue (true, 0.0);
        outputGainKnobs[i].onValueChange = [this, i] {
            channels[i]->setPan ((float) outputGainKnobs[i].getValue());
            showKnobReadout (outputGainKnobs[i], panText (outputGainKnobs[i].getValue()));
        };
        outputGainKnobs[i].onDragStart = [this, i] {
            showKnobReadout (outputGainKnobs[i], panText (outputGainKnobs[i].getValue())); };
        outputGainKnobs[i].onDragEnd = [this] { hideKnobReadout(); };
        addAndMakeVisible (outputGainKnobs[i]);

        // Input trim knob label - subtle, small
        inputTrimLabels[i].setText ("IN", juce::dontSendNotification);
        inputTrimLabels[i].setFont (juce::Font(juce::FontOptions().withHeight(9.0f)));
        inputTrimLabels[i].setJustificationType (juce::Justification::centred);
        inputTrimLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xff666666));
        addAndMakeVisible (inputTrimLabels[i]);

        // Input trim knob (per-channel) - no text box, blue color
        inputTrimKnobs[i].setName ("ch" + juce::String(i+1) + "_in");
        inputTrimKnobs[i].setComponentID ("ch" + juce::String(i+1) + "_in");
        inputTrimKnobs[i].addMouseListener (this, false);
        inputTrimKnobs[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        inputTrimKnobs[i].setRange (-24.0, 24.0, 0.1);
        inputTrimKnobs[i].setValue (0.0);
        inputTrimKnobs[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        inputTrimKnobs[i].setTooltip ("Input Trim");
        inputTrimKnobs[i].setDoubleClickReturnValue (true, 0.0);
        inputTrimKnobs[i].onValueChange = [this, i] {
            float gainDb = (float) inputTrimKnobs[i].getValue();
            channels[i]->setInputGain (juce::Decibels::decibelsToGain (gainDb));
            showKnobReadout (inputTrimKnobs[i], juce::String (gainDb, 1) + " dB");
        };
        inputTrimKnobs[i].onDragStart = [this, i] {
            showKnobReadout (inputTrimKnobs[i], juce::String (inputTrimKnobs[i].getValue(), 1) + " dB"); };
        inputTrimKnobs[i].onDragEnd = [this] { hideKnobReadout(); };
        addAndMakeVisible (inputTrimKnobs[i]);

        // Plugin chain panel
        channelStripPanels[i] = std::make_unique<ChannelStripPanel> (*channels[i]);
        channelStripPanels[i]->onAddPluginClicked = [this, i] { showAddPluginMenu (i); };
        channelStripPanels[i]->onPastePlugin = [this, i] (const juce::String& id, const juce::MemoryBlock& state, bool bypassed)
        {
            if (auto found = knownPluginList.getTypeForIdentifierString (id))
            {
                channels[i]->addPlugin (*found, [this, i, state, bypassed] (bool ok)
                {
                    if (! ok) return;
                    int slot = channels[i]->getNumPlugins() - 1;
                    if (state.getSize() > 0)
                        if (auto* proc = channels[i]->getPlugin (slot))
                            proc->setStateInformation (state.getData(), (int) state.getSize());
                    channels[i]->setPluginBypassed (slot, bypassed);
                    juce::MessageManager::callAsync ([this, i] { channelStripPanels[i]->refresh(); });
                });
            }
        };
        addAndMakeVisible (*channelStripPanels[i]);

        // Add plugin button
        addPluginButtons[i].setButtonText ("+ Add Plugin");
        addPluginButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff222222));
        addPluginButtons[i].setColour (juce::TextButton::textColourOffId, juce::Colour (0xff5588aa));
        addPluginButtons[i].addListener (this);
        addAndMakeVisible (addPluginButtons[i]);

        // Solo button - dim yellow when off, bright yellow when on
        soloButtons[i].setButtonText ("S");
        soloButtons[i].setClickingTogglesState (true);
        soloButtons[i].setTooltip ("Solo");
        soloButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3520));
        soloButtons[i].setColour (juce::TextButton::textColourOffId, juce::Colour (0xff887744));
        soloButtons[i].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffaa00));
        soloButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        soloButtons[i].addListener (this);
        addAndMakeVisible (soloButtons[i]);

        // Mute button - dim red when off, bright red when on
        muteButtons[i].setButtonText ("M");
        muteButtons[i].setClickingTogglesState (true);
        muteButtons[i].setTooltip ("Mute");
        muteButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a2020));
        muteButtons[i].setColour (juce::TextButton::textColourOffId, juce::Colour (0xff884444));
        muteButtons[i].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffdd3333));
        muteButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        muteButtons[i].addListener (this);
        addAndMakeVisible (muteButtons[i]);

        // Active indicator - use a toggle button styled as indicator
        activeIndicators[i].setButtonText ("");
        activeIndicators[i].setClickingTogglesState (false);
        activeIndicators[i].onClick = [this, i] { setActiveChannel (i); };
        addAndMakeVisible (activeIndicators[i]);

        // Initialize fader label
        updateFaderLabel (i);
    }

    // Initialize active indicators
    updateActiveIndicators();

    // Input channel UI
    inputChannelLabel.setText ("INPUT", juce::dontSendNotification);
    inputChannelLabel.setComponentID ("strip_label");  // dot-matrix LCD rendering
    inputChannelLabel.setFont (juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
    inputChannelLabel.setJustificationType (juce::Justification::centred);
    inputChannelLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffaa66));
    addAndMakeVisible (inputChannelLabel);

    inputChannelMeterIn = std::make_unique<LevelMeter> (LevelMeter::Orientation::Vertical);
    addAndMakeVisible (*inputChannelMeterIn);

    inputChannelMeterOut = std::make_unique<LevelMeter> (LevelMeter::Orientation::Vertical);
    addAndMakeVisible (*inputChannelMeterOut);

    inputChannelPanel = std::make_unique<ChannelStripPanel> (*inputChannel);
    inputChannelPanel->onAddPluginClicked = [this] { showAddPluginMenu (-1); };
    inputChannelPanel->onPastePlugin = [this] (const juce::String& id, const juce::MemoryBlock& state, bool bypassed)
    {
        if (auto found = knownPluginList.getTypeForIdentifierString (id))
        {
            inputChannel->addPlugin (*found, [this, state, bypassed] (bool ok)
            {
                if (! ok) return;
                int slot = inputChannel->getNumPlugins() - 1;
                if (state.getSize() > 0)
                    if (auto* proc = inputChannel->getPlugin (slot))
                        proc->setStateInformation (state.getData(), (int) state.getSize());
                inputChannel->setPluginBypassed (slot, bypassed);
                juce::MessageManager::callAsync ([this] { inputChannelPanel->refresh(); });
            });
        }
    };
    addAndMakeVisible (*inputChannelPanel);

    addInputPluginButton.setButtonText ("+ Add Plugin");
    addInputPluginButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2220));
    addInputPluginButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88775a));
    addInputPluginButton.onClick = [this] { showAddPluginMenu (-1); };
    addAndMakeVisible (addInputPluginButton);

    // Input direct knob (sends post-input-FX signal directly to master mix)
    inputDirectLabel.setText ("DIRECT", juce::dontSendNotification);
    inputDirectLabel.setFont (juce::Font(juce::FontOptions().withHeight(9.0f)));
    inputDirectLabel.setJustificationType (juce::Justification::centred);
    inputDirectLabel.setColour (juce::Label::textColourId, juce::Colour (0xff666666));
    addAndMakeVisible (inputDirectLabel);

    inputDirectKnob.setName ("input_direct");
    inputDirectKnob.setComponentID ("input_direct");
    inputDirectKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    inputDirectKnob.setRange (0.0, 1.0, 0.01);
    inputDirectKnob.setValue (0.0);
    inputDirectKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    inputDirectKnob.setTooltip ("Direct: blends post-input-FX signal into master bus");
    inputDirectKnob.setDoubleClickReturnValue (true, 0.0);
    inputDirectKnob.onValueChange = [this] {
        inputDirectLevel.store ((float) inputDirectKnob.getValue(), std::memory_order_relaxed);
        showKnobReadout (inputDirectKnob,
            juce::String (juce::roundToInt (inputDirectKnob.getValue() * 100.0)) + "%");
    };
    inputDirectKnob.onDragStart = [this] {
        showKnobReadout (inputDirectKnob,
            juce::String (juce::roundToInt (inputDirectKnob.getValue() * 100.0)) + "%");
    };
    inputDirectKnob.onDragEnd = [this] { hideKnobReadout(); };
    addAndMakeVisible (inputDirectKnob);

    // Shared floating value readout (#2) — created hidden, shown on knob turn.
    knobReadout = std::make_unique<juce::Label>();
    knobReadout->setJustificationType (juce::Justification::centred);
    knobReadout->setColour (juce::Label::backgroundColourId, juce::Colour (0xee101010));
    knobReadout->setColour (juce::Label::textColourId,       juce::Colour (0xffd0f0ff));
    knobReadout->setColour (juce::Label::outlineColourId,    juce::Colour (0xff3a3a3a));
    knobReadout->setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
    knobReadout->setInterceptsMouseClicks (false, false);
    knobReadout->setVisible (false);
    addChildComponent (knobReadout.get());

    // Input strip fader label
    inputStripFaderLabel.setText ("0.0 dB", juce::dontSendNotification);
    inputStripFaderLabel.setFont (juce::Font(juce::FontOptions().withHeight(11.0f)));
    inputStripFaderLabel.setComponentID ("readout");   // recessed LCD readout
    inputStripFaderLabel.setJustificationType (juce::Justification::centred);
    inputStripFaderLabel.setColour (juce::Label::textColourId, juce::Colour (0xffbfe8c8));
    inputStripFaderLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1a1a1a));
    inputStripFaderLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff333333));
    addAndMakeVisible (inputStripFaderLabel);

    // Input strip fader (controls input trim in dB)
    inputStripFader.setSliderStyle (juce::Slider::LinearVertical);
    inputStripFader.setRange (-24.0, 24.0, 0.1);
    inputStripFader.setValue (0.0);
    inputStripFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    inputStripFader.setDoubleClickReturnValue (true, 0.0);
    inputStripFader.onValueChange = [this] {
        inputTrimSlider.setValue (inputStripFader.getValue(), juce::sendNotificationSync);
        float db = (float) inputStripFader.getValue();
        juce::String text = (db <= -23.9f) ? "-INF" : juce::String (db, 1) + " dB";
        inputStripFaderLabel.setText (text, juce::dontSendNotification);
    };
    addAndMakeVisible (inputStripFader);

    // FX Bus panel
    fxBusPanel = std::make_unique<FxBusPanel> (*fxBus);
    fxBusPanel->onAddPluginClicked = [this] { showAddPluginMenuForFxBus(); };
    fxBusPanel->onPastePlugin = [this] (const juce::String& id, const juce::MemoryBlock& state, bool bypassed)
    {
        if (auto found = knownPluginList.getTypeForIdentifierString (id))
        {
            fxBus->addPlugin (*found, [this, state, bypassed] (bool ok)
            {
                if (! ok) return;
                int slot = fxBus->getNumPlugins() - 1;
                if (state.getSize() > 0)
                    if (auto* proc = fxBus->getPlugin (slot))
                        proc->setStateInformation (state.getData(), (int) state.getSize());
                fxBus->setPluginBypassed (slot, bypassed);
                juce::MessageManager::callAsync ([this] { fxBusPanel->refresh(); });
                projectDirty = true;
            });
        }
    };
    fxBusPanel->onMasterFaderChanged = [this] (float db) {
        masterOutputGain.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed);
    };
    addAndMakeVisible (*fxBusPanel);


    // Signal chain view removed - using mixer-style layout instead

    // Tuner - initially hidden
    tunerPanel.tunerActive = false;
    addAndMakeVisible (tunerPanel);
    tunerPanel.setVisible (false);

    // MIDI activity LED
    midiLedLabel.setText ("MIDI", juce::dontSendNotification);
    midiLedLabel.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    midiLedLabel.setJustificationType (juce::Justification::centred);
    midiLedLabel.setColour (juce::Label::textColourId, juce::Colour (0xff444444));
    midiLedLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1a1a1a));
    midiLedLabel.addMouseListener (this, false);
    addAndMakeVisible (midiLedLabel);

    // Status bar
    statusLabel.setFont (juce::Font(juce::FontOptions().withHeight(11.0f)));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    statusLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff181818));
    addAndMakeVisible (statusLabel);

    statusStateLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    statusStateLabel.setJustificationType (juce::Justification::centredRight);
    statusStateLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x00000000));
    addAndMakeVisible (statusStateLabel);

    cpuLabel.setFont (juce::Font(juce::FontOptions().withHeight(10.0f)));
    cpuLabel.setColour (juce::Label::textColourId, juce::Colour (0xff88aa88));
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpuLabel);

    ramLabel.setFont (juce::Font(juce::FontOptions().withHeight(10.0f)));
    ramLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
    ramLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ramLabel);

    //==========================================================================
    setlistManager.setSceneManager (&sceneManager);
    setlistManager.setMidiLearnManager (&midiLearnManager);
    setlistManager.onSongChanged = [this] (int idx, const ProjectData& data) {
        juce::MessageManager::callAsync ([this, idx, data]
        {
            auto doLoad = [this, idx, data]
            {
                loadProjectData (data);

                if (auto* song = setlistManager.getSong (idx))
                {
                    if (song->preferredSceneIndex >= 0)
                        applySceneWithMute (song->preferredSceneIndex);
                }
                if (songBar) songBar->refresh();
            };

            if (projectDirty)
            {
                auto* aw = new juce::AlertWindow (
                    "Unsaved Changes",
                    "Current project has unsaved changes. Save before switching songs?",
                    juce::AlertWindow::QuestionIcon);
                aw->addButton ("Save", 1);
                aw->addButton ("Don't Save", 2);
                aw->addButton ("Cancel", 0);
                aw->enterModalState (true, juce::ModalCallbackFunction::create (
                    [doLoad, aw, this] (int result)
                    {
                        if (result == 0) { delete aw; return; }
                        if (result == 1) saveProject();
                        doLoad();
                        delete aw;
                    }), false);
            }
            else
            {
                doLoad();
            }
        });
    };

    //==========================================================================
    // Song bar (persistent setlist navigation strip)
    songBar = std::make_unique<SongBar> (setlistManager);
    songBar->onSongSelected = [this] (int idx)
    {
        ProjectData data;
        if (setlistManager.loadSongAtIndex (idx, data))
        {
            loadProjectData (data);
            if (auto* song = setlistManager.getSong (idx))
            {
                if (song->preferredSceneIndex >= 0)
                    applySceneWithMute (song->preferredSceneIndex);
            }
        }
    };
    songBar->onOpenSetlist = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Open Setlist", juce::File{}, "*.setlist");
        chooser->launchAsync (juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    setlistManager.loadSetlist (result);
                    SongBar::addToRecentSetlists (result);
                    ProjectData data;
                    if (setlistManager.loadSongAtIndex (0, data))
                        loadProjectData (data);
                    if (songBar) songBar->refresh();
                }
            });
    };
    songBar->onSaveSetlist = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Save Setlist", juce::File{}, "*.setlist");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode |
                              juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result != juce::File{})
                {
                    auto f = result.withFileExtension ("setlist");
                    setlistManager.saveSetlist (f);
                    SongBar::addToRecentSetlists (f);
                }
            });
    };
    songBar->onClearSetlist = [this]
    {
        setlistManager.clear();
        if (songBar) songBar->refresh();
    };
    songBar->onLoadSetlistFile = [this] (const juce::File& f)
    {
        setlistManager.loadSetlist (f);
        SongBar::addToRecentSetlists (f);
        ProjectData data;
        if (setlistManager.loadSongAtIndex (0, data))
            loadProjectData (data);
        if (songBar) songBar->refresh();
    };
    addAndMakeVisible (*songBar);

    //==========================================================================
    // Initialize audio with ASIO support
    // Add all available device types (Windows Audio, DirectSound, ASIO, etc.)
    deviceManager.createAudioDeviceTypes (deviceTypes);

    juce::String typeNames = "Available device types: ";
    for (auto* type : deviceTypes)
    {
        typeNames += type->getTypeName() + ", ";
        deviceManager.addAudioDeviceType (std::unique_ptr<juce::AudioIODeviceType> (type));
    }
    deviceTypes.clear (false);

    DBG (typeNames);  // Debug output to see what types were found

    setAudioChannels (1, 2);
    restoreAudioDeviceState();

    // Enable ALL MIDI inputs so every connected device is heard
    for (auto& dev : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled (dev.identifier, true);
        deviceManager.addMidiInputDeviceCallback (dev.identifier, this);
    }
    activeMidiInputId = {};

    auto midiOutDevices = juce::MidiOutput::getAvailableDevices();
    if (! midiOutDevices.isEmpty())
    {
        activeMidiOutputId = midiOutDevices[0].identifier;
        midiOutput = juce::MidiOutput::openDevice (activeMidiOutputId);
    }

    // Load cached plugin list
    auto pluginCacheFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("UpStage").getChildFile ("PluginCache.xml");

    DBG ("Plugin cache file: " + pluginCacheFile.getFullPathName());
    DBG ("Cache exists: " + juce::String (pluginCacheFile.existsAsFile() ? "yes" : "no"));

    if (pluginCacheFile.existsAsFile())
    {
        if (auto xml = juce::parseXML (pluginCacheFile))
        {
            knownPluginList.recreateFromXml (*xml);
            DBG ("Loaded " + juce::String (knownPluginList.getNumTypes()) + " plugins from cache");
        }
        else
        {
            DBG ("Failed to parse cache XML");
        }
    }
    else
    {
        DBG ("No cache file found - will need to scan");
    }

    // Map learnable on-screen controls to their MidiLearnManager paramIDs (#2/#6).
    learnableControls[&inputTrimSlider]  = "inputTrim";
    learnableControls[&loopVolumeSlider] = "loopVolume";
    learnableControls[&gateThreshSlider] = "gateThresh";
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        learnableControls[&outputFaders[i]]    = "chFader" + juce::String (i);
        learnableControls[&outputGainKnobs[i]] = "chPan"   + juce::String (i);
    }

    setWantsKeyboardFocus (true);
    setSize (640, 1080);
    startTimerHz (30);
    updateActiveIndicators();
    updateSceneButtonStates();
    updateTransportUI();
    updateStatusBar();

    // Pass knob color map to LookAndFeel
    if (auto* laf = dynamic_cast<MixerLookAndFeel*> (&getLookAndFeel()))
        laf->setKnobColorMap (&knobColorMap);

    checkAutosaveRecovery();
}

MainComponent::~MainComponent()
{
    saveAudioDeviceState();
    stopTimer();
    tapTempo.stopClock();
    shutdownAudio();
    midiLearnManager.removeListener (this);

    for (auto& dev : juce::MidiInput::getAvailableDevices())
        deviceManager.removeMidiInputDeviceCallback (dev.identifier, this);
}

//==============================================================================
void MainComponent::prepareToPlay (int blockSize, double sr)
{
    currentSampleRate = sr;
    currentBlockSize  = blockSize;

    // Pre-size real-time scratch buffers to the maximum block size so the audio
    // callback never has to allocate. setSize with keepExistingContent=false,
    // clearExtraSpace=true.
    work        .setSize (2, blockSize, false, true, false);
    masterMix   .setSize (2, blockSize, false, true, false);
    directSignal.setSize (2, blockSize, false, true, false);
    silentBuffer.setSize (2, blockSize, false, true, false);
    silentBuffer.clear();
    for (auto& b : channelOutputs)
        b.setSize (2, blockSize, false, true, false);

    inputChannel->prepare (sr, blockSize);

    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        channels[i]->prepare (sr, blockSize);
        channelFadeGain[i].reset (sr, 0.01);
        channelFadeGain[i].setCurrentAndTargetValue (i == activeChannel ? 1.0f : 0.0f);
    }

    fxBus     ->prepare (sr, blockSize);
    inputRouter.prepare (sr, blockSize);
    noiseGate  .prepare (sr, blockSize);
    metronome  .prepare (sr, blockSize);
    looper     .prepare (sr, blockSize);
    recorder   .prepare (sr, 2);

    // K-weighting biquad coefficients (ITU-R BS.1770)
    // Stage 1: high shelf ~+4dB above 1681Hz
    {
        float A  = std::pow (10.0f, 4.0f / 40.0f); // +4dB
        float w0 = 2.0f * juce::MathConstants<float>::pi * 1681.0f / (float) sr;
        float cs = std::cos (w0), sn = std::sin (w0);
        float alpha = sn / (2.0f * 0.7071f);
        float a0 = (A+1) - (A-1)*cs + 2*std::sqrt(A)*alpha;
        kShelfL.b0 = (A*((A+1)+(A-1)*cs+2*std::sqrt(A)*alpha)) / a0;
        kShelfL.b1 = (-2*A*((A-1)+(A+1)*cs)) / a0;
        kShelfL.b2 = (A*((A+1)+(A-1)*cs-2*std::sqrt(A)*alpha)) / a0;
        kShelfL.a1 = (-2*((A-1)-(A+1)*cs)) / a0;
        kShelfL.a2 = ((A+1)-(A-1)*cs-2*std::sqrt(A)*alpha) / a0;
        kShelfR = kShelfL;
        kShelfL.reset(); kShelfR.reset();
    }
    // Stage 2: high-pass at 38Hz
    {
        float w0 = 2.0f * juce::MathConstants<float>::pi * 38.0f / (float) sr;
        float cs = std::cos (w0);
        float alpha = std::sin (w0) / (2.0f * 0.5f);
        float a0 = 1 + alpha;
        kHpL.b0 = ((1+cs)/2) / a0;
        kHpL.b1 = (-(1+cs))  / a0;
        kHpL.b2 = ((1+cs)/2) / a0;
        kHpL.a1 = (-2*cs)    / a0;
        kHpL.a2 = (1-alpha)  / a0;
        kHpR = kHpL;
        kHpL.reset(); kHpR.reset();
    }
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    juce::ScopedNoDenormals noDenormals;
    auto* buffer = info.buffer;

    // Guard against an over-large or zero block (e.g. device reconfig). The
    // scratch buffers are sized to currentBlockSize in prepareToPlay().
    if (info.numSamples <= 0)
        return;
    if (info.numSamples > work.getNumSamples())
    {
        // Larger than prepared — last-resort grow (rare; avoids OOB writes).
        work        .setSize (2, info.numSamples, false, true, false);
        masterMix   .setSize (2, info.numSamples, false, true, false);
        directSignal.setSize (2, info.numSamples, false, true, false);
        silentBuffer.setSize (2, info.numSamples, false, true, false);
        for (auto& b : channelOutputs)
            b.setSize (2, info.numSamples, false, true, false);
    }

    // ---- Build working stereo buffer ---- (members; no per-block allocation)
    work.clear();

    if (inputRouter.getMode() == InputRouter::Mode::Live)
    {
        if (buffer->getNumChannels() > 0
            && currentProject.inputChannel < buffer->getNumChannels())
        {
            work.copyFrom (0, 0, *buffer, currentProject.inputChannel,
                           info.startSample, info.numSamples);
            work.copyFrom (1, 0, work, 0, 0, info.numSamples);
        }
    }
    else
    {
        inputRouter.fillNextBlock (work);
    }

    buffer->clear (info.startSample, info.numSamples);

    // ---- Input Trim ----
    {
        const float trimGain = juce::Decibels::decibelsToGain (
            static_cast<float> (inputTrimSlider.getValue()));
        work.applyGain (trimGain);
    }

    // ---- Tuner gets trimmed input ----
    tunerPanel.pushAudioData (work.getReadPointer (0), info.numSamples);

    // ---- Noise gate ----
    noiseGate.processBlock (work);

    // ---- Feed input to looper (for input capture mode) ----
    looper.feedInput (work, info.numSamples);

    // ---- Measure input level (before FX) ----
    inputLevelInL.store (work.getMagnitude (0, 0, info.numSamples), std::memory_order_relaxed);
    inputLevelInR.store (work.getMagnitude (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Input channel pre-FX ----
    juce::MidiBuffer inputMidi; // Input channel gets MIDI too
    inputChannel->processBlock (work, inputMidi);

    // ---- Measure output level (after FX) ----
    inputLevelOutL.store (work.getMagnitude (0, 0, info.numSamples), std::memory_order_relaxed);
    inputLevelOutR.store (work.getMagnitude (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Save post-input-FX signal for direct mix ----
    float directGain = inputDirectLevel.load (std::memory_order_relaxed);
    bool  haveDirect = directGain > 0.001f;
    if (haveDirect)
        for (int ch = 0; ch < 2; ++ch)
            directSignal.copyFrom (ch, 0, work, ch, 0, info.numSamples);

    // ---- Dry capture (after input FX) ----
    recorder.writeInputBlock (work);

    // ---- MIDI ----
    juce::MidiBuffer midi;
    {
        juce::ScopedLock sl (midiLock);
        midi = pendingMidi;
        pendingMidi.clear();
    }
    midiTranslator.processBuffer (midi);

    // Parse PC messages
    for (const auto meta : midi)
    {
        auto msg = meta.getMessage();
        if (msg.isProgramChange())
        {
            int pc = msg.getProgramChangeNumber();
            juce::MessageManager::callAsync ([this, pc]()
            {
                if (pc < NUM_CHANNELS)
                {
                    setActiveChannel (pc);
                }
                else if (pc < NUM_CHANNELS + NUM_SCENES)
                {
                    applySceneWithMute (pc - NUM_CHANNELS);
                }
                else if (pc == AB_PC_A)
                {
                    setActiveChannel (abChannelA);
                    abIsShowingA = true;
                    updateActiveIndicators();
                }
                else if (pc == AB_PC_B)
                {
                    setActiveChannel (abChannelB);
                    abIsShowingA = false;
                    updateActiveIndicators();
                }
                else if (pc == setlistManager.previousPCNumber)
                {
                    setlistManager.previous();
                }
                else if (pc == setlistManager.advancePCNumber)
                {
                    setlistManager.advance();
                }
            });
        }

        midiLearnManager.processMessage (meta.getMessage());
    }

    // ---- Channel processing ---- (channelOutputs/masterMix are members)
    masterMix.clear();

    if (! outputMuted)
    {
        bool anySoloed = false;
        for (int i = 0; i < NUM_CHANNELS; ++i)
            if (channelSoloed[i]) { anySoloed = true; break; }

        for (int i = 0; i < NUM_CHANNELS; ++i)
        {
            channelOutputs[i].setSize (2, info.numSamples, false, false, true);

            bool isActive = parallelRouting || (i == activeChannel);
            bool isFading = ! parallelRouting && channelFadeGain[i].isSmoothing();
            bool shouldProcess = isActive || isFading;

            if (shouldProcess)
            {
                for (int ch = 0; ch < 2; ++ch)
                    channelOutputs[i].copyFrom (ch, 0, work, ch, 0, info.numSamples);
            }
            else
            {
                channelOutputs[i].clear();
            }

            // Capture input levels (before FX)
            channelInputLevelL[i].store (channelOutputs[i].getMagnitude (0, 0, info.numSamples), std::memory_order_relaxed);
            channelInputLevelR[i].store (channelOutputs[i].getMagnitude (1, 0, info.numSamples), std::memory_order_relaxed);

            {
                juce::MidiBuffer channelMidi (midi);
                if (shouldProcess)
                {
                    channels[i]->processBlock (channelOutputs[i], channelMidi);
                }
                else
                {
                    // Drive inactive channels with silence so their plugins keep
                    // their internal state running (reusing the member buffer).
                    silentBuffer.clear();
                    channels[i]->processBlock (silentBuffer, channelMidi);
                }
            }

            // Apply crossfade in single mode
            if (! parallelRouting && shouldProcess)
            {
                for (int s = 0; s < info.numSamples; ++s)
                {
                    float g = channelFadeGain[i].getNextValue();
                    for (int ch = 0; ch < 2; ++ch)
                        channelOutputs[i].getWritePointer (ch)[s] *= g;
                }
            }

            // Solo/mute logic
            bool shouldMix = anySoloed ? channelSoloed[i] : !channelMuted[i];

            if (shouldMix && shouldProcess)
            {
                for (int ch = 0; ch < 2; ++ch)
                    masterMix.addFrom (ch, 0, channelOutputs[i], ch, 0, info.numSamples);
            }
        }

        // Mix in direct signal from input channel
        if (haveDirect)
        {
            for (int ch = 0; ch < 2; ++ch)
                masterMix.addFrom (ch, 0, directSignal, ch, 0, info.numSamples, directGain);
        }

        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, masterMix, ch, 0, info.numSamples);
    }
    else
    {
        work.clear();
    }

    // ---- Master input level (before FX bus) ----
    masterLevelInL.store (work.getMagnitude (0, 0, info.numSamples), std::memory_order_relaxed);
    masterLevelInR.store (work.getMagnitude (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Master insert chain ----
    {
        juce::MidiBuffer fxMidi (midi);
        fxBus->processBlock (work, info.numSamples, fxMidi);
    }

    // ---- Channel level meters ----
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        channelLevelL[i].store (channelOutputs[i].getMagnitude (0, 0, info.numSamples), std::memory_order_relaxed);
        channelLevelR[i].store (channelOutputs[i].getMagnitude (1, 0, info.numSamples), std::memory_order_relaxed);
    }

    // ---- Looper (captures and plays back master output) ----
    looper.processBlock (work);

    // ---- Metronome (mixed after looper so clicks aren't recorded into loops) ----
    metronome.processBlock (work);

    // ---- Wet capture ----
    recorder.writeOutputBlock (work);

    // ---- Master output gain (fader) ----
    float mGain = masterOutputGain.load (std::memory_order_relaxed);
    if (std::abs (mGain - 1.0f) > 0.0001f)
        work.applyGain (0, info.numSamples, mGain);

    // ---- Master output level (post FX + fader) ----
    masterLevelOutL.store (work.getMagnitude (0, 0, info.numSamples), std::memory_order_relaxed);
    masterLevelOutR.store (work.getMagnitude (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Stereo spread (mid/side energy) + goniometer ----
    {
        float midE = 0.0f, sideE = 0.0f;
        const float* lPtr = work.getReadPointer (0);
        const float* rPtr = work.getReadPointer (1);
        for (int s = 0; s < info.numSamples; ++s)
        {
            float mid  = (lPtr[s] + rPtr[s]) * 0.5f;
            float side = (lPtr[s] - rPtr[s]) * 0.5f;
            midE  += mid * mid;
            sideE += side * side;
        }
        masterStereoL.store (midE  / (float) info.numSamples, std::memory_order_relaxed);
        masterStereoR.store (sideE / (float) info.numSamples, std::memory_order_relaxed);

        if (fxBusPanel)
            fxBusPanel->pushGoniometerSamples (lPtr, rPtr, info.numSamples);
    }

    // ---- Short-term LUFS with K-weighting (~400ms window) ----
    {
        float sumSq = 0.0f;
        for (int s = 0; s < info.numSamples; ++s)
        {
            float l = work.getReadPointer (0)[s];
            float r = work.getReadPointer (1)[s];
            l = kHpL.process (kShelfL.process (l));
            r = kHpR.process (kShelfR.process (r));
            sumSq += l * l + r * r;
        }
        lufsAccumulator += sumSq;
        lufsSampleCount += info.numSamples;

        int windowSamples = (int)(currentSampleRate * 0.4);
        if (lufsSampleCount >= windowSamples)
        {
            float meanSq = lufsAccumulator / (float)(lufsSampleCount * 2);
            float lufs = -0.691f + 10.0f * std::log10 (juce::jmax (1e-10f, meanSq));
            masterLufsDb.store (lufs, std::memory_order_relaxed);
            lufsAccumulator = 0.0f;
            lufsSampleCount = 0;
        }
    }

    // ---- Write to ASIO output ----
    for (int ch = 0; ch < buffer->getNumChannels() && ch < 2; ++ch)
        buffer->copyFrom (ch, info.startSample, work, ch, 0, info.numSamples);
}

void MainComponent::releaseResources()
{
    for (auto& ch : channels)
        ch->releaseResources();
    fxBus->releaseResources();
    inputRouter.releaseResources();
}

//==============================================================================
juce::Optional<juce::AudioPlayHead::PositionInfo> MainComponent::getPosition() const
{
    juce::AudioPlayHead::PositionInfo info;
    info.setBpm (tapTempo.getBPM());
    info.setIsPlaying (true);
    info.setTimeInSamples (0);
    info.setTimeInSeconds (0.0);
    auto ts = juce::AudioPlayHead::TimeSignature();
    ts.numerator   = metronome.getNumerator();
    ts.denominator = metronome.getDenominator();
    info.setTimeSignature (ts);
    return info;
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*,
                                               const juce::MidiMessage& msg)
{
    midiActivityFlag.store (true, std::memory_order_relaxed);
    {
        juce::ScopedLock sl (midiLock);
        pendingMidi.addEvent (msg, 0);
    }

    if (! msg.isMidiClock() && ! msg.isActiveSense())
    {
        auto copy = msg;
        juce::MessageManager::callAsync ([this, copy]()
        {
            if (midiMonitorWindow != nullptr && midiMonitorWindow->isVisible())
                midiMonitorWindow->addMessage (copy);

            if (activeMidiRulesPanel != nullptr)
                activeMidiRulesPanel->incomingMidiMessage (copy);
        });
    }
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();

    // ---- Console surface: dark matte with subtle warm undertone ----
    {
        juce::ColourGradient consoleBg (
            juce::Colour (0xff363330), 0.0f, 0.0f,
            juce::Colour (0xff1c1a18), 0.0f, h, false);
        consoleBg.addColour (0.02, juce::Colour (0xff3a3835));
        consoleBg.addColour (0.12, juce::Colour (0xff32302d));
        consoleBg.addColour (0.5,  juce::Colour (0xff282623));
        consoleBg.addColour (0.85, juce::Colour (0xff201e1c));
        g.setGradientFill (consoleBg);
        g.fillAll();
    }

    // ---- Brushed metal noise texture ----
    {
        juce::Random rng (42);
        g.saveState();
        g.reduceClipRegion (getLocalBounds());
        for (int ty = 0; ty < getHeight(); ++ty)
        {
            float noiseAlpha = 0.03f + rng.nextFloat() * 0.025f;
            g.setColour (juce::Colours::white.withAlpha (noiseAlpha));
            g.drawHorizontalLine (ty, 0.0f, w);
        }
        g.restoreState();
    }

    // ---- Patina: aged sheet-metal discoloration, stains and wear ----
    // Fixed seed so it never flickers between repaints.
    {
        g.saveState();
        g.reduceClipRegion (getLocalBounds());
        juce::Random rng (1977);

        // Soft mottled stains — warm (oxidation) and cool (grime) blotches.
        for (int i = 0; i < 70; ++i)
        {
            float bx = rng.nextFloat() * w;
            float by = rng.nextFloat() * h;
            float br = 30.0f + rng.nextFloat() * 110.0f;
            bool warm = rng.nextBool();
            juce::Colour tint = warm ? juce::Colour (0xff5a4632)   // brownish oxidation
                                     : juce::Colour (0xff32383a);  // cool grime
            float a = 0.015f + rng.nextFloat() * 0.03f;
            juce::ColourGradient blot (tint.withAlpha (a), bx, by,
                                       tint.withAlpha (0.0f), bx + br, by + br, true);
            g.setGradientFill (blot);
            g.fillEllipse (bx - br, by - br, br * 2.0f, br * 2.0f);
        }

        // Darker worn/scuffed patches near high-traffic spots.
        for (int i = 0; i < 30; ++i)
        {
            float bx = rng.nextFloat() * w;
            float by = rng.nextFloat() * h;
            float br = 12.0f + rng.nextFloat() * 40.0f;
            float a = 0.02f + rng.nextFloat() * 0.035f;
            juce::ColourGradient dark (juce::Colours::black.withAlpha (a), bx, by,
                                       juce::Colours::transparentBlack, bx + br, by + br, true);
            g.setGradientFill (dark);
            g.fillEllipse (bx - br, by - br, br * 2.0f, br * 2.0f);
        }

        // Fine scratches — faint diagonal hairlines.
        for (int i = 0; i < 40; ++i)
        {
            float sx = rng.nextFloat() * w;
            float sy = rng.nextFloat() * h;
            float len = 8.0f + rng.nextFloat() * 50.0f;
            float ang = (rng.nextFloat() - 0.5f) * 0.6f;  // mostly horizontal
            float ex = sx + std::cos (ang) * len;
            float ey = sy + std::sin (ang) * len;
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black)
                             .withAlpha (0.025f + rng.nextFloat() * 0.025f));
            g.drawLine (sx, sy, ex, ey, 0.6f);
        }
        g.restoreState();
    }

    // ---- Radial vignette: dark edges, slightly lighter center ----
    {
        float cx = w * 0.5f;
        float cy = h * 0.45f;
        float radius = juce::jmax (w, h) * 0.75f;
        juce::ColourGradient vignette (
            juce::Colours::transparentBlack, cx, cy,
            juce::Colour (0x28000000), cx, cy + radius, true);
        g.setGradientFill (vignette);
        g.fillRect (getLocalBounds());
    }

    // ---- Top panel: darker header area ----
    {
        auto headerArea = juce::Rectangle<float> (0.0f, 0.0f, w, 104.0f);
        juce::ColourGradient headerBg (
            juce::Colour (0xff2a2826), 0.0f, 0.0f,
            juce::Colour (0xff222020), 0.0f, 104.0f, false);
        g.setGradientFill (headerBg);
        g.fillRect (headerArea);
    }

    // ---- Horizontal dividers: beveled grooves ----
    auto drawGroove = [&] (int y)
    {
        g.setColour (juce::Colour (0xff0a0908));
        g.drawHorizontalLine (y, 0.0f, w);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine (y + 1, 0.0f, w);
    };
    drawGroove (28);

    // Draws a raised drop-in module face within a dark seam — the chassis unit
    // look shared by the toolbar and scene rows.
    auto drawDropInModule = [&g, w] (juce::Rectangle<float> band)
    {
        // Recessed seam/slot the module drops into.
        g.setColour (juce::Colour (0xff0a0908));
        g.fillRoundedRectangle (band.expanded (2.0f), 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));        // shadow at slot top
        g.drawHorizontalLine ((int) band.getY() - 2, band.getX() - 1.0f, band.getRight() + 1.0f);

        // Raised module face: top-lit so it sits PROUD of the surface.
        juce::ColourGradient pg (juce::Colour (0xff353230), band.getCentreX(), band.getY(),
                                 juce::Colour (0xff211f1d), band.getCentreX(), band.getBottom(), false);
        pg.addColour (0.45, juce::Colour (0xff2b2926));
        g.setGradientFill (pg);
        g.fillRoundedRectangle (band, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));      // top bevel highlight
        g.drawHorizontalLine ((int) band.getY() + 1, band.getX() + 4.0f, band.getRight() - 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));      // left bevel
        g.drawVerticalLine ((int) band.getX() + 1, band.getY() + 4.0f, band.getBottom() - 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));      // bottom shadow edge
        g.drawHorizontalLine ((int) band.getBottom() - 1, band.getX() + 4.0f, band.getRight() - 4.0f);
        g.setColour (juce::Colour (0xff4a4640));                    // crisp frame
        g.drawRoundedRectangle (band, 4.0f, 1.0f);
    };

    // ---- Transport/toolbar row as a drop-in module (y=62..104 region) ----
    drawDropInModule (juce::Rectangle<float> (6.0f, 64.0f, w - 12.0f, 38.0f));

    // ---- Scene row: a raised horizontal drop-in module ----
    {
        // The toolbar module ends ~104 and the strip groove is at y=138. Centre
        // a 26px module in that region (108..134) so a seam shows all around.
        auto band = juce::Rectangle<float> (6.0f, 108.0f, w - 12.0f, 26.0f);
        drawDropInModule (band);

        // A screw in each end gutter, vertically centred in the band.
        auto bandScrew = [&g] (float cx, float cy)
        {
            const float rad = 5.0f;
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (cx - rad - 1.2f, cy - rad - 1.2f, (rad + 1.2f) * 2.0f, (rad + 1.2f) * 2.0f);
            juce::ColourGradient head (juce::Colour (0xff6e6a62), cx, cy - rad,
                                       juce::Colour (0xff2a2724), cx, cy + rad, false);
            head.addColour (0.5, juce::Colour (0xff4a463f));
            g.setGradientFill (head);
            g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, 0.8f);
            auto rot = juce::AffineTransform::rotation (juce::degreesToRadians (35.0f), cx, cy);
            const float s = rad * 0.72f;
            juce::Point<float> p1 (cx - s, cy), p2 (cx + s, cy);
            p1.applyTransform (rot); p2.applyTransform (rot);
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.drawLine ({ p1, p2 }, 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillEllipse (cx - rad * 0.5f, cy - rad * 0.6f, rad * 0.6f, rad * 0.6f);
        };
        float cy = band.getCentreY();
        bandScrew (band.getX() + 9.0f, cy);
        bandScrew (band.getRight() - 9.0f, cy);
    }

    // ---- Toolbar group dividers (vertical) ----
    {
        auto drawVDiv = [&] (int x, int top, int bottom)
        {
            g.setColour (juce::Colour (0xff0a0908));
            g.drawVerticalLine (x, (float) top, (float) bottom);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawVerticalLine (x + 1, (float) top, (float) bottom);
        };

        int tTop = 70, tBot = 98;   // within the toolbar module face

        // Separate the logical button groups (utility | rec/play | mod).
        int d1x = (panicButton.getRight() + recordButton.getX()) / 2;
        int d2x = (playLoopButton.getRight() + metronomeButton.getX()) / 2;

        drawVDiv (d1x, tTop, tBot);
        drawVDiv (d2x, tTop, tBot);
    }

    // ---- Channel strip backgrounds ----
    // Strips begin below the scene-button row: menu(28)+songbar(34)+
    // transport(48)+scenes(28) = 138. (Using 104 drew them up behind the scenes.)
    const int mixerTop = 138;
    const int statusBottom = getHeight() - 24;
    const int totalChannels = NUM_CHANNELS + 2;
    const int stripWidth = getWidth() / totalChannels;

    for (int i = 0; i < totalChannels; ++i)
    {
        auto stripRect = juce::Rectangle<float> ((float)(stripWidth * i), (float)mixerTop,
                                                 (float)stripWidth, (float)(statusBottom - mixerTop));

        // Alternating strip tone with warm tint
        juce::Colour baseCol = (i % 2 == 0) ? juce::Colour (0xff2a2826) : juce::Colour (0xff302e2b);

        // Top-lit gradient
        juce::ColourGradient stripGrad (
            baseCol.brighter (0.08f), stripRect.getCentreX(), stripRect.getY(),
            baseCol.darker (0.06f),   stripRect.getCentreX(), stripRect.getBottom(), false);
        stripGrad.addColour (0.3, baseCol);
        g.setGradientFill (stripGrad);
        g.fillRect (stripRect);

        // Left highlight edge
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawVerticalLine ((int) stripRect.getX(), stripRect.getY(), stripRect.getBottom());

        // Right shadow edge
        g.setColour (juce::Colours::black.withAlpha (0.1f));
        g.drawVerticalLine ((int) stripRect.getRight() - 1, stripRect.getY(), stripRect.getBottom());
    }

    // ---- Channel separators: routed grooves ----
    for (int i = 1; i < totalChannels; ++i)
    {
        float x = (float)(stripWidth * i);

        g.setColour (juce::Colour (0xff060504));
        g.drawVerticalLine ((int)x - 1, (float)mixerTop, (float)statusBottom);

        g.setColour (juce::Colour (0xff0c0b0a));
        g.drawVerticalLine ((int)x, (float)mixerTop, (float)statusBottom);

        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.drawVerticalLine ((int)x + 1, (float)mixerTop, (float)statusBottom);
    }

    // ---- Horizontal grooves framing the top and bottom of the strip grid ----
    // Complements the vertical inter-channel separators so the mixer block is
    // bounded on all sides.
    {
        auto fullGroove = [&g, w] (int gy)
        {
            g.setColour (juce::Colour (0xff060504));
            g.drawHorizontalLine (gy, 0.0f, w);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawHorizontalLine (gy + 1, 0.0f, w);
        };
        fullGroove (mixerTop);          // top of the strip grid (below scene row)
        fullGroove (statusBottom - 1);  // bottom of the strip grid (above status bar)
    }

    // ---- Mute overlays on channel strips ----
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        if (channelMuted[i])
        {
            int chStripX = stripWidth * (i + 1); // +1 because input is strip 0
            auto muteRect = juce::Rectangle<float> (
                (float) chStripX, (float) mixerTop,
                (float) stripWidth, (float)(statusBottom - mixerTop));
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (muteRect);
        }
    }

    // ---- Scribble-strip LCD backing behind each channel label (#7) ----
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        auto lb = channelLabels[i].getBounds().toFloat().reduced (1.0f);
        const bool isActive = (i == activeChannel);
        const bool isMuted  = channelMuted[i];

        // Recessed dark-LCD panel; backlight tint reflects state.
        juce::Colour top = isMuted   ? juce::Colour (0xff2a1414)
                         : isActive  ? juce::Colour (0xff1d3324)
                                     : juce::Colour (0xff161a18);
        juce::Colour bot = top.darker (0.4f);
        g.setGradientFill (juce::ColourGradient (top, lb.getX(), lb.getY(),
                                                 bot, lb.getX(), lb.getBottom(), false));
        g.fillRoundedRectangle (lb, 3.0f);

        // Subtle horizontal scanlines for the LCD feel.
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        for (float yy = lb.getY() + 1.0f; yy < lb.getBottom(); yy += 2.0f)
            g.drawHorizontalLine ((int) yy, lb.getX(), lb.getRight());

        // Recessed bevel.
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (lb, 3.0f, 1.0f);
    }

    // ---- Active-channel frame (#1): unmistakable highlight on selected strip ----
    if (juce::isPositiveAndBelow (activeChannel, NUM_CHANNELS))
    {
        auto r = channelStripBounds[activeChannel].toFloat().reduced (2.0f);
        const bool muted = channelMuted[activeChannel];
        juce::Colour accent = muted ? juce::Colour (0xffcc4444)
                                    : juce::Colour (0xff5fd06a);
        g.setColour (accent.withAlpha (0.18f));               // soft outer glow
        g.drawRoundedRectangle (r.expanded (2.0f), 6.0f, 4.0f);
        g.setColour (accent.withAlpha (0.95f));               // crisp inner frame
        g.drawRoundedRectangle (r, 6.0f, 2.0f);
    }

    // ---- Faceplate screws at the ends of each fader slot ----
    // Drawn here (parent paint) so the fader thumb, a child component, slides
    // OVER them. Positioned just inside the slot ends (slot has a 12px margin
    // inside the fader component; see drawLinearSlider).
    {
        auto drawScrew = [&g] (float cx, float cy)
        {
            const float rad = 6.0f;
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (cx - rad - 1.5f, cy - rad - 1.5f, (rad + 1.5f) * 2.0f, (rad + 1.5f) * 2.0f);
            juce::ColourGradient head (juce::Colour (0xff6e6a62), cx, cy - rad,
                                       juce::Colour (0xff2a2724), cx, cy + rad, false);
            head.addColour (0.5, juce::Colour (0xff4a463f));
            g.setGradientFill (head);
            g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, 0.8f);
            auto rot = juce::AffineTransform::rotation (juce::degreesToRadians (35.0f), cx, cy);
            const float s = rad * 0.72f;
            juce::Point<float> p1 (cx - s, cy), p2 (cx + s, cy);
            p1.applyTransform (rot); p2.applyTransform (rot);
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.drawLine ({ p1, p2 }, 1.6f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillEllipse (cx - rad * 0.5f, cy - rad * 0.6f, rad * 0.6f, rad * 0.6f);
        };

        const float slotMargin = 12.0f;  // matches drawLinearSlider
        auto screwsForFader = [&] (const juce::Component& fader)
        {
            auto b = fader.getBounds().toFloat();
            float cx = b.getCentreX();
            drawScrew (cx, b.getY() + slotMargin);       // top end of slot
            drawScrew (cx, b.getBottom() - slotMargin);  // bottom end of slot
        };

        screwsForFader (inputStripFader);
        for (int i = 0; i < NUM_CHANNELS; ++i)
            screwsForFader (outputFaders[i]);
    }

    // ---- Divider separating the channel block from the master section (#8) ----
    if (fxBusPanel != nullptr)
    {
        auto mb = fxBusPanel->getBounds();
        int x = mb.getX() - 2;
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawVerticalLine (x, (float) mb.getY(), (float) mb.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine (x + 1, (float) mb.getY(), (float) mb.getBottom());
    }

    // ---- MIDI-learn state (#6): badge on bound controls; pulse on armed one ----
    for (const auto& kv : learnableControls)
    {
        auto* comp = kv.first;
        if (comp == nullptr || ! comp->isVisible()) continue;
        auto area = getLocalArea (comp, comp->getLocalBounds()).toFloat();

        const bool armed = midiLearnManager.isLearning()
                         && midiLearnManager.getLearningParam() == kv.second;
        if (armed)
        {
            g.setColour (juce::Colour (0xffffaa00).withAlpha (0.85f));
            g.drawRoundedRectangle (area.reduced (1.0f), 4.0f, 2.0f);
        }
        else if (midiLearnManager.getCcForParam (kv.second) >= 0)
        {
            g.setColour (juce::Colour (0xff3a78ff).withAlpha (0.9f));  // blue "bound" dot
            g.fillEllipse (area.getX() + 2.0f, area.getY() + 2.0f, 5.0f, 5.0f);
        }
    }

    // ---- Right-click affordance hint (#4): subtle 3-dot marker on scene buttons ----
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        auto b = sceneButtons[i].getBounds().toFloat();
        float x = b.getRight() - 6.0f;
        float y = b.getY() + 3.0f;
        for (int d = 0; d < 3; ++d)
            g.fillEllipse (x, y + d * 2.2f, 1.4f, 1.4f);  // vertical 3-dot
    }

    // ---- Scene-save hold countdown (#5): radial fill over the held button ----
    if (heldSceneIndex >= 0 && heldSceneIndex < NUM_SCENES && heldSceneProgress > 0.0f)
    {
        auto b = sceneButtons[heldSceneIndex].getBounds().toFloat();
        auto centre = b.getCentre();
        float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
        juce::Path arc;
        arc.addPieSegment (centre.x - radius, centre.y - radius,
                           radius * 2.0f, radius * 2.0f,
                           0.0f,
                           juce::MathConstants<float>::twoPi * heldSceneProgress,
                           0.0f);
        g.setColour (juce::Colour (0xff88ff88).withAlpha (0.55f));
        g.fillPath (arc);
    }

    // ---- Status bar: recessed trough ----
    {
        auto statusRect = juce::Rectangle<float> (0.0f, (float)statusBottom, w, 24.0f);
        juce::ColourGradient statusBg (
            juce::Colour (0xff181614), 0.0f, (float)statusBottom,
            juce::Colour (0xff1e1c1a), 0.0f, (float)statusBottom + 24.0f, false);
        g.setGradientFill (statusBg);
        g.fillRect (statusRect);

        g.setColour (juce::Colour (0xff080706));
        g.drawHorizontalLine (statusBottom, 0.0f, w);
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.drawHorizontalLine (statusBottom + 1, 0.0f, w);
    }
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    // Check if click was on a channel label
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        if (e.eventComponent == &channelLabels[i])
        {
            if (e.mods.isRightButtonDown())
                showChannelRenameDialog (i);   // right-click: rename
            else
                setActiveChannel (i);          // left-click: select
            return;
        }
    }

    // Check if right-click on scene button (save scene)
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        if (e.eventComponent == &sceneButtons[i] && e.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            bool used = sceneManager.isSceneUsed (i);
            menu.addItem (1, "Save Here");
            menu.addItem (2, "Rename...", used);
            menu.addSeparator();
            menu.addItem (3, "Clear", used);

            menu.showMenuAsync ({}, [this, i, used] (int result)
            {
                if (result == 1)
                {
                    captureSceneFromCurrent (i);
                }
                else if (result == 2)
                {
                    auto currentName = sceneManager.getSceneName (i);
                    auto* aw = new juce::AlertWindow ("Rename Scene",
                                                       "Enter a name for scene " + juce::String (i + 1) + ":",
                                                       juce::AlertWindow::NoIcon);
                    aw->addTextEditor ("name", currentName, "Name:");
                    aw->addButton ("OK", 1);
                    aw->addButton ("Cancel", 0);
                    aw->enterModalState (true, juce::ModalCallbackFunction::create (
                        [this, i, aw] (int res)
                        {
                            if (res == 1)
                            {
                                auto newName = aw->getTextEditorContents ("name").trim();
                                if (newName.isNotEmpty())
                                {
                                    sceneManager.setSceneName (i, newName);
                                    updateSceneButtonStates();
                                    projectDirty = true;
                                }
                            }
                            delete aw;
                        }), true);
                }
                else if (result == 3)
                {
                    sceneManager.clearScene (i);
                    updateSceneButtonStates();
                    projectDirty = true;
                }
            });
            return;
        }
    }

    // Right-click faders to change color
    if (e.mods.isRightButtonDown())
    {
        for (int i = 0; i < NUM_CHANNELS; ++i)
        {
            if (e.eventComponent == &outputFaders[i])
            {
                showKnobColorMenu (e.eventComponent);
                return;
            }
        }
    }

    // Edit mode: right-click knobs to change color (color menu also offers
    // Learn MIDI for learnable knobs via showKnobColorMenu).
    if (uiEditMode && e.mods.isRightButtonDown())
    {
        for (int i = 0; i < NUM_CHANNELS; ++i)
        {
            if (e.eventComponent == &outputGainKnobs[i] || e.eventComponent == &inputTrimKnobs[i])
            {
                showKnobColorMenu (e.eventComponent);
                return;
            }
        }
    }

    // Right-click any other registered learnable control → Learn / Clear MIDI (#6).
    if (e.mods.isRightButtonDown())
    {
        const juce::String pid = paramIdForComponent (e.eventComponent);
        if (pid.isNotEmpty())
        {
            const bool armed = midiLearnManager.isLearning()
                             && midiLearnManager.getLearningParam() == pid;
            juce::PopupMenu m;
            m.addItem (1, armed ? "Listening for CC..." : "Learn MIDI");
            m.addItem (2, "Clear MIDI binding", midiLearnManager.getCcForParam (pid) >= 0);
            m.showMenuAsync ({}, [this, pid] (int r)
            {
                if (r == 1) { midiLearnManager.beginLearning (pid); repaint(); }
                else if (r == 2) { midiLearnManager.clearBinding (pid); repaint(); }
            });
            return;
        }
    }

    // Click on MIDI LED — toggle MIDI monitor window
    if (e.eventComponent == &midiLedLabel)
    {
        if (midiMonitorWindow == nullptr)
            midiMonitorWindow = std::make_unique<MidiMonitorWindow>();

        midiMonitorWindow->setVisible (! midiMonitorWindow->isVisible());
        if (midiMonitorWindow->isVisible())
            midiMonitorWindow->toFront (true);
        return;
    }
}

void MainComponent::mouseDoubleClick (const juce::MouseEvent& /*e*/)
{
    // Looper overdub used to live on a hidden double-click; it now has its own
    // OVERDUB footswitch in the Loop Station window.
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        if (e.eventComponent == &channelLabels[i] && e.getDistanceFromDragStart() > 8)
        {
            if (! isDragAndDropActive())
            {
                dragSourceChannel = i;
                startDragging ("channel_" + juce::String (i), &channelLabels[i]);
            }
            return;
        }
    }
}

bool MainComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith ("channel_");
}

void MainComponent::itemDropped (const SourceDetails& details)
{
    if (dragSourceChannel < 0) return;

    // Find which channel label the drop landed on
    auto pos = details.localPosition;
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        if (channelLabels[i].getBounds().contains (pos.roundToInt()))
        {
            if (i != dragSourceChannel)
                swapChannels (dragSourceChannel, i);
            break;
        }
    }
    dragSourceChannel = -1;
}

void MainComponent::swapChannels (int a, int b)
{
    if (a == b || ! juce::isPositiveAndBelow (a, NUM_CHANNELS)
               || ! juce::isPositiveAndBelow (b, NUM_CHANNELS))
        return;

    // Swap channel strip pointers
    channels[a].swap (channels[b]);

    // Swap channel strip panels
    channelStripPanels[a].swap (channelStripPanels[b]);

    // Swap level meter pointers
    levelMeters[a].swap (levelMeters[b]);
    inputLevelMeters[a].swap (inputLevelMeters[b]);

    // Swap fader values
    double faderA = outputFaders[a].getValue();
    double faderB = outputFaders[b].getValue();
    outputFaders[a].setValue (faderB, juce::dontSendNotification);
    outputFaders[b].setValue (faderA, juce::dontSendNotification);

    // Swap knob values
    double outKnobA = outputGainKnobs[a].getValue();
    double outKnobB = outputGainKnobs[b].getValue();
    outputGainKnobs[a].setValue (outKnobB, juce::dontSendNotification);
    outputGainKnobs[b].setValue (outKnobA, juce::dontSendNotification);

    double inKnobA = inputTrimKnobs[a].getValue();
    double inKnobB = inputTrimKnobs[b].getValue();
    inputTrimKnobs[a].setValue (inKnobB, juce::dontSendNotification);
    inputTrimKnobs[b].setValue (inKnobA, juce::dontSendNotification);

    // Swap channel names
    auto nameA = channelLabels[a].getText();
    auto nameB = channelLabels[b].getText();
    channelLabels[a].setText (nameB, juce::dontSendNotification);
    channelLabels[b].setText (nameA, juce::dontSendNotification);

    // Swap mute/solo state
    std::swap (channelMuted[a], channelMuted[b]);
    std::swap (channelSoloed[a], channelSoloed[b]);
    soloButtons[a].setToggleState (channelSoloed[a], juce::dontSendNotification);
    soloButtons[b].setToggleState (channelSoloed[b], juce::dontSendNotification);
    muteButtons[a].setToggleState (channelMuted[a], juce::dontSendNotification);
    muteButtons[b].setToggleState (channelMuted[b], juce::dontSendNotification);

    // Update active channel tracking
    if (activeChannel == a) activeChannel = b;
    else if (activeChannel == b) activeChannel = a;

    // Refresh UI
    channelStripPanels[a]->refresh();
    channelStripPanels[b]->refresh();
    updateFaderLabel (a);
    updateFaderLabel (b);
    updateActiveIndicators();
    projectDirty = true;
    resized();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Ctrl+Shift+S = Save as Song
    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'S')
    {
        saveSongState();
        return true;
    }

    // Ctrl+S = Save
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
    {
        saveProject();
        return true;
    }

    // Ctrl+O = Open
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O')
    {
        openProject();
        return true;
    }

    // Ctrl+N = New
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'N')
    {
        newProject();
        return true;
    }

    // Ctrl+Z = Undo (disabled — only fader drags are undoable, incomplete)
    // TODO: expand undo to cover all mutations before re-enabling

    // 1-4 = Switch channel (main keyboard)
    if (key.getKeyCode() >= '1' && key.getKeyCode() <= '4' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        setActiveChannel (key.getKeyCode() - '1');
        return true;
    }

    // Numpad 0 = Tap tempo
    if (key.getKeyCode() == juce::KeyPress::numberPad0)
    {
        double bpm = tapTempo.tap();
        if (bpm > 0.0)
            bpmLabel.setText (juce::String ((int) bpm) + " BPM", juce::dontSendNotification);
        return true;
    }

    // Numpad 9 = prefix key for channel switching
    if (key.getKeyCode() == juce::KeyPress::numberPad9)
    {
        numpad9Prefix = true;
        return true;
    }

    // Numpad 1-8: after Numpad 9 prefix, 1-4 switches channel;
    // otherwise begin hold detection (scene recall on release, save on 3s hold)
    for (int n = 1; n <= 8; ++n)
    {
        if (key.getKeyCode() == juce::KeyPress::numberPad0 + n)
        {
            if (numpad9Prefix)
            {
                numpad9Prefix = false;
                if (n >= 1 && n <= NUM_CHANNELS)
                    setActiveChannel (n - 1);
            }
            else if (heldSceneIndex < 0)
            {
                heldSceneIndex = n - 1;
                holdStartMs = juce::Time::getMillisecondCounter();
            }
            return true;
        }
    }

    // Numpad + / - for setlist navigation
    if (key.getKeyCode() == juce::KeyPress::numberPadAdd)
    {
        setlistManager.advance();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::numberPadSubtract)
    {
        setlistManager.previous();
        return true;
    }

    return false;
}

bool MainComponent::keyStateChanged (bool /*isKeyDown*/)
{
    return false;
}

void MainComponent::showKnobColorMenu (juce::Component* knob)
{
    juce::PopupMenu menu;
    juce::PopupMenu colourMenu;
    colourMenu.addItem (1, "Grey");
    colourMenu.addItem (2, "Blue");
    colourMenu.addItem (3, "Green");
    colourMenu.addItem (4, "Purple");
    colourMenu.addItem (5, "Red");
    colourMenu.addItem (6, "Royal Blue");
    colourMenu.addItem (7, "Teal");
    menu.addSubMenu ("Colour", colourMenu);

    // If this control is MIDI-learnable, offer learn/clear here too (#6).
    const juce::String pid = paramIdForComponent (knob);
    if (pid.isNotEmpty())
    {
        const bool armed = midiLearnManager.isLearning()
                         && midiLearnManager.getLearningParam() == pid;
        menu.addSeparator();
        menu.addItem (100, armed ? "Listening for CC..." : "Learn MIDI");
        menu.addItem (101, "Clear MIDI binding", midiLearnManager.getCcForParam (pid) >= 0);
    }

    menu.showMenuAsync ({}, [this, knob, pid] (int result)
    {
        if (result <= 0) return;

        if (result == 100) { midiLearnManager.beginLearning (pid); repaint(); return; }
        if (result == 101) { midiLearnManager.clearBinding  (pid); repaint(); return; }

        juce::String colorName;
        switch (result)
        {
            case 1: colorName = "grey"; break;
            case 2: colorName = "blue"; break;
            case 3: colorName = "green"; break;
            case 4: colorName = "purple"; break;
            case 5: colorName = "red"; break;
            case 6: colorName = "royal_blue"; break;
            case 7: colorName = "teal"; break;
        }

        knobColorMap[knob->getComponentID()] = colorName;
        knob->repaint();
        projectDirty = true;
    });
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Menu bar
    menuBar.setBounds (area.removeFromTop (28));

    // Song bar (setlist navigation strip)
    if (songBar)
        songBar->setBounds (area.removeFromTop (34));

    // Transport row — sits inside the drop-in module painted at y=64..102.
    auto transport = area.removeFromTop (48);
    transport.reduce (12, 8);   // keep buttons within the module face

    int bw = toolbarLabelsVisible ? 55 : 36;
    int bwNarrow = toolbarLabelsVisible ? 45 : 36;

    // Expand toggle and help - far left
    toolbarExpandButton.setBounds (transport.removeFromLeft (24));
    transport.removeFromLeft (2);
    helpButton.setBounds (transport.removeFromLeft (24));
    transport.removeFromLeft (5);

    // Tuner and Panic
    tunerButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (3);
    panicButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (8);

    // Transport: Rec (recorder) | Play (cassette deck)
    recordButton.setBounds (transport.removeFromLeft (bwNarrow));
    transport.removeFromLeft (3);
    playLoopButton.setBounds (transport.removeFromLeft (bwNarrow));
    transport.removeFromLeft (8);

    // Metronome, Looper, and Routing
    metronomeButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (3);
    loopRecButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (1);
    looperProgressLabel.setBounds (transport.removeFromLeft (58).reduced (0, 4));
    transport.removeFromLeft (3);
    routingModeButton.setBounds (transport.removeFromLeft (78));  // wide enough for "PARALLEL"

    // Tap tempo - far right
    tapButton.setBounds (transport.removeFromRight (bwNarrow));
    transport.removeFromRight (3);
    bpmLabel.setBounds (transport.removeFromRight (65));
    transport.removeFromRight (3);
    midiLedLabel.setBounds (transport.removeFromRight (32).reduced (0, 6));

    // Scenes row — a recessed panel band with side gutters (for end screws)
    // and even top/bottom padding so the buttons sit centred. Panel + screws
    // are drawn in paint() using these same insets.
    auto sceneBand = area.removeFromTop (28);            // reserves y=110..138
    // Buttons sit inside the raised module face (painted at y=108..134); inset
    // to 111..131 so the module's bevel shows around them.
    auto scenesRow = juce::Rectangle<int> (sceneBand.getX(), 111,
                                           sceneBand.getWidth(), 20);
    scenesRow.removeFromLeft (20);                       // left gutter for screw
    scenesRow.removeFromRight (20);                      // right gutter for screw
    int sceneWidth = (scenesRow.getWidth() - (NUM_SCENES - 1) * 3) / NUM_SCENES;
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        sceneButtons[i].setBounds (scenesRow.removeFromLeft (sceneWidth));
        if (i < NUM_SCENES - 1)
            scenesRow.removeFromLeft (3);
    }

    // Footer status bar
    auto statusArea = area.removeFromBottom (22);
    ramLabel.setBounds (statusArea.removeFromRight (90).reduced (4, 2));
    cpuLabel.setBounds (statusArea.removeFromRight (90).reduced (4, 2));
    statusStateLabel.setBounds (statusArea.removeFromRight (200).reduced (4, 2));
    statusLabel.setBounds (statusArea.reduced (8, 2));

    // Tuner overlay (covers main area)
    if (tunerPanel.isVisible())
    {
        tunerPanel.setBounds (area.reduced (20));
        return;
    }

    // MIXER-STYLE CHANNEL STRIPS - equal width: INPUT + 4 channels + MASTER
    const int totalChannels = NUM_CHANNELS + 2; // +1 for input, +1 for master
    const int stripWidth = area.getWidth() / totalChannels;
    const int stripPadding = 4;

    // Input channel on left - full strip like regular channels
    {
        auto strip = area.removeFromLeft (stripWidth).reduced (stripPadding);

        inputChannelLabel.setBounds (strip.removeFromTop (28));
        strip.removeFromTop (2);

        auto pluginArea = strip.removeFromTop (230);
        inputChannelPanel->setBounds (pluginArea);
        addInputPluginButton.setVisible (false);
        strip.removeFromTop (4);

        // Direct knob (centred)
        auto knobsRow = strip.removeFromTop (65);
        auto directArea = knobsRow.reduced (2);
        inputDirectLabel.setBounds (directArea.removeFromTop (12));
        inputDirectKnob.setBounds (directArea.withSizeKeepingCentre (
            juce::jmin (52, directArea.getWidth()), juce::jmin (52, directArea.getHeight())));
        strip.removeFromTop (2);

        // Fader dB label
        inputStripFaderLabel.setBounds (strip.removeFromTop (18));
        strip.removeFromTop (2);

        // Meters + fader
        auto metersRow = strip.removeFromTop (strip.getHeight() - 12);
        auto inMeterCol = metersRow.removeFromLeft (14);
        inputChannelMeterIn->setBounds (inMeterCol);
        metersRow.removeFromLeft (1);
        auto outMeterCol = metersRow.removeFromRight (14);
        inputChannelMeterOut->setBounds (outMeterCol);
        metersRow.removeFromRight (1);
        inputStripFader.setBounds (metersRow);
    }

    // FX bus panel (master) on right side - same width as other channels
    auto fxArea = area.removeFromRight (stripWidth);
    if (fxBusPanel)
        fxBusPanel->setBounds (fxArea);

    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        auto fullStrip = area.removeFromLeft (stripWidth);
        channelStripBounds[i] = fullStrip;   // exact column this strip occupies (#1/#7)
        auto strip = fullStrip.reduced (stripPadding);

        // Channel label at top
        channelLabels[i].setBounds (strip.removeFromTop (28));
        strip.removeFromTop (2);

        // Plugin chain panel at top (slots handle add-plugin clicks)
        auto pluginArea = strip.removeFromTop (230);
        channelStripPanels[i]->setBounds (pluginArea);
        addPluginButtons[i].setVisible (false);
        strip.removeFromTop (4);

        // Knobs row - horizontal: IN on left, OUT on right
        auto knobsRow = strip.removeFromTop (65);

        auto inKnobArea = knobsRow.removeFromLeft (knobsRow.getWidth() / 2).reduced (2);
        inputTrimLabels[i].setBounds (inKnobArea.removeFromTop (12));
        inputTrimKnobs[i].setBounds (inKnobArea.withSizeKeepingCentre (
            juce::jmin (52, inKnobArea.getWidth()), juce::jmin (52, inKnobArea.getHeight())));

        auto outKnobArea = knobsRow.reduced (2);
        outputGainLabels[i].setBounds (outKnobArea.removeFromTop (12));
        outputGainKnobs[i].setBounds (outKnobArea.withSizeKeepingCentre (
            juce::jmin (52, outKnobArea.getWidth()), juce::jmin (52, outKnobArea.getHeight())));

        strip.removeFromTop (2);

        // Fader level label - recessed panel above meters
        faderLevelLabels[i].setBounds (strip.removeFromTop (18));
        strip.removeFromTop (2);

        // Meter, fader, and level display
        auto metersRow = strip.removeFromTop (strip.getHeight() - 38);

        // Input meter on left with label below
        auto inMeterCol = metersRow.removeFromLeft (14);
        inputLevelMeters[i]->setBounds (inMeterCol);
        metersRow.removeFromLeft (1);

        // Output meter on right with label below
        auto outMeterCol = metersRow.removeFromRight (14);
        levelMeters[i]->setBounds (outMeterCol);
        metersRow.removeFromRight (1);

        // Output fader in the middle
        outputFaders[i].setBounds (metersRow);

        // Meter labels below meters
        auto labelRow = strip.removeFromTop (12);
        inMeterLabels[i].setBounds (labelRow.getX(), labelRow.getY(), 18, 12);
        outMeterLabels[i].setBounds (labelRow.getRight() - 22, labelRow.getY(), 22, 12);

        // Solo/Mute buttons at bottom
        auto buttonRow = strip.removeFromTop (24);
        soloButtons[i].setBounds (buttonRow.removeFromLeft (buttonRow.getWidth() / 2).reduced (2));
        muteButtons[i].setBounds (buttonRow.reduced (2));
    }
}

//==============================================================================
void MainComponent::buttonClicked (juce::Button* b)
{
    // Input source
    if (b == &liveInputButton) { inputRouter.setMode (InputRouter::Mode::Live); updateTransportUI(); }
    else if (b == &loopInputButton) { inputRouter.setMode (InputRouter::Mode::Loop); updateTransportUI(); }
    else if (b == &loadLoopButton)
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Load Loop File", juce::File{}, "*.wav;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    if (inputRouter.loadLoopFile (result))
                    {
                        loopFileLabel.setText (result.getFileName(), juce::dontSendNotification);
                        inputRouter.setMode (InputRouter::Mode::Loop);
                        updateTransportUI();
                    }
                }
            });
    }
    else if (b == &playLoopButton)
    {
        // Opens the cassette deck (loop-file player). The icon lights while the
        // tape is rolling; transport lives inside the deck window.
        if (cassetteDeckWindow == nullptr)
            cassetteDeckWindow = std::make_unique<CassetteDeckWindow> (inputRouter);
        cassetteDeckWindow->toggleVisible();
    }

    // Tap tempo
    else if (b == &tapButton)
    {
        double bpm = tapTempo.tap();
        if (bpm > 0.0)
            bpmLabel.setText (juce::String ((int) bpm) + " BPM", juce::dontSendNotification);
    }
    else if (b == &clockToggle)
    {
        if (clockToggle.getToggleState()) tapTempo.startClock (midiOutput.get());
        else tapTempo.stopClock();
    }

    // Tuner
    else if (b == &tunerButton)
    {
        bool showing = tunerPanel.isVisible();
        tunerPanel.tunerActive = ! showing;
        outputMuted = ! showing;
        tunerPanel.setVisible (! showing);
        tunerButton.setToggleState (! showing, juce::dontSendNotification);  // lit while active
        resized();
    }

    // Panic
    else if (b == &panicButton) { sendMidiPanic(); }

    // Gate toggle
    else if (b == &gateToggle) { noiseGate.enabled = gateToggle.getToggleState(); }

    // Record — opens the Reel Recorder window (capture mode, folder, REC/STOP inside).
    else if (b == &recordButton)
    {
        if (reelRecorderWindow == nullptr)
            reelRecorderWindow = std::make_unique<ReelRecorderWindow> (recorder, [this]
            {
                if (currentProject.recordOutputFolder.isNotEmpty())
                    return juce::File (currentProject.recordOutputFolder);
                return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile ("UpStage Recordings");
            });
        reelRecorderWindow->toggleVisible();
    }

    // Metronome — opens the Metronome window (START/STOP + all settings inside).
    else if (b == &metronomeButton)
    {
        if (metronomeWindow == nullptr)
            metronomeWindow = std::make_unique<MetronomeWindow> (metronome,
                [this] (double bpm) { tapTempo.setBPM (bpm); });
        metronomeWindow->toggleVisible();
    }

    // Looper — opens the Loop Station window (discrete Rec/Overdub/Play/Clear).
    else if (b == &loopRecButton)
    {
        if (loopStationWindow == nullptr)
            loopStationWindow = std::make_unique<LoopStationWindow> (
                looper, [this] { return tapTempo.getBPM(); });
        loopStationWindow->toggleVisible();
    }

    // Routing mode
    else if (b == &routingModeButton)
    {
        parallelRouting = ! parallelRouting;
        updateRoutingModeButton();
    }

    // Toolbar expand
    else if (b == &toolbarExpandButton)
    {
        toolbarLabelsVisible = ! toolbarLabelsVisible;
        if (auto* laf = dynamic_cast<MixerLookAndFeel*> (&getLookAndFeel()))
            laf->showButtonLabels = toolbarLabelsVisible;
        resized();
        repaint();
    }

    // ASIO / Setlist / Help
    else if (b == &asioButton)    { openAsioSettings(); }
    else if (b == &setlistButton) { showSetlistPanel(); }
    else if (b == &helpButton)    { showShortcutHelp(); }

    // Channel strip controls
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        if (b == &addPluginButtons[i]) { showAddPluginMenu (i); return; }
    }

    // Scene recall / capture
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        if (b == &sceneButtons[i])
        {
            applySceneWithMute (i);
            return;
        }
        if (b == &saveSceneButtons[i])
        {
            captureSceneFromCurrent (i);
            return;
        }
    }

    // Solo/Mute buttons
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        if (b == &soloButtons[i])
        {
            channelSoloed[i] = soloButtons[i].getToggleState();
            return;
        }
        if (b == &muteButtons[i])
        {
            channelMuted[i] = muteButtons[i].getToggleState();
            updateActiveIndicators();
            return;
        }
    }
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Update input channel meters
    inputChannelMeterIn->pushLevel (inputLevelInL.load(), inputLevelInR.load());
    inputChannelMeterOut->pushLevel (inputLevelOutL.load(), inputLevelOutR.load());

    // MIDI-learn (#6): keep the armed-control pulse animating, and refresh
    // CC tooltips a few times per second (cheap, reflects new bindings).
    if (midiLearnManager.isLearning())
        repaint();
    if (++midiBadgeTick >= 15)
    {
        midiBadgeTick = 0;
        for (const auto& kv : learnableControls)
        {
            int cc = midiLearnManager.getCcForParam (kv.second);
            if (cc < 0) continue;  // leave the control's own descriptive tooltip intact
            if (auto* s = dynamic_cast<juce::SettableTooltipClient*> (kv.first))
                s->setTooltip ("MIDI: CC " + juce::String (cc) + " ch "
                    + juce::String (midiLearnManager.getChannelForParam (kv.second)));
        }
    }

    // Update regular channel meters
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        levelMeters[i]->pushLevel (channelLevelL[i].load(), channelLevelR[i].load());
        inputLevelMeters[i]->pushLevel (channelInputLevelL[i].load(), channelInputLevelR[i].load());
    }

    // Update master bus meters
    if (fxBusPanel)
    {
        fxBusPanel->pushMeterLevels (masterLevelInL.load(), masterLevelInR.load(),
                                     masterLevelOutL.load(), masterLevelOutR.load());
        fxBusPanel->pushStereo (masterStereoL.load(), masterStereoR.load());
        fxBusPanel->pushLufs (masterLufsDb.load());
    }


    // Metronome: light the toolbar icon while running. The per-beat pendulum
    // animation lives in the Metronome window (it owns consumeBeatFlash()).
    if (metronomeButton.getToggleState() != metronome.isEnabled())
        metronomeButton.setToggleState (metronome.isEnabled(), juce::dontSendNotification);

    // Cassette deck: light the play icon while the tape is rolling.
    if (playLoopButton.getToggleState() != inputRouter.isLoopPlaying())
        playLoopButton.setToggleState (inputRouter.isLoopPlaying(), juce::dontSendNotification);

    // Reel recorder: light the record icon red while recording to disk.
    if (recordButton.getToggleState() != recorder.isRecording())
        recordButton.setToggleState (recorder.isRecording(), juce::dontSendNotification);

    // Scene save flash
    if (sceneFlashCounter > 0)
    {
        if (--sceneFlashCounter == 0)
        {
            sceneFlashIndex = -1;
            updateSceneButtonStates();
        }
    }

    // Hold-to-save: save immediately at 3s threshold, recall on early release
    if (heldSceneIndex >= 0)
    {
        int heldKeyCode = juce::KeyPress::numberPad0 + heldSceneIndex + 1;
        auto heldMs = juce::Time::getMillisecondCounter() - holdStartMs;

        heldSceneProgress = juce::jlimit (0.0f, 1.0f, (float) heldMs / 3000.0f);
        repaint();  // animate the countdown arc (#5)

        if (heldMs >= 3000)
        {
            int sceneIdx = heldSceneIndex;
            heldSceneIndex = -1;
            heldSceneProgress = 0.0f;
            captureSceneFromCurrent (sceneIdx);
            flashSceneButton (sceneIdx);
        }
        else if (! juce::KeyPress::isKeyCurrentlyDown (heldKeyCode))
        {
            int sceneIdx = heldSceneIndex;
            heldSceneIndex = -1;
            heldSceneProgress = 0.0f;
            applySceneWithMute (sceneIdx);
        }
    }

    // Looper visual feedback
    {
        auto ls = looper.getState();
        double bpm = tapTempo.getBPM();
        int meterNum = looper.getMeterNum();
        auto secsToBarBeat = [&] (double secs) -> juce::String
        {
            if (bpm <= 0.0) return juce::String (secs, 1) + "s";
            double totalBeats = secs * bpm / 60.0;
            int bar  = (int)(totalBeats / meterNum) + 1;
            int beat = (int)(std::fmod (totalBeats, (double) meterNum)) + 1;
            return juce::String (bar) + "." + juce::String (beat);
        };

        // The toolbar icon shows active/inactive (lit via toggle state) and the
        // backlit colour reflects the current state; the bar.beat readout lives
        // in the adjacent progress label. Full controls are in the Loop Station.
        juce::Colour litColour = juce::Colour (0xff228822);  // playing = green

        if (ls == Looper::State::Idle)
        {
            looperProgressLabel.setText ("", juce::dontSendNotification);
            looperProgressLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x00000000));
        }
        else if (ls == Looper::State::CountIn)
        {
            double secs = -looper.getElapsedSeconds();
            double beatsLeft = secs * bpm / 60.0;
            int beatsInt = (int) std::ceil (beatsLeft);
            looperProgressLabel.setText ("-" + juce::String (beatsInt), juce::dontSendNotification);
            bool flash = (looperFlashCounter++ / 4) % 2 == 0;
            looperProgressLabel.setColour (juce::Label::textColourId,
                flash ? juce::Colour (0xffffcc44) : juce::Colour (0xffaa8822));
            looperProgressLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x30ffaa00));
            litColour = juce::Colour (0xff886622);  // count-in = amber
        }
        else if (ls == Looper::State::Recording)
        {
            double elapsed = looper.getElapsedSeconds();
            looperProgressLabel.setText (secsToBarBeat (elapsed), juce::dontSendNotification);
            bool flash = (looperFlashCounter++ / 4) % 2 == 0;
            looperProgressLabel.setColour (juce::Label::textColourId,
                flash ? juce::Colour (0xffff4444) : juce::Colour (0xffaa2222));
            looperProgressLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x30ff0000));
            litColour = juce::Colour (0xffcc2222);  // recording = red
        }
        else
        {
            double pos = looper.getPositionNormalised();
            double lenSecs = looper.getLoopLengthSeconds();
            double posSecs = pos * lenSecs;
            juce::String posStr = secsToBarBeat (posSecs) + "/" + secsToBarBeat (lenSecs);
            looperProgressLabel.setText (posStr, juce::dontSendNotification);
            bool isDubbing  = (ls == Looper::State::Overdubbing);
            bool isPending  = (ls == Looper::State::OverdubPending);
            looperProgressLabel.setColour (juce::Label::textColourId,
                isDubbing ? juce::Colour (0xffccaa44)
                : isPending ? juce::Colour (0xffaaaa44)
                : juce::Colour (0xff88cc88));
            looperProgressLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x00000000));
            litColour = isDubbing ? juce::Colour (0xffccaa44)
                      : isPending ? juce::Colour (0xff888822)
                      : juce::Colour (0xff228822);
        }

        // Light the toolbar loop icon whenever the looper is active, tinted by state.
        loopRecButton.setColour (juce::TextButton::buttonOnColourId, litColour);
        bool looperActive = (ls != Looper::State::Idle);
        if (loopRecButton.getToggleState() != looperActive)
            loopRecButton.setToggleState (looperActive, juce::dontSendNotification);
    }

    // MIDI activity LED
    if (midiActivityFlag.exchange (false, std::memory_order_relaxed))
    {
        midiLedLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00ff88));
        midiLedLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0a3a1a));
        midiFlashCounter = 3;
    }
    else if (midiFlashCounter > 0)
    {
        if (--midiFlashCounter == 0)
        {
            midiLedLabel.setColour (juce::Label::textColourId, juce::Colour (0xff444444));
            midiLedLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1a1a1a));
        }
    }

    // Re-layout if FX bus panel height changed
    if (fxBusPanel)
    {
        static int lastFxBusH = 0;
        int curH = fxBusPanel->getPreferredHeight();
        if (curH != lastFxBusH) { lastFxBusH = curH; resized(); }
    }

    // Signal chain view

    // Autosave (~60s at 30Hz timer)
    if (++autosaveCounter >= 1800)
    {
        autosaveCounter = 0;
        if (projectDirty)
            autosave();
    }

    // CPU and RAM monitoring (update every ~0.5s)
    static int perfCounter = 0;
    if (++perfCounter >= 15)
    {
        perfCounter = 0;
        double cpuUsage = deviceManager.getCpuUsage() * 100.0;
        cpuLabel.setText ("CPU: " + juce::String (cpuUsage, 1) + "%", juce::dontSendNotification);
        cpuLabel.setColour (juce::Label::textColourId,
            cpuUsage > 80.0 ? juce::Colour (0xffcc4444)
            : cpuUsage > 50.0 ? juce::Colour (0xffccaa44)
            : juce::Colour (0xff88aa88));

        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc)))
        {
            double mb = (double) pmc.WorkingSetSize / (1024.0 * 1024.0);
            ramLabel.setText ("RAM: " + juce::String (mb, 0) + " MB", juce::dontSendNotification);
        }
    }

    updateStatusBar();
}

//==============================================================================
void MainComponent::midiLearnParameterChanged (const juce::String& paramID, float value)
{
    if (paramID == "loopVolume")
    {
        loopVolumeSlider.setValue (value, juce::dontSendNotification);
        inputRouter.loopVolume = value;
    }
    else if (paramID == "gateThresh")
    {
        gateThreshSlider.setValue (value, juce::dontSendNotification);
        noiseGate.thresholdDb = value;
    }
    else if (paramID == "inputTrim")
    {
        const float db = juce::jmap (value, 0.0f, 1.0f, -24.0f, 24.0f);
        inputTrimSlider.setValue (db, juce::dontSendNotification);
    }
    else if (paramID == "masterFader")
    {
        const float db = juce::jmap (value, 0.0f, 1.0f, -60.0f, 12.0f);
        masterOutputGain.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed);
        if (fxBusPanel) fxBusPanel->setMasterFaderDb (db);
    }
    else if (paramID == "fxBypass")
    {
        bool bp = value >= 0.5f;
        fxBus->setBypassed (bp);
        if (fxBusPanel) fxBusPanel->syncFromBus();
    }
    else if (paramID == "metroToggle")
    {
        metronome.setEnabled (value >= 0.5f);  // toolbar icon lit state follows in timerCallback
    }
    else
    {
        for (int i = 0; i < NUM_CHANNELS; ++i)
        {
            if (paramID == "chFader" + juce::String (i))
            {
                const float db = juce::jmap (value, 0.0f, 1.0f, -60.0f, 12.0f);
                outputFaders[i].setValue (db, juce::dontSendNotification);
                channels[i]->setOutputGain (juce::Decibels::decibelsToGain (db));
                updateFaderLabel (i);
                break;
            }
            if (paramID == "chPan" + juce::String (i))
            {
                const float p = juce::jmap (value, 0.0f, 1.0f, -1.0f, 1.0f);
                outputGainKnobs[i].setValue (p, juce::dontSendNotification);
                channels[i]->setPan (p);
                break;
            }
            if (paramID == "chMute" + juce::String (i))
            {
                bool muted = value >= 0.5f;
                channelMuted[i] = muted;
                muteButtons[i].setToggleState (muted, juce::dontSendNotification);
                updateActiveIndicators();
                break;
            }
        }
    }
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Settings" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int idx, const juce::String&)
{
    juce::PopupMenu m;
    if (idx == 0)
    {
        m.addItem (MenuNew,    "New Project");
        m.addItem (MenuOpen,   "Open Project...");

        // Recent Projects submenu
        auto recentFiles = loadRecentProjects();
        if (! recentFiles.isEmpty())
        {
            juce::PopupMenu recentMenu;
            for (int i = 0; i < recentFiles.size(); ++i)
            {
                auto f = juce::File (recentFiles[i]);
                recentMenu.addItem (MenuRecentBase + i, f.getFileNameWithoutExtension());
            }
            m.addSubMenu ("Recent Projects", recentMenu);
        }

        m.addSeparator();
        m.addItem (MenuSave,   "Save",    currentProjectFile.existsAsFile());
        m.addItem (MenuSaveAs, "Save As...");
        m.addSeparator();
        m.addItem (MenuLiveMode, "Live Input Mode", true, inputRouter.getMode() == InputRouter::Mode::Live);
        m.addItem (MenuLoopMode, "Loop Mode", true, inputRouter.getMode() == InputRouter::Mode::Loop);
        m.addItem (MenuLoadLoop, "Load Loop File...");
        m.addSeparator();
        m.addItem (MenuSetlist, "Setlist...");
        m.addSeparator();
        m.addItem (MenuQuit,   "Quit");
    }
    else
    {
        m.addItem (MenuPluginManager, "Plugin Manager...");
        m.addItem (MenuAsioSettings,  "ASIO Device Settings...");
        m.addItem (MenuMidiRules,     "MIDI Rules...");
        m.addSeparator();

        // MIDI Input submenu (toggle each device on/off)
        {
            juce::PopupMenu midiInMenu;
            auto inputs = juce::MidiInput::getAvailableDevices();
            for (int i = 0; i < inputs.size(); ++i)
                midiInMenu.addItem (MenuMidiInBase + 1 + i, inputs[i].name,
                                    true, deviceManager.isMidiInputDeviceEnabled (inputs[i].identifier));
            if (inputs.isEmpty())
                midiInMenu.addItem (MenuMidiInBase, "(No devices)", false);
            m.addSubMenu ("MIDI Input", midiInMenu);
        }

        // MIDI Output submenu
        {
            juce::PopupMenu midiOutMenu;
            auto outputs = juce::MidiOutput::getAvailableDevices();
            midiOutMenu.addItem (MenuMidiOutBase, "(None)", true, activeMidiOutputId.isEmpty());
            for (int i = 0; i < outputs.size(); ++i)
                midiOutMenu.addItem (MenuMidiOutBase + 1 + i, outputs[i].name,
                                     true, outputs[i].identifier == activeMidiOutputId);
            m.addSubMenu ("MIDI Output", midiOutMenu);
        }

        m.addSeparator();
        m.addItem (MenuEditUIColors, "Edit Knob Colors", true, uiEditMode);
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case MenuNew:          newProject();       break;
        case MenuOpen:         openProject();      break;
        case MenuSave:         saveProject();      break;
        case MenuSaveAs:       saveProjectAs();    break;
        case MenuLiveMode:     inputRouter.setMode (InputRouter::Mode::Live); updateTransportUI(); break;
        case MenuLoopMode:     inputRouter.setMode (InputRouter::Mode::Loop); updateTransportUI(); break;
        case MenuLoadLoop:     buttonClicked (&loadLoopButton); break;
        case MenuPluginManager: showPluginManager(); break;
        case MenuAsioSettings: openAsioSettings(); break;
        case MenuMidiRules:   showMidiRulesEditor(); break;
        case MenuSetlist:      showSetlistPanel(); break;
        case MenuEditUIColors:
            uiEditMode = !uiEditMode;
            helpButton.setColour (juce::TextButton::buttonColourId,
                uiEditMode ? juce::Colour (0xff885522) : juce::Colour (0xff252535));
            helpButton.setColour (juce::TextButton::textColourOffId,
                uiEditMode ? juce::Colour (0xffffaa44) : juce::Colour (0xff8888cc));
            helpButton.setButtonText (uiEditMode ? "E" : "?");
            repaint();
            break;
        case MenuQuit:         juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        default:
            if (id >= MenuRecentBase && id < MenuRecentBase + 10)
                openRecentProject (id - MenuRecentBase);
            else if (id > MenuMidiInBase && id < MenuMidiOutBase)
            {
                auto inputs = juce::MidiInput::getAvailableDevices();
                int idx = id - MenuMidiInBase - 1;
                if (juce::isPositiveAndBelow (idx, inputs.size()))
                {
                    auto devId = inputs[idx].identifier;
                    bool wasEnabled = deviceManager.isMidiInputDeviceEnabled (devId);

                    if (wasEnabled)
                    {
                        deviceManager.setMidiInputDeviceEnabled (devId, false);
                        deviceManager.removeMidiInputDeviceCallback (devId, this);
                    }
                    else
                    {
                        deviceManager.setMidiInputDeviceEnabled (devId, true);
                        deviceManager.addMidiInputDeviceCallback (devId, this);
                    }
                }
            }
            else if (id >= MenuMidiOutBase)
            {
                midiOutput.reset();
                if (id == MenuMidiOutBase)
                {
                    activeMidiOutputId = {};
                }
                else
                {
                    auto outputs = juce::MidiOutput::getAvailableDevices();
                    int idx = id - MenuMidiOutBase - 1;
                    if (juce::isPositiveAndBelow (idx, outputs.size()))
                    {
                        activeMidiOutputId = outputs[idx].identifier;
                        midiOutput = juce::MidiOutput::openDevice (activeMidiOutputId);
                    }
                }
                currentProject.midiOutputDevice = activeMidiOutputId;
                projectDirty = true;
            }
            break;
    }
}

//==============================================================================
void MainComponent::newProject()
{
    currentProject     = ProjectData();
    currentProjectFile = juce::File();
    projectDirty       = false;
    setActiveChannel (0);
    updateStatusBar();
}

void MainComponent::openProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Open UpStage Project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.upstage");
    chooser->launchAsync (juce::FileBrowserComponent::openMode |
                          juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
            {
                ProjectData data;
                if (projectState.loadFromFile (result, data, &sceneManager, &midiLearnManager))
                {
                    currentProjectFile = result;
                    loadProjectData (data);
                    saveLastProjectPath();
                }
            }
        });
}

void MainComponent::saveProject()
{
    if (! currentProjectFile.existsAsFile()) { saveProjectAs(); return; }
    auto data = collectProjectData();
    if (projectState.saveToFile (currentProjectFile, data, &sceneManager, &midiLearnManager))
    {
        projectDirty = false;
        saveLastProjectPath();
        getAutosaveFile().deleteFile();
    }
    updateStatusBar();
}

void MainComponent::saveProjectAs()
{
    auto data = collectProjectData();
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save UpStage Project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.upstage");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode |
                          juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, data] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result != juce::File{})
            {
                auto file = result.withFileExtension ("upstage");
                if (projectState.saveToFile (file, data, &sceneManager, &midiLearnManager))
                {
                    currentProjectFile = file;
                    projectDirty = false;
                    saveLastProjectPath();
                    getAutosaveFile().deleteFile();
                }
                updateStatusBar();
            }
        });
}

void MainComponent::loadProjectData (const ProjectData& data)
{
    // Show loading overlay immediately
    loadingOverlay.show (this, "Loading project...");

    // Count total plugins to load async
    pendingPluginLoads = 0;
    for (int i = 0; i < NUM_CHANNELS; ++i)
        pendingPluginLoads += data.channels[i].plugins.size();
    pendingPluginLoads += data.inputChannelState.plugins.size();
    pendingPluginLoads += data.fxBusState.plugins.size();

    currentProject = data;
    setActiveChannel (data.activeChannel);
    parallelRouting = data.parallelRouting;
    updateRoutingModeButton();
    midiTranslator.setRules (data.midiRules);
    tapTempo.setBPM (data.tapTempoBPM);

    // Restore MIDI devices from project
    if (data.midiInputDevice.isNotEmpty())
    {
        if (activeMidiInputId.isNotEmpty())
        {
            deviceManager.removeMidiInputDeviceCallback (activeMidiInputId, this);
            deviceManager.setMidiInputDeviceEnabled (activeMidiInputId, false);
        }
        activeMidiInputId = data.midiInputDevice;
        deviceManager.addMidiInputDeviceCallback (activeMidiInputId, this);
        deviceManager.setMidiInputDeviceEnabled (activeMidiInputId, true);
    }
    if (data.midiOutputDevice.isNotEmpty())
    {
        midiOutput.reset();
        activeMidiOutputId = data.midiOutputDevice;
        midiOutput = juce::MidiOutput::openDevice (activeMidiOutputId);
    }

    noiseGate.enabled     = data.gateEnabled;
    noiseGate.thresholdDb = data.gateThreshDb;
    noiseGate.attackMs    = data.gateAttackMs;
    noiseGate.holdMs      = data.gateHoldMs;
    noiseGate.releaseMs   = data.gateReleaseMs;
    gateToggle      .setToggleState (data.gateEnabled,   juce::dontSendNotification);
    gateThreshSlider.setValue       (data.gateThreshDb,  juce::dontSendNotification);

    inputTrimSlider.setValue (data.inputTrimDb, juce::dontSendNotification);

    // Restore knob colors
    knobColorMap = data.knobColorMap;

    // Arm soft takeover for all bound parameters after a project load
    midiLearnManager.setParameterTarget ("loopVolume", (float) loopVolumeSlider.getValue());
    midiLearnManager.setParameterTarget ("gateThresh", noiseGate.thresholdDb);
    midiLearnManager.setParameterTarget ("inputTrim",  data.inputTrimDb);

    if (data.useLoopFile && juce::File (data.loopFilePath).existsAsFile())
    {
        inputRouter.loadLoopFile (juce::File (data.loopFilePath));
        inputRouter.setMode (InputRouter::Mode::Loop);
        loopFileLabel.setText (juce::File (data.loopFilePath).getFileName(),
                               juce::dontSendNotification);
    }

    // Clear existing plugin chains
    for (int i = 0; i < NUM_CHANNELS; ++i)
        channels[i]->clearAllPlugins();
    inputChannel->clearAllPlugins();
    fxBus->clearAllPlugins();

    // Set channel params (no plugin loading yet)
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        channels[i]->setActive     (i == data.activeChannel);
        channels[i]->setName       (data.channels[i].name);
        channels[i]->setInputGain  (data.channels[i].inputGain);
        channels[i]->setOutputGain (data.channels[i].outputGain);
        channels[i]->setPan        (data.channels[i].pan);
        outputGainKnobs[i].setValue (data.channels[i].pan, juce::dontSendNotification);
        channelStripPanels[i]->setAppearances (data.channels[i].pluginAppearances);
    }

    inputChannel->setInputGain (data.inputChannelState.inputGain);
    inputChannel->setOutputGain (data.inputChannelState.outputGain);
    inputDirectLevel.store (data.inputDirectMix, std::memory_order_relaxed);
    inputDirectKnob.setValue (data.inputDirectMix, juce::dontSendNotification);

    {
        FxBus::State fbs;
        fbs.bypassed = data.fxBusState.bypassed;
        fxBus->setState (fbs);
    }
    fxBusPanel->setAppearances (data.fxBusState.pluginAppearances);
    fxBusPanel->syncFromBus();

    // Detect which scene matches the loaded state (if any)
    activeSceneIndex = -1;
    for (int s = 0; s < NUM_SCENES; ++s)
    {
        if (! sceneManager.isSceneUsed (s)) continue;

        ChannelStrip* ptrs[NUM_CHANNELS];
        for (int c = 0; c < NUM_CHANNELS; ++c) ptrs[c] = channels[c].get();

        bool match = true;
        for (int c = 0; c < NUM_CHANNELS; ++c)
        {
            auto cur = channels[c]->getState();
            auto scn = sceneManager.getChannelState (s, c);
            if (std::abs (cur.outputGain - scn.outputGain) > 0.001f ||
                std::abs (cur.inputGain - scn.inputGain) > 0.001f ||
                std::abs (cur.pan - scn.pan) > 0.001f)
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            activeSceneIndex = s;
            break;
        }
    }

    projectDirty = false;
    updateActiveIndicators();
    updateSceneButtonStates();
    updateTransportUI();
    updateStatusBar();

    // Defer plugin loading to the next message loop pass so the overlay paints first
    if (pendingPluginLoads > 0)
    {
        auto dataCopy = data;
        juce::MessageManager::callAsync ([this, dataCopy] { loadProjectPlugins (dataCopy); });
    }
    else
    {
        loadingOverlay.dismiss();
    }
}

void MainComponent::loadProjectPlugins (const ProjectData& data)
{
    // Load channel plugins
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        for (int slotIndex = 0; slotIndex < data.channels[i].plugins.size(); ++slotIndex)
        {
            const auto& slot = data.channels[i].plugins.getReference (slotIndex);
            juce::PluginDescription desc;
            if (auto found = knownPluginList.getTypeForIdentifierString (slot.pluginIdentifier))
                desc = *found;
            else
            {
                desc.pluginFormatName = "VST3";
                desc.fileOrIdentifier = slot.pluginIdentifier;
                desc.name             = slot.pluginName;
            }

            juce::MemoryBlock stateBlob = slot.stateData;
            bool bypassed   = slot.isBypassed;
            int  chanIdx    = i;
            int  slotIdx    = slotIndex;

            channels[i]->addPlugin (desc, [this, chanIdx, slotIdx, stateBlob, bypassed] (bool ok)
            {
                if (ok)
                {
                    if (stateBlob.getSize() > 0)
                        if (auto* proc = channels[chanIdx]->getPlugin (slotIdx))
                            proc->setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
                    channels[chanIdx]->setPluginBypassed (slotIdx, bypassed);
                }
                juce::MessageManager::callAsync ([this, chanIdx] {
                    channelStripPanels[chanIdx]->refresh();
                    if (--pendingPluginLoads <= 0)
                        loadingOverlay.dismiss();
                });
            });
        }
    }

    // Load input channel plugins
    for (int slotIndex = 0; slotIndex < data.inputChannelState.plugins.size(); ++slotIndex)
    {
        const auto& slot = data.inputChannelState.plugins.getReference (slotIndex);
        juce::PluginDescription desc;
        if (auto found = knownPluginList.getTypeForIdentifierString (slot.pluginIdentifier))
            desc = *found;
        else
        {
            desc.pluginFormatName = "VST3";
            desc.fileOrIdentifier = slot.pluginIdentifier;
            desc.name             = slot.pluginName;
        }
        juce::MemoryBlock stateBlob = slot.stateData;
        bool bypassed = slot.isBypassed;
        int slotIdx = slotIndex;

        inputChannel->addPlugin (desc, [this, slotIdx, stateBlob, bypassed] (bool ok)
        {
            if (ok)
            {
                if (stateBlob.getSize() > 0)
                    if (auto* proc = inputChannel->getPlugin (slotIdx))
                        proc->setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
                inputChannel->setPluginBypassed (slotIdx, bypassed);
            }
            juce::MessageManager::callAsync ([this] {
                inputChannelPanel->refresh();
                if (--pendingPluginLoads <= 0)
                    loadingOverlay.dismiss();
            });
        });
    }

    // Load master FX bus plugins
    for (int slotIndex = 0; slotIndex < data.fxBusState.plugins.size(); ++slotIndex)
    {
        const auto& slot = data.fxBusState.plugins.getReference (slotIndex);
        juce::PluginDescription desc;
        if (auto found = knownPluginList.getTypeForIdentifierString (slot.pluginIdentifier))
            desc = *found;
        else
        {
            desc.pluginFormatName = "VST3";
            desc.fileOrIdentifier = slot.pluginIdentifier;
            desc.name             = slot.pluginName;
        }
        juce::MemoryBlock stateBlob = slot.stateData;
        bool bypassed               = slot.isBypassed;
        int  slotIdx                = slotIndex;

        fxBus->addPlugin (desc, [this, slotIdx, stateBlob, bypassed] (bool ok)
        {
            if (ok)
            {
                if (stateBlob.getSize() > 0)
                    if (auto* proc = fxBus->getPlugin (slotIdx))
                        proc->setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
                fxBus->setPluginBypassed (slotIdx, bypassed);
            }
            juce::MessageManager::callAsync ([this] {
                fxBusPanel->refresh();
                if (--pendingPluginLoads <= 0)
                    loadingOverlay.dismiss();
            });
        });
    }
}

void MainComponent::loadSongState (const ProjectData& data)
{
    // Mute channels during state restoration to avoid glitches
    for (int c = 0; c < NUM_CHANNELS; ++c)
        channels[c]->setOutputGain (0.0f);
    sceneMuteActive = true;

    // Restore channel states (faders, bypass, plugin parameters) — no plugin reload
    for (int c = 0; c < NUM_CHANNELS; ++c)
        channels[c]->setState (data.channels[c]);

    // Restore input channel state
    inputChannel->setState (data.inputChannelState);
    inputDirectLevel.store (data.inputDirectMix, std::memory_order_relaxed);
    inputDirectKnob.setValue (data.inputDirectMix, juce::dontSendNotification);

    // Restore gate settings
    noiseGate.enabled     = data.gateEnabled;
    noiseGate.thresholdDb = data.gateThreshDb;
    noiseGate.attackMs    = data.gateAttackMs;
    noiseGate.holdMs      = data.gateHoldMs;
    noiseGate.releaseMs   = data.gateReleaseMs;
    gateToggle.setToggleState (data.gateEnabled, juce::dontSendNotification);
    gateThreshSlider.setValue (data.gateThreshDb, juce::dontSendNotification);
    inputTrimSlider.setValue (data.inputTrimDb, juce::dontSendNotification);

    // Restore master FX bus state
    FxBus::State fbs;
    fbs.bypassed = data.fxBusState.bypassed;
    fbs.plugins  = data.fxBusState.plugins;
    fxBus->setState (fbs);
    fxBusPanel->syncFromBus();

    // Sync UI
    for (int c = 0; c < NUM_CHANNELS; ++c)
    {
        float outDb = juce::Decibels::gainToDecibels (channels[c]->getOutputGain());
        float inDb  = juce::Decibels::gainToDecibels (channels[c]->getInputGain());
        outputFaders[c].setValue (outDb, juce::dontSendNotification);
        outputGainKnobs[c].setValue (channels[c]->getPan(), juce::dontSendNotification);
        inputTrimKnobs[c].setValue (inDb, juce::dontSendNotification);
        updateFaderLabel (c);
        channelStripPanels[c]->refresh();
    }
    inputChannelPanel->refresh();

    // Unmute after plugin states have settled
    juce::Timer::callAfterDelay (100, [this]
    {
        if (! sceneMuteActive) return;
        for (int c = 0; c < NUM_CHANNELS; ++c)
        {
            float db = (float) outputFaders[c].getValue();
            channels[c]->setOutputGain (juce::Decibels::decibelsToGain (db));
        }
        sceneMuteActive = false;
    });

    midiLearnManager.setParameterTarget ("loopVolume", (float) loopVolumeSlider.getValue());
    midiLearnManager.setParameterTarget ("gateThresh", noiseGate.thresholdDb);
    midiLearnManager.setParameterTarget ("inputTrim", data.inputTrimDb);

    projectDirty = false;
    updateActiveIndicators();
    updateSceneButtonStates();
    updateStatusBar();
}

ProjectData MainComponent::collectProjectData() const
{
    ProjectData data = currentProject;
    data.activeChannel   = activeChannel;
    data.parallelRouting = parallelRouting;
    data.inputTrimDb     = (float) inputTrimSlider.getValue();
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        data.channels[i] = channels[i]->getState();
        data.channels[i].pluginAppearances = channelStripPanels[i]->getAppearances();
    }
    data.inputChannelState = inputChannel->getState();
    data.inputDirectMix  = inputDirectLevel.load();
    data.midiRules       = midiTranslator.getRules();
    data.midiInputDevice = activeMidiInputId;
    data.midiOutputDevice = activeMidiOutputId;
    data.useLoopFile     = (inputRouter.getMode() == InputRouter::Mode::Loop);
    data.loopFilePath    = inputRouter.getLoopFileName();
    data.tapTempoBPM     = tapTempo.getBPM();
    data.gateEnabled     = noiseGate.enabled;
    data.gateThreshDb    = noiseGate.thresholdDb;
    data.gateAttackMs    = noiseGate.attackMs;
    data.gateHoldMs      = noiseGate.holdMs;
    data.gateReleaseMs   = noiseGate.releaseMs;

    // Master insert chain state
    FxBus::State fbs = fxBus->getState();
    data.fxBusState.bypassed = fbs.bypassed;
    data.fxBusState.plugins  = fbs.plugins;
    data.fxBusState.pluginAppearances = fxBusPanel->getAppearances();

    // Knob colors
    data.knobColorMap = knobColorMap;

    return data;
}

//==============================================================================
void MainComponent::saveLastProjectPath()
{
    if (currentProjectFile.existsAsFile())
        addToRecentProjects (currentProjectFile);
}

juce::StringArray MainComponent::loadRecentProjects()
{
    auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("UpStage").getChildFile ("RecentProjects.txt");

    juce::StringArray result;
    if (file.existsAsFile())
    {
        auto lines = juce::StringArray::fromLines (file.loadFileAsString());
        for (const auto& line : lines)
            if (line.trim().isNotEmpty())
                result.add (line.trim());
    }
    return result;
}

void MainComponent::addToRecentProjects (const juce::File& projectFile)
{
    auto recentFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("UpStage").getChildFile ("RecentProjects.txt");

    auto existing = loadRecentProjects();

    // Remove if already present
    existing.removeString (projectFile.getFullPathName());

    // Insert at front
    existing.insert (0, projectFile.getFullPathName());

    // Cap at 10
    while (existing.size() > 10)
        existing.remove (existing.size() - 1);

    // Save
    juce::String content;
    for (const auto& entry : existing)
        content += entry + "\n";

    recentFile.getParentDirectory().createDirectory();
    recentFile.replaceWithText (content);
}

void MainComponent::openRecentProject (int index)
{
    auto recent = loadRecentProjects();
    if (index < 0 || index >= recent.size()) return;

    juce::File file (recent[index]);
    if (file.existsAsFile())
    {
        ProjectData data;
        if (projectState.loadFromFile (file, data, &sceneManager, &midiLearnManager))
        {
            currentProjectFile = file;
            loadProjectData (data);
            saveLastProjectPath();
        }
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "File Not Found",
            "The project file no longer exists:\n" + file.getFullPathName(), "OK");
    }
}

//==============================================================================
void MainComponent::setActiveChannel (int idx)
{
    if (! juce::isPositiveAndBelow (idx, NUM_CHANNELS)) return;
    activeChannel = idx;
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        channels[i]->setActive (i == idx);
        channelFadeGain[i].setTargetValue (i == idx ? 1.0f : 0.0f);
    }
    updateActiveIndicators();
    updateStatusBar();
}

void MainComponent::sendMidiPanic()
{
    if (midiOutput)
    {
        for (int ch = 1; ch <= 16; ++ch)
        {
            midiOutput->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
            midiOutput->sendMessageNow (juce::MidiMessage::allSoundOff (ch));
        }
    }
    juce::ScopedLock sl (midiLock);
    for (int ch = 1; ch <= 16; ++ch)
    {
        pendingMidi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        pendingMidi.addEvent (juce::MidiMessage::allSoundOff (ch), 0);
    }
}

//==============================================================================
void MainComponent::scanForPlugins (bool clearCache)
{
    // Define a scanning thread using JUCE's built-in crashproof scanning
    class ScanThread : public juce::ThreadWithProgressWindow
    {
    public:
        ScanThread (juce::KnownPluginList& list, juce::AudioPluginFormatManager& fm, MainComponent* owner, bool clearFirst)
            : ThreadWithProgressWindow ("Scanning VST3 Plugins", true, true),
              knownList (list), formatManager (fm), mainComponent (owner), shouldClearCache (clearFirst)
        {
        }

        bool shouldClearCache;

        void run() override
        {
            // Create log file for diagnostics
            auto logFile = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("UpStage_Scan_Log.txt");
            logFile.deleteFile();

            auto log = [&logFile](const juce::String& msg) {
                logFile.appendText (msg + "\n");
                DBG (msg);
            };

            log ("=== VST3 Scan Starting ===");

            // Check if we have a VST3 format
            auto* vst3Format = formatManager.getFormat (0);
            if (vst3Format == nullptr)
            {
                log ("ERROR: No plugin format found!");
                return;
            }

            log ("Format: " + vst3Format->getName());

            juce::FileSearchPath vst3Paths;
            vst3Paths.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
            vst3Paths.add (juce::File ("C:\\Program Files (x86)\\Common Files\\VST3"));

            // Check if directories exist
            for (int i = 0; i < vst3Paths.getNumPaths(); ++i)
            {
                auto dir = vst3Paths[i];
                log ("Path " + juce::String(i) + ": " + dir.getFullPathName() +
                    " (exists: " + juce::String(dir.exists() ? "yes" : "no") + ")");
            }

            auto appDataDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("UpStage");
            auto pluginCacheFile = appDataDir.getChildFile ("PluginCache.xml");
            auto blacklistFile = appDataDir.getChildFile ("PluginBlacklist.txt");
            auto crashMarkerFile = appDataDir.getChildFile ("ScanningPlugin.txt");

            appDataDir.createDirectory();

            // Clear cache if requested
            if (shouldClearCache)
            {
                log ("Clearing plugin cache and rescanning all...");
                log ("Before clear: " + juce::String (knownList.getNumTypes()) + " plugins");
                knownList.clear();
                pluginCacheFile.deleteFile();
                log ("After clear: " + juce::String (knownList.getNumTypes()) + " plugins");
            }

            log ("Starting VST3 scan with crash recovery...");
            log ("Starting with " + juce::String (knownList.getNumTypes()) + " cached plugins");

            // Load blacklist
            juce::StringArray blacklist;

            if (blacklistFile.existsAsFile())
            {
                auto lines = juce::StringArray::fromLines (blacklistFile.loadFileAsString());
                // Filter out empty strings!
                for (const auto& line : lines)
                {
                    if (line.trim().isNotEmpty())
                        blacklist.add (line.trim());
                }
                log ("Loaded " + juce::String (blacklist.size() - 1) + " plugins from blacklist file");
            }

            log ("Total blacklist entries: " + juce::String(blacklist.size()));
            for (int i = 0; i < blacklist.size(); ++i)
            {
                log ("  Blacklist[" + juce::String(i) + "]: '" + blacklist[i] + "'");
            }

            // Check if we crashed during last scan
            if (crashMarkerFile.existsAsFile())
            {
                auto crashedPlugin = crashMarkerFile.loadFileAsString().trim();
                if (crashedPlugin.isNotEmpty())
                {
                    log ("Detected crash on: " + crashedPlugin);
                    blacklist.add (crashedPlugin);
                    // Save to permanent blacklist
                    blacklistFile.appendText (crashedPlugin + "\n");
                }
                crashMarkerFile.deleteFile();
            }

            // Manual check: list all .vst3 in the directory
            int foundCount = 0;
            for (int i = 0; i < vst3Paths.getNumPaths(); ++i)
            {
                auto dir = vst3Paths[i];
                if (dir.exists())
                {
                    auto files = dir.findChildFiles (juce::File::findFilesAndDirectories, true, "*.vst3");
                    log ("Found " + juce::String(files.size()) + " .vst3 files/folders in " + dir.getFullPathName());
                    foundCount += files.size();
                }
            }
            log ("Total .vst3 files found manually: " + juce::String(foundCount));

            // Load set of already-scanned file paths
            auto scannedFilesFile = appDataDir.getChildFile ("ScannedFiles.txt");
            juce::StringArray alreadyScanned;
            if (! shouldClearCache && scannedFilesFile.existsAsFile())
            {
                auto lines = juce::StringArray::fromLines (scannedFilesFile.loadFileAsString());
                for (const auto& line : lines)
                    if (line.trim().isNotEmpty())
                        alreadyScanned.add (line.trim());
                log ("Loaded " + juce::String (alreadyScanned.size()) + " previously scanned file paths");
            }
            else if (shouldClearCache)
            {
                scannedFilesFile.deleteFile();
            }

            juce::PluginDirectoryScanner scanner (knownList,
                                                  *formatManager.getFormat (0),
                                                  vst3Paths,
                                                  true,
                                                  juce::File());

            juce::String pluginBeingScanned;
            int newPluginsFound = 0;
            int skippedCount = 0;

            while (true)
            {
                auto nextFile = scanner.getNextPluginFileThatWillBeScanned();

                if (nextFile.isEmpty())
                    break;

                if (threadShouldExit())
                {
                    crashMarkerFile.deleteFile();
                    return;
                }

                // Skip already-scanned files
                if (alreadyScanned.contains (nextFile))
                {
                    scanner.skipNextFile();
                    skippedCount++;
                    continue;
                }

                // Check blacklist
                bool shouldSkip = false;
                for (const auto& pattern : blacklist)
                {
                    if (nextFile.contains (pattern))
                    {
                        setStatusMessage ("Skipping: " + juce::File (nextFile).getFileName());
                        scanner.skipNextFile();
                        shouldSkip = true;
                        break;
                    }
                }

                if (shouldSkip)
                    continue;

                // Write crash marker BEFORE scanning
                crashMarkerFile.replaceWithText (nextFile);

                setStatusMessage ("Scanning: " +
                    nextFile.fromLastOccurrenceOf ("/", false, false)
                            .fromLastOccurrenceOf ("\\", false, false));

                int countBefore = knownList.getNumTypes();
                bool scanResult = scanner.scanNextFile (true, pluginBeingScanned);

                if (! scanResult)
                    break;

                int countAfter = knownList.getNumTypes();
                bool pluginWasAdded = countAfter > countBefore;

                // Only mark as scanned if plugin was actually registered
                if (pluginWasAdded)
                {
                    if (auto xml = knownList.createXml())
                        xml->writeTo (pluginCacheFile);
                    newPluginsFound += (countAfter - countBefore);
                }
                else
                {
                    auto failedFiles = scanner.getFailedFiles();
                    for (const auto& f : failedFiles)
                        log ("Failed to validate: " + f);
                }

                scannedFilesFile.appendText (nextFile + "\n");
                crashMarkerFile.deleteFile();
                setProgress (scanner.getProgress());
            }

            // Clean up crash marker
            crashMarkerFile.deleteFile();

            int totalPlugins = knownList.getNumTypes();
            DBG ("Scan complete. Found " + juce::String (newPluginsFound) + " new plugins. Total: " + juce::String (totalPlugins));

            // Show completion message on main thread
            juce::MessageManager::callAsync ([totalPlugins, newPluginsFound] {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::InfoIcon, "Scan Complete",
                    juce::String (totalPlugins) + " total plugins (" +
                    juce::String (newPluginsFound) + " newly scanned).", "OK");
            });
        }

        void threadComplete (bool) override
        {
            // Self-delete since we were created with new
            delete this;
        }

    private:
        juce::KnownPluginList& knownList;
        juce::AudioPluginFormatManager& formatManager;
        MainComponent* mainComponent;
    };

    // Run the scan
    auto* scanner = new ScanThread (knownPluginList, pluginFormatManager, this, clearCache);
    scanner->launchThread();
}

void MainComponent::showAddPluginMenu (int chanIdx)
{
    if (knownPluginList.getTypes().isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon, "No Plugins",
            "No plugins found. Open Settings > Plugin Manager to scan.", "OK");
        return;
    }

    auto* browser = new PluginBrowserWindow (knownPluginList);
    browser->onPluginSelected = [this, chanIdx] (const juce::PluginDescription& desc)
    {
        if (chanIdx == -1)
        {
            inputChannel->addPlugin (desc, [this] (bool ok) {
                if (! ok)
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "UpStage", "Failed to load plugin.", "OK");
                juce::MessageManager::callAsync ([this] {
                    inputChannelPanel->refresh();
                });
                projectDirty = true;
            });
        }
        else
        {
            channels[chanIdx]->addPlugin (desc, [this, chanIdx] (bool ok) {
                if (! ok)
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "UpStage", "Failed to load plugin.", "OK");
                juce::MessageManager::callAsync ([this, chanIdx] {
                    channelStripPanels[chanIdx]->refresh();
                });
                projectDirty = true;
            });
        }
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (browser);
    opts.dialogTitle              = "Add Plugin";
    opts.dialogBackgroundColour   = juce::Colour (0xff1e1e1e);
    opts.useNativeTitleBar        = true;
    opts.resizable                = true;
    opts.escapeKeyTriggersCloseButton = true;
    opts.launchAsync();
}

void MainComponent::showAddPluginMenuForFxBus()
{
    if (knownPluginList.getTypes().isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon, "No Plugins",
            "No plugins found. Open Settings > Plugin Manager to scan.", "OK");
        return;
    }

    auto* browser = new PluginBrowserWindow (knownPluginList);
    browser->onPluginSelected = [this] (const juce::PluginDescription& desc)
    {
        fxBus->addPlugin (desc, [this] (bool ok) {
            if (! ok)
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "UpStage", "Failed to load FX plugin.", "OK");
            juce::MessageManager::callAsync ([this] { fxBusPanel->refresh(); });
            projectDirty = true;
        });
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (browser);
    opts.dialogTitle              = "Add Plugin";
    opts.dialogBackgroundColour   = juce::Colour (0xff1e1e1e);
    opts.useNativeTitleBar        = true;
    opts.resizable                = true;
    opts.escapeKeyTriggersCloseButton = true;
    opts.launchAsync();
}

void MainComponent::showPluginManager()
{
    auto* panel = new PluginManagerWindow (knownPluginList, pluginFormatManager);
    panel->onScanPlugins = [this] (bool clearCache) {
        scanForPlugins (clearCache);
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel);
    opts.dialogTitle            = "Plugin Manager";
    opts.dialogBackgroundColour = juce::Colour (0xff1e1e1e);
    opts.useNativeTitleBar      = true;
    opts.resizable              = true;
    opts.launchAsync();
}

void MainComponent::showMidiRulesEditor()
{
    auto* panel = new MidiRulesPanel (midiTranslator.getRules());
    activeMidiRulesPanel = panel;

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel);
    opts.dialogTitle            = "MIDI Rules";
    opts.dialogBackgroundColour = juce::Colour (0xff1a1a2a);
    opts.useNativeTitleBar      = false;
    opts.resizable              = true;
    opts.escapeKeyTriggersCloseButton = true;

    panel->onClose = [this, panel]
    {
        midiTranslator.setRules (panel->getRules());
        activeMidiRulesPanel = nullptr;
        projectDirty = true;
    };
    opts.launchAsync();
}

void MainComponent::openAsioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (
        deviceManager, 0, 1, 0, 2, true, false, false, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (selector);
    opts.dialogTitle              = "Audio / MIDI Settings";
    opts.dialogBackgroundColour   = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar        = true;
    opts.resizable                = false;
    opts.launchAsync();
}

void MainComponent::showSetlistPanel()
{
    auto* panel = new SetlistPanel (setlistManager);
    panel->onSongSelected = [this] (int idx) {
        auto doLoad = [this, idx]
        {
            ProjectData data;
            if (setlistManager.loadSongAtIndex (idx, data))
            {
                loadProjectData (data);
                if (auto* song = setlistManager.getSong (idx))
                {
                    if (song->preferredSceneIndex >= 0)
                        applySceneWithMute (song->preferredSceneIndex);
                }
            }
            if (songBar) songBar->refresh();
        };

        if (projectDirty)
        {
            auto* aw = new juce::AlertWindow (
                "Unsaved Changes",
                "Current project has unsaved changes. Save before switching songs?",
                juce::AlertWindow::QuestionIcon);
            aw->addButton ("Save", 1);
            aw->addButton ("Don't Save", 2);
            aw->addButton ("Cancel", 0);
            aw->enterModalState (true, juce::ModalCallbackFunction::create (
                [this, doLoad, aw] (int result)
                {
                    if (result == 0) { delete aw; return; }
                    if (result == 1) saveProject();
                    doLoad();
                    delete aw;
                }), false);
        }
        else
        {
            doLoad();
        }
    };

    panel->onSaveSongRequested = [this] { saveSongState(); };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel);
    opts.dialogTitle            = "Setlist";
    opts.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    opts.useNativeTitleBar      = true;
    opts.resizable              = true;
    opts.launchAsync();
}

void MainComponent::saveSongState()
{
    auto songsDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                        .getChildFile ("UpStage").getChildFile ("Songs");
    songsDir.createDirectory();

    auto chooser = std::make_shared<juce::FileChooser> (
        "Save as Song", songsDir, "*.upstage");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode |
                          juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File{}) return;

            auto file = result.withFileExtension ("upstage");
            auto data = collectProjectData();
            if (projectState.saveToFile (file, data, &sceneManager, &midiLearnManager))
            {
                SetlistManager::Song song;
                song.name               = file.getFileNameWithoutExtension();
                song.filePath           = file;
                song.preferredSceneIndex = activeSceneIndex >= 0 ? activeSceneIndex : 0;
                setlistManager.addSong (song);
            }
        });
}

//==============================================================================
void MainComponent::applySceneWithMute (int sceneIndex)
{
    ChannelStrip* ptrs[NUM_CHANNELS];
    for (int c = 0; c < NUM_CHANNELS; ++c) ptrs[c] = channels[c].get();

    auto result = sceneManager.applyScene (sceneIndex, ptrs);
    if (! result.success)
        return;

    // Mute channels AFTER setState restored them (setState overwrites output gain)
    // Store the scene's target gains, then zero the actual output for glitch prevention
    float targetGain[NUM_CHANNELS];
    for (int c = 0; c < NUM_CHANNELS; ++c)
    {
        targetGain[c] = channels[c]->getOutputGain();
        channels[c]->setOutputGain (0.0f);
    }
    sceneMuteActive = true;

    activeSceneIndex = sceneIndex;

    noiseGate.enabled     = result.globals.gateEnabled;
    noiseGate.thresholdDb = result.globals.gateThreshDb;
    noiseGate.attackMs    = result.globals.gateAttackMs;
    noiseGate.holdMs      = result.globals.gateHoldMs;
    noiseGate.releaseMs   = result.globals.gateReleaseMs;
    gateToggle.setToggleState (result.globals.gateEnabled, juce::dontSendNotification);
    gateThreshSlider.setValue (result.globals.gateThreshDb, juce::dontSendNotification);
    inputTrimSlider.setValue (result.globals.inputTrimDb, juce::dontSendNotification);

    // Restore FX bus state (bus bypass + per-plugin bypass + plugin parameters)
    FxBus::State fbs;
    fbs.bypassed = result.globals.fxBusState.bypassed;
    fbs.plugins  = result.globals.fxBusState.plugins;
    fxBus->setState (fbs);
    fxBusPanel->syncFromBus();

    // Restore input channel state
    inputChannel->setState (result.globals.inputChannelState);
    inputDirectLevel.store (result.globals.inputDirectMix, std::memory_order_relaxed);
    inputDirectKnob.setValue (result.globals.inputDirectMix, juce::dontSendNotification);
    inputChannelPanel->refresh();

    // Unmute after a short delay to let plugin state settle
    juce::Timer::callAfterDelay (100, [this, targetGain]
    {
        if (! sceneMuteActive) return;
        for (int c = 0; c < NUM_CHANNELS; ++c)
            channels[c]->setOutputGain (targetGain[c]);
        sceneMuteActive = false;
    });

    for (int c = 0; c < NUM_CHANNELS; ++c)
    {
        float outDb = juce::Decibels::gainToDecibels (targetGain[c]);
        float inDb  = juce::Decibels::gainToDecibels (channels[c]->getInputGain());
        outputFaders[c].setValue (outDb, juce::dontSendNotification);
        outputGainKnobs[c].setValue (channels[c]->getPan(), juce::dontSendNotification);
        inputTrimKnobs[c].setValue (inDb, juce::dontSendNotification);
        updateFaderLabel (c);
        channelStripPanels[c]->refresh();
    }

    midiLearnManager.setParameterTarget ("loopVolume",
        (float) loopVolumeSlider.getValue());
    midiLearnManager.setParameterTarget ("gateThresh",
        noiseGate.thresholdDb);
    midiLearnManager.setParameterTarget ("inputTrim",
        (float) inputTrimSlider.getValue());
    updateSceneButtonStates();
}

void MainComponent::captureSceneFromCurrent (int sceneIndex)
{
    ChannelStrip* ptrs[NUM_CHANNELS];
    for (int c = 0; c < NUM_CHANNELS; ++c) ptrs[c] = channels[c].get();
    SceneManager::GlobalState gs;
    gs.gateEnabled  = noiseGate.enabled;
    gs.gateThreshDb = noiseGate.thresholdDb;
    gs.gateAttackMs  = noiseGate.attackMs;
    gs.gateHoldMs    = noiseGate.holdMs;
    gs.gateReleaseMs = noiseGate.releaseMs;
    gs.inputTrimDb  = (float) inputTrimSlider.getValue();
    auto fbs = fxBus->getState();
    gs.fxBusState.bypassed = fbs.bypassed;
    gs.fxBusState.plugins  = fbs.plugins;
    gs.inputChannelState = inputChannel->getState();
    gs.inputDirectMix    = inputDirectLevel.load();
    sceneManager.captureScene (sceneIndex, ptrs, gs);
    updateSceneButtonStates();
    projectDirty = true;
}

void MainComponent::flashSceneButton (int sceneIndex)
{
    if (! juce::isPositiveAndBelow (sceneIndex, NUM_SCENES)) return;
    sceneButtons[sceneIndex].setColour (juce::TextButton::buttonColourId,
        juce::Colour (0xff00dd00));
    sceneButtons[sceneIndex].setColour (juce::TextButton::textColourOffId,
        juce::Colours::white);
    sceneFlashIndex = sceneIndex;
    sceneFlashCounter = 8;  // ~500ms at 60Hz timer
}

//==============================================================================
void MainComponent::showChannelRenameDialog (int channelIndex)
{
    if (! juce::isPositiveAndBelow (channelIndex, NUM_CHANNELS)) return;

    auto current = channelLabels[channelIndex].getText();
    auto* aw = new juce::AlertWindow ("Rename Channel",
                                       "Enter a name for channel " + juce::String (channelIndex + 1) + ":",
                                       juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", current, "Name:");
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, channelIndex, aw] (int res)
        {
            if (res == 1)
            {
                auto newName = aw->getTextEditorContents ("name").trim();
                if (newName.isNotEmpty())
                {
                    channelLabels[channelIndex].setText (newName, juce::sendNotification);
                    channels[channelIndex]->setName (newName.toStdString());
                    projectDirty = true;
                }
            }
            delete aw;
        }), true);
}

void MainComponent::updateSceneButtonStates()
{
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        bool used = sceneManager.isSceneUsed (i);
        bool active = (i == activeSceneIndex);

        if (active)
        {
            sceneButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a5a2a));
            sceneButtons[i].setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88ff88));
        }
        else
        {
            sceneButtons[i].setColour (juce::TextButton::buttonColourId,
                used ? juce::Colour (0xff243a24) : juce::Colour (0xff1a1a1a));
            sceneButtons[i].setColour (juce::TextButton::textColourOffId,
                used ? juce::Colour (0xff66cc66) : juce::Colour (0xff555555));
        }
        sceneButtons[i].setButtonText (used ? sceneManager.getSceneName (i)
                                            : ("S" + juce::String (i + 1)));
    }
    repaint();
}

void MainComponent::showKnobReadout (juce::Component& nearComp, const juce::String& text)
{
    if (knobReadout == nullptr) return;
    knobReadout->setText (text, juce::dontSendNotification);
    auto b = getLocalArea (&nearComp, nearComp.getLocalBounds());
    const int w = 56, h = 18;
    knobReadout->setBounds (b.getCentreX() - w / 2,
                            b.getY() - h - 2,   // just above the knob
                            w, h);
    knobReadout->setVisible (true);
    knobReadout->toFront (false);
}

void MainComponent::hideKnobReadout()
{
    if (knobReadout != nullptr) knobReadout->setVisible (false);
}

juce::String MainComponent::paramIdForComponent (juce::Component* c) const
{
    auto it = learnableControls.find (c);
    return it != learnableControls.end() ? it->second : juce::String();
}

void MainComponent::updateRoutingModeButton()
{
    // Parallel = all channels sum to master (teal); Single = only the active
    // channel plays (amber). Explicit words beat the old >>/> glyphs.
    routingModeButton.setButtonText (parallelRouting ? "PARALLEL" : "SINGLE");
    routingModeButton.setColour (juce::TextButton::buttonColourId,
        parallelRouting ? juce::Colour (0xff2a4a4a)    // teal
                        : juce::Colour (0xff4a3a1a));   // amber
    routingModeButton.setColour (juce::TextButton::textColourOffId,
        parallelRouting ? juce::Colour (0xff88dddd) : juce::Colour (0xffddbb88));
    routingModeButton.setTooltip (parallelRouting
        ? "Routing: PARALLEL - all channels play and sum to master. Click for SINGLE."
        : "Routing: SINGLE - only the active channel plays. Click for PARALLEL.");
}

void MainComponent::updateTransportUI()
{
    bool isLoop = (inputRouter.getMode() == InputRouter::Mode::Loop);
    {
        auto defaultBg = getLookAndFeel().findColour (juce::TextButton::buttonColourId);
        loopInputButton.setColour (juce::TextButton::buttonColourId,
            isLoop  ? juce::Colours::royalblue : defaultBg);
        liveInputButton.setColour (juce::TextButton::buttonColourId,
            !isLoop ? juce::Colours::royalblue : defaultBg);
    }
}

void MainComponent::updateStatusBar()
{
    // Left side: housekeeping info (dim)
    juce::String s;
    s << (currentProject.projectName.isEmpty() ? "Untitled" : currentProject.projectName);
    if (projectDirty) s << " *";
    s << "   Ch " << (activeChannel + 1);
    if (abIsShowingA && abChannelA == activeChannel) s << " [A]";
    if (!abIsShowingA && abChannelB == activeChannel) s << " [B]";
    s << "   " << (inputRouter.getMode() == InputRouter::Mode::Loop ? "Loop" : "Live");
    s << "   " << (parallelRouting ? "PAR" : "SGL");
    s << "   Trim " << juce::String ((int) inputTrimSlider.getValue()) << "dB";
    s << "   " << (int) tapTempo.getBPM() << " BPM";
    if (tapTempo.isClockRunning()) s << " [CLK]";
    if (noiseGate.enabled) s << "   Gate " << (int) noiseGate.thresholdDb << "dB";
    statusLabel.setText (s, juce::dontSendNotification);

    // Right side: operational state flags (color-coded)
    juce::String stateStr;
    juce::Colour stateColour (0xff888888);

    if (recorder.isRecording())
    {
        stateStr << "[REC]  ";
        stateColour = juce::Colour (0xffff4444);
    }
    if (tunerPanel.isVisible())
    {
        stateStr << "[TUNER/MUTED]  ";
        stateColour = juce::Colour (0xffff6644);
    }

    auto ls = looper.getState();
    if (ls == Looper::State::CountIn)
    {
        stateStr << "[LOOP COUNT-IN]";
        stateColour = juce::Colour (0xffffaa44);
    }
    else if (ls == Looper::State::Recording)
    {
        stateStr << "[LOOP REC]";
        stateColour = juce::Colour (0xffff4444);
    }
    else if (ls == Looper::State::Playing)
    {
        stateStr << "[LOOP PLAY]";
        if (stateColour == juce::Colour (0xff888888))
            stateColour = juce::Colour (0xffccaa44);
    }
    else if (ls == Looper::State::OverdubPending)
    {
        stateStr << "[LOOP DUB ARMED]";
        stateColour = juce::Colour (0xffaaaa44);
    }
    else if (ls == Looper::State::Overdubbing)
    {
        stateStr << "[LOOP DUB]";
        stateColour = juce::Colour (0xffccaa44);
    }

    if (metronome.isEnabled())
    {
        if (stateStr.isNotEmpty()) stateStr << "  ";
        stateStr << "[METRO]";
    }

    statusStateLabel.setText (stateStr.trim(), juce::dontSendNotification);
    statusStateLabel.setColour (juce::Label::textColourId, stateColour);

    updateActiveIndicators();
}

void MainComponent::showShortcutHelp()
{
    juce::String msg;
    msg << "KEYBOARD SHORTCUTS\n\n";
    msg << "1-4                  Switch channel\n";
    msg << "Numpad 1-8       Recall scene\n";
    msg << "Numpad 0           Tap tempo\n";
    msg << "Numpad 9, 1-4   Switch channel\n\n";
    msg << "Ctrl+S                Save project\n";
    msg << "Ctrl+O               Open project\n";
    msg << "Ctrl+N               New project\n";
    msg << "Ctrl+Z                Undo\n";
    msg << "Ctrl+Shift+Z       Redo\n\n";
    msg << "MOUSE\n\n";
    msg << "Channel label      Click: select | Dbl-click: rename | Drag: reorder\n";
    msg << "Fader dB label    Double-click to type exact value\n";
    msg << "Plugin slot           Click: bypass toggle | Dbl-click: open editor\n";
    msg << "Plugin slot           Right-click: tint, nickname, copy/paste, remove\n";
    msg << "Fader                   Right-click: color\n";
    msg << "Scene button       Right-click: save / rename / clear\n";
    msg << "Metronome btn   Right-click: settings\n\n";
    msg << "MIDI\n\n";
    msg << "PC 0-3                Switch channel\n";
    msg << "PC 4-11              Recall scene 1-8\n";
    msg << "PC 124 / 125     Recall A / B channel\n";
    msg << "PC 126 / 127     Setlist prev / next\n";

    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::InfoIcon, "UpStage Shortcuts", msg, "OK");
}

juce::File MainComponent::getAutosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("UpStage").getChildFile ("UpStage_autosave.upstage");
}

void MainComponent::autosave()
{
    auto file = getAutosaveFile();
    file.getParentDirectory().createDirectory();
    auto data = collectProjectData();
    projectState.saveToFile (file, data, &sceneManager, &midiLearnManager);
}

void MainComponent::checkAutosaveRecovery()
{
    auto autosaveFile = getAutosaveFile();
    if (! autosaveFile.existsAsFile())
        return;

    bool shouldRecover = false;

    if (currentProjectFile.existsAsFile())
    {
        auto autosaveTime = autosaveFile.getLastModificationTime();
        auto projectTime  = currentProjectFile.getLastModificationTime();
        shouldRecover = (autosaveTime > projectTime);
    }
    else
    {
        shouldRecover = true;
    }

    if (shouldRecover)
    {
        auto* aw = new juce::AlertWindow (
            "Recover Autosave?",
            "An autosave file was found that is newer than your last save.\n\n"
            "Would you like to recover it?",
            juce::AlertWindow::QuestionIcon);
        aw->addButton ("Recover", 1);
        aw->addButton ("Discard", 0);
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw, autosaveFile] (int result)
            {
                if (result == 1)
                {
                    ProjectData data;
                    if (projectState.loadFromFile (autosaveFile, data, &sceneManager, &midiLearnManager))
                        loadProjectData (data);
                }
                autosaveFile.deleteFile();
                delete aw;
            }), true);
    }
}

void MainComponent::updateFaderLabel (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= NUM_CHANNELS) return;

    float db = (float) outputFaders[channelIndex].getValue();
    juce::String text;

    if (db <= -59.9)
        text = "-INF";
    else
        text = juce::String (db, 1) + " dB";

    faderLevelLabels[channelIndex].setText (text, juce::dontSendNotification);
}

void MainComponent::updateActiveIndicators()
{
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        bool isActive = (i == activeChannel);
        bool isMuted  = channelMuted[i];

        // Text colour only — the LCD backing (background) is drawn in paint()
        // so it can render recessed scanlines. Outline stays transparent.
        channelLabels[i].setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        if (isMuted)
            channelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffcc6666));
        else if (isActive)
            channelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffcaffca));
        else
            channelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xff99bbaa));
    }
    repaint();
}

//==============================================================================
juce::File MainComponent::getSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("UpStage").getChildFile ("UpStage_settings.xml");
}

void MainComponent::saveAudioDeviceState()
{
    auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();

    auto settingsXml = std::make_unique<juce::XmlElement> ("UpStageSettings");

    if (auto deviceXml = deviceManager.createStateXml())
        settingsXml->addChildElement (deviceXml.release());

    if (auto* win = findParentComponentOfClass<juce::DocumentWindow>())
    {
        auto* boundsEl = settingsXml->createNewChildElement ("WindowBounds");
        boundsEl->setAttribute ("x",      win->getX());
        boundsEl->setAttribute ("y",      win->getY());
        boundsEl->setAttribute ("width",  win->getWidth());
        boundsEl->setAttribute ("height", win->getHeight());
    }

    settingsXml->writeTo (file);
}

void MainComponent::restoreAudioDeviceState()
{
    auto file = getSettingsFile();
    if (! file.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (auto* deviceXml = xml->getChildByName ("DEVICESETUP"))
            deviceManager.initialise (1, 2, deviceXml, true);

        if (auto* boundsEl = xml->getChildByName ("WindowBounds"))
        {
            if (auto* win = findParentComponentOfClass<juce::DocumentWindow>())
            {
                int x = boundsEl->getIntAttribute ("x", win->getX());
                int y = boundsEl->getIntAttribute ("y", win->getY());
                int w = boundsEl->getIntAttribute ("width", win->getWidth());
                int h = boundsEl->getIntAttribute ("height", win->getHeight());
                win->setBounds (x, y, w, h);
            }
        }
    }
}
