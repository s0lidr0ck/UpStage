#include "MainComponent.h"
#include "MixerLookAndFeel.h"
#include <windows.h>
#include <psapi.h>

//==============================================================================
MainComponent::MainComponent() : menuBar (this)
{
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

    // Register MIDI-learnable parameters
    midiLearnManager.addListener (this);
    midiLearnManager.registerParameter ("loopVolume",  0.0f,  1.0f);
    midiLearnManager.registerParameter ("gateThresh", -80.0f, 0.0f);
    midiLearnManager.registerParameter ("inputTrim",  -24.0f, 24.0f);

    //==========================================================================
    addAndMakeVisible (menuBar);

    // Transport buttons - console style
    for (auto* b : { &tapButton, &tunerButton, &panicButton,
                     &recordButton, &stopRecordButton,
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
    stopRecordButton.setComponentID ("icon_stop");
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

    stopRecordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a2a));

    metronomeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a3a));
    metronomeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9999cc));
    metronomeButton.addMouseListener (this, false);

    loopRecButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a2a));
    loopRecButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88cc88));

    routingModeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a3a));
    routingModeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88cccc));

    toolbarExpandButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252525));
    toolbarExpandButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff888888));

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
        sceneButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1a1a));
        sceneButtons[i].setColour (juce::TextButton::textColourOffId, juce::Colour (0xff555555));
        sceneButtons[i].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a5a2a));
        sceneButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff88ff88));
        sceneButtons[i].setTooltip ("Click: recall | Right-click: options");
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
        channelLabels[i].setFont (juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
        channelLabels[i].setJustificationType (juce::Justification::centred);
        channelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
        channelLabels[i].setColour (juce::Label::backgroundColourId, juce::Colour (0xff2a2a2a));
        channelLabels[i].setEditable (false, true, false); // single-click to activate, double-click to edit
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
        faderLevelLabels[i].setJustificationType (juce::Justification::centred);
        faderLevelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
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
        };
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
        };
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
    inputChannelLabel.setFont (juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
    inputChannelLabel.setJustificationType (juce::Justification::centred);
    inputChannelLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffaa66));
    inputChannelLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff3a2a20));
    inputChannelLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff4a3a28));
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
    };
    addAndMakeVisible (inputDirectKnob);

    // Input strip fader label
    inputStripFaderLabel.setText ("0.0 dB", juce::dontSendNotification);
    inputStripFaderLabel.setFont (juce::Font(juce::FontOptions().withHeight(11.0f)));
    inputStripFaderLabel.setJustificationType (juce::Justification::centred);
    inputStripFaderLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
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
    fxBusPanel->onMasterFaderChanged = [this] (float db) {
        masterOutputGain.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed);
    };
    addAndMakeVisible (*fxBusPanel);


    // Signal chain view removed - using mixer-style layout instead

    // Tuner - initially hidden
    tunerPanel.tunerActive = false;
    addAndMakeVisible (tunerPanel);
    tunerPanel.setVisible (false);

    // Status bar
    statusLabel.setFont (juce::Font(juce::FontOptions().withHeight(11.0f)));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    statusLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff181818));
    addAndMakeVisible (statusLabel);

    cpuLabel.setFont (juce::Font(juce::FontOptions().withHeight(10.0f)));
    cpuLabel.setColour (juce::Label::textColourId, juce::Colour (0xff88aa88));
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpuLabel);

    ramLabel.setFont (juce::Font(juce::FontOptions().withHeight(10.0f)));
    ramLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
    ramLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ramLabel);

    //==========================================================================
    setlistManager.onSongChanged = [this] (int, const ProjectData& data) {
        juce::MessageManager::callAsync ([this, data] { loadProjectData (data); });
    };

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

    auto midiDevices = juce::MidiInput::getAvailableDevices();
    if (! midiDevices.isEmpty())
    {
        deviceManager.addMidiInputDeviceCallback (midiDevices[0].identifier, this);
        deviceManager.setMidiInputDeviceEnabled  (midiDevices[0].identifier, true);
    }

    auto midiOutDevices = juce::MidiOutput::getAvailableDevices();
    if (! midiOutDevices.isEmpty())
        midiOutput = juce::MidiOutput::openDevice (midiOutDevices[0].identifier);

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
}

MainComponent::~MainComponent()
{
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

    inputChannel->prepare (sr, blockSize);

    for (auto& ch : channels)
        ch->prepare (sr, blockSize);

    fxBus     ->prepare (sr, blockSize);
    inputRouter.prepare (sr, blockSize);
    noiseGate  .prepare (sr, blockSize);
    metronome  .prepare (sr, blockSize);
    looper     .prepare (sr, blockSize);
    recorder   .prepare (sr, 2);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    auto* buffer = info.buffer;
    buffer->clear (info.startSample, info.numSamples);

    // ---- Build working stereo buffer ----
    juce::AudioBuffer<float> work (2, info.numSamples);
    work.clear();

    if (inputRouter.getMode() == InputRouter::Mode::Live)
    {
        if (buffer->getNumChannels() > 0)
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

    // ---- Measure input level (before FX) ----
    inputLevelInL.store (work.getRMSLevel (0, 0, info.numSamples), std::memory_order_relaxed);
    inputLevelInR.store (work.getRMSLevel (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Input channel pre-FX ----
    juce::MidiBuffer inputMidi; // Input channel gets MIDI too
    inputChannel->processBlock (work, inputMidi);

    // ---- Measure output level (after FX) ----
    inputLevelOutL.store (work.getRMSLevel (0, 0, info.numSamples), std::memory_order_relaxed);
    inputLevelOutR.store (work.getRMSLevel (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Save post-input-FX signal for direct mix ----
    juce::AudioBuffer<float> directSignal;
    float directGain = inputDirectLevel.load (std::memory_order_relaxed);
    if (directGain > 0.001f)
        directSignal.makeCopyOf (work);

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
                    ChannelStrip* ptrs[NUM_CHANNELS];
                    for (int i = 0; i < NUM_CHANNELS; ++i) ptrs[i] = channels[i].get();
                    if (sceneManager.applyScene (pc - NUM_CHANNELS, ptrs))
                    {
                        activeSceneIndex = pc - NUM_CHANNELS;
                        for (int i = 0; i < NUM_CHANNELS; ++i)
                        {
                            float outDb = juce::Decibels::gainToDecibels (channels[i]->getOutputGain());
                            float inDb  = juce::Decibels::gainToDecibels (channels[i]->getInputGain());
                            outputFaders[i].setValue (outDb, juce::dontSendNotification);
                            outputGainKnobs[i].setValue (channels[i]->getPan(), juce::dontSendNotification);
                            inputTrimKnobs[i].setValue (inDb, juce::dontSendNotification);
                            updateFaderLabel (i);
                            channelStripPanels[i]->refresh();
                        }
                    }
                    midiLearnManager.setParameterTarget ("loopVolume",
                        (float) loopVolumeSlider.getValue());
                    midiLearnManager.setParameterTarget ("gateThresh",
                        noiseGate.thresholdDb);
                    midiLearnManager.setParameterTarget ("inputTrim",
                        (float) inputTrimSlider.getValue());
                    updateSceneButtonStates();
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

    // ---- Channel processing ----
    juce::AudioBuffer<float> channelOutputs[NUM_CHANNELS];
    juce::AudioBuffer<float> masterMix (2, info.numSamples);
    masterMix.clear();

    if (! outputMuted)
    {
        bool anySoloed = false;
        for (int i = 0; i < NUM_CHANNELS; ++i)
            if (channelSoloed[i]) { anySoloed = true; break; }

        for (int i = 0; i < NUM_CHANNELS; ++i)
        {
            channelOutputs[i].setSize (2, info.numSamples, false, false, true);

            bool shouldProcess = parallelRouting || (i == activeChannel);

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
            channelInputLevelL[i].store (channelOutputs[i].getRMSLevel (0, 0, info.numSamples), std::memory_order_relaxed);
            channelInputLevelR[i].store (channelOutputs[i].getRMSLevel (1, 0, info.numSamples), std::memory_order_relaxed);

            if (shouldProcess)
                channels[i]->processBlock (channelOutputs[i], midi);

            // Solo/mute logic
            bool shouldMix = anySoloed ? channelSoloed[i] : !channelMuted[i];

            if (shouldMix && shouldProcess)
            {
                for (int ch = 0; ch < 2; ++ch)
                    masterMix.addFrom (ch, 0, channelOutputs[i], ch, 0, info.numSamples);
            }
        }

        // Mix in direct signal from input channel
        if (directGain > 0.001f && directSignal.getNumSamples() == info.numSamples)
        {
            for (int ch = 0; ch < 2; ++ch)
                masterMix.addFrom (ch, 0, directSignal, ch, 0, info.numSamples, directGain);
        }

        work.makeCopyOf (masterMix);
    }
    else
    {
        work.clear();
    }

    // ---- Master input level (before FX bus) ----
    masterLevelInL.store (work.getRMSLevel (0, 0, info.numSamples), std::memory_order_relaxed);
    masterLevelInR.store (work.getRMSLevel (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Master insert chain ----
    fxBus->processBlock (work, info.numSamples);

    // ---- Master output level (after FX bus) ----
    masterLevelOutL.store (work.getRMSLevel (0, 0, info.numSamples), std::memory_order_relaxed);
    masterLevelOutR.store (work.getRMSLevel (1, 0, info.numSamples), std::memory_order_relaxed);

    // ---- Channel level meters ----
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        channelLevelL[i].store (channelOutputs[i].getRMSLevel (0, 0, info.numSamples), std::memory_order_relaxed);
        channelLevelR[i].store (channelOutputs[i].getRMSLevel (1, 0, info.numSamples), std::memory_order_relaxed);
    }

    // ---- Metronome (mixed post-FX) ----
    metronome.processBlock (work);

    // ---- Looper (captures and plays back master output) ----
    looper.processBlock (work);

    // ---- Wet capture ----
    recorder.writeOutputBlock (work);

    // ---- Master output gain (fader) ----
    float mGain = masterOutputGain.load (std::memory_order_relaxed);
    if (std::abs (mGain - 1.0f) > 0.0001f)
        work.applyGain (0, info.numSamples, mGain);

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
void MainComponent::handleIncomingMidiMessage (juce::MidiInput*,
                                               const juce::MidiMessage& msg)
{
    juce::ScopedLock sl (midiLock);
    pendingMidi.addEvent (msg, 0);
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
        for (int ty = 0; ty < getHeight(); ty += 2)
        {
            float noiseAlpha = 0.015f + rng.nextFloat() * 0.015f;
            g.setColour (juce::Colours::white.withAlpha (noiseAlpha));
            g.drawHorizontalLine (ty, 0.0f, w);
        }
        g.restoreState();
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
    drawGroove (76);
    drawGroove (104);

    // ---- Toolbar group dividers (vertical) ----
    {
        auto drawVDiv = [&] (int x, int top, int bottom)
        {
            g.setColour (juce::Colour (0xff0a0908));
            g.drawVerticalLine (x, (float) top, (float) bottom);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawVerticalLine (x + 1, (float) top, (float) bottom);
        };

        int tTop = 32, tBot = 72;

        int d1x = (panicButton.getRight() + recordButton.getX()) / 2;
        int d2x = (stopRecordButton.getRight() + metronomeButton.getX()) / 2;
        int d3x = (routingModeButton.getRight() + bpmLabel.getX()) / 2;

        drawVDiv (d1x, tTop, tBot);
        drawVDiv (d2x, tTop, tBot);
        drawVDiv (d3x, tTop, tBot);
    }

    // ---- Channel strip backgrounds ----
    const int mixerTop = 104;
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
            setActiveChannel (i);
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
                    ChannelStrip* ptrs[NUM_CHANNELS];
                    for (int c = 0; c < NUM_CHANNELS; ++c) ptrs[c] = channels[c].get();
                    sceneManager.captureScene (i, ptrs);
                    updateSceneButtonStates();
                    projectDirty = true;
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

    // Edit mode: right-click knobs to change color
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

    // Right-click on metronome button — open settings
    if (e.mods.isRightButtonDown() && e.eventComponent == &metronomeButton)
    {
        showMetronomeSettings();
        return;
    }
}

void MainComponent::showMetronomeSettings()
{
    juce::PopupMenu menu;

    // BPM
    juce::PopupMenu bpmMenu;
    for (int b : { 60, 80, 90, 100, 110, 120, 130, 140, 150, 160, 180, 200 })
        bpmMenu.addItem (1000 + b, juce::String (b) + " BPM",
                         true, (int) metronome.getBPM() == b);
    menu.addSubMenu ("BPM", bpmMenu);

    // Time signature
    juce::PopupMenu meterMenu;
    int curNum = metronome.getNumerator();
    int curDen = metronome.getDenominator();
    for (auto& ts : std::vector<std::pair<int,int>>{ {2,4}, {3,4}, {4,4}, {5,4}, {6,8}, {7,8}, {12,8} })
    {
        juce::String label = juce::String (ts.first) + "/" + juce::String (ts.second);
        meterMenu.addItem (2000 + ts.first * 100 + ts.second, label,
                           true, ts.first == curNum && ts.second == curDen);
    }
    menu.addSubMenu ("Meter", meterMenu);

    // Subdivision
    juce::PopupMenu subMenu;
    int curSub = metronome.getSubdivision();
    subMenu.addItem (3001, "None (quarter)",   true, curSub == 1);
    subMenu.addItem (3002, "8th notes",        true, curSub == 2);
    subMenu.addItem (3003, "8th triplets",     true, curSub == 3);
    subMenu.addItem (3004, "16th notes",       true, curSub == 4);
    menu.addSubMenu ("Subdivision", subMenu);

    menu.addSeparator();

    // Sound
    juce::PopupMenu soundMenu;
    auto curSound = metronome.getClickSound();
    soundMenu.addItem (4001, "Sine (clean)",   true, curSound == Metronome::ClickSound::Sine);
    soundMenu.addItem (4002, "Tick (snappy)",  true, curSound == Metronome::ClickSound::Tick);
    soundMenu.addItem (4003, "Woodblock",      true, curSound == Metronome::ClickSound::Woodblock);
    menu.addSubMenu ("Sound", soundMenu);

    // Accent
    menu.addItem (5001, metronome.isAccentEnabled() ? "Accent: ON" : "Accent: OFF");

    // Accent frequency
    juce::PopupMenu accentFreqMenu;
    int curAF = (int) metronome.getAccentFreq();
    for (int f : { 1000, 1200, 1500, 1800, 2000, 2500 })
        accentFreqMenu.addItem (6000 + f, juce::String (f) + " Hz", true, f == curAF);
    menu.addSubMenu ("Accent Pitch", accentFreqMenu);

    // Normal frequency
    juce::PopupMenu normFreqMenu;
    int curNF = (int) metronome.getNormalFreq();
    for (int f : { 600, 800, 1000, 1200, 1500 })
        normFreqMenu.addItem (7000 + f, juce::String (f) + " Hz", true, f == curNF);
    menu.addSubMenu ("Click Pitch", normFreqMenu);

    menu.addSeparator();

    // Volume
    juce::PopupMenu volMenu;
    int curVol = (int)(metronome.getVolume() * 100.0f);
    for (int v : { 25, 50, 75, 100 })
        volMenu.addItem (8000 + v, juce::String (v) + "%", true, std::abs (curVol - v) < 5);
    menu.addSubMenu ("Volume", volMenu);

    menu.showMenuAsync ({}, [this] (int result)
    {
        if (result <= 0) return;

        if (result >= 1000 && result < 2000)
        {
            int bpm = result - 1000;
            metronome.setBPM ((double) bpm);
            tapTempo.setBPM ((double) bpm);
        }
        else if (result >= 2000 && result < 3000)
        {
            int encoded = result - 2000;
            int num = encoded / 100;
            int den = encoded % 100;
            metronome.setTimeSignature (num, den);
        }
        else if (result >= 3001 && result <= 3004)
        {
            metronome.setSubdivision (result - 3000);
        }
        else if (result >= 4001 && result <= 4003)
        {
            metronome.setClickSound (static_cast<Metronome::ClickSound> (result - 4001));
        }
        else if (result == 5001)
        {
            metronome.setAccentEnabled (! metronome.isAccentEnabled());
        }
        else if (result >= 6000 && result < 7000)
        {
            metronome.setAccentFreq ((double)(result - 6000));
        }
        else if (result >= 7000 && result < 8000)
        {
            metronome.setNormalFreq ((double)(result - 7000));
        }
        else if (result >= 8000 && result < 9000)
        {
            metronome.setVolume ((float)(result - 8000) / 100.0f);
        }
    });
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

    // Ctrl+Z = Undo
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            undoManager.redo();
        else
            undoManager.undo();
        return true;
    }

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

    // Numpad 1-8 = Scene recall (with + held: switch channel 1-4 instead)
    for (int n = 1; n <= 8; ++n)
    {
        if (key.getKeyCode() == juce::KeyPress::numberPad0 + n)
        {
            if (numpadPlusHeld && n >= 1 && n <= NUM_CHANNELS)
            {
                setActiveChannel (n - 1);
            }
            else
            {
                int sceneIdx = n - 1;
                ChannelStrip* ptrs[NUM_CHANNELS];
                for (int c = 0; c < NUM_CHANNELS; ++c) ptrs[c] = channels[c].get();
                if (sceneManager.applyScene (sceneIdx, ptrs))
                {
                    activeSceneIndex = sceneIdx;
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
                    updateSceneButtonStates();
                }
            }
            return true;
        }
    }

    // Numpad + held flag
    if (key.getKeyCode() == juce::KeyPress::numberPadAdd)
    {
        numpadPlusHeld = true;
        return true;
    }

    return false;
}

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::numberPadAdd))
        numpadPlusHeld = false;
    return false;
}

void MainComponent::showKnobColorMenu (juce::Component* knob)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Grey");
    menu.addItem (2, "Blue");
    menu.addItem (3, "Green");
    menu.addItem (4, "Purple");
    menu.addItem (5, "Red");
    menu.addItem (6, "Royal Blue");
    menu.addItem (7, "Teal");

    menu.showMenuAsync ({}, [this, knob] (int result)
    {
        if (result <= 0) return;

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

    // Transport row - cassette player style
    auto transport = area.removeFromTop (48);
    transport.reduce (8, 6);

    int bw = toolbarLabelsVisible ? 55 : 36;
    int bwNarrow = toolbarLabelsVisible ? 45 : 36;

    // Expand toggle - far left
    toolbarExpandButton.setBounds (transport.removeFromLeft (24));
    transport.removeFromLeft (5);

    // Tuner and Panic
    tunerButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (3);
    panicButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (8);

    // Transport: Rec | Play | Stop - cassette style
    recordButton.setBounds (transport.removeFromLeft (bwNarrow));
    transport.removeFromLeft (3);
    playLoopButton.setBounds (transport.removeFromLeft (bwNarrow));
    transport.removeFromLeft (3);
    stopRecordButton.setBounds (transport.removeFromLeft (bwNarrow));
    transport.removeFromLeft (8);

    // Metronome, Looper, and Routing
    metronomeButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (3);
    loopRecButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (3);
    routingModeButton.setBounds (transport.removeFromLeft (bwNarrow));

    // Tap tempo - far right
    tapButton.setBounds (transport.removeFromRight (bwNarrow));
    transport.removeFromRight (3);
    bpmLabel.setBounds (transport.removeFromRight (65));

    // Scenes row - all 8 scenes, smaller to fit
    auto scenesRow = area.removeFromTop (28);
    scenesRow.reduce (6, 3);
    int sceneWidth = (scenesRow.getWidth() - (NUM_SCENES - 1) * 3) / NUM_SCENES;
    for (int i = 0; i < NUM_SCENES; ++i)
    {
        sceneButtons[i].setBounds (scenesRow.removeFromLeft (sceneWidth));
        if (i < NUM_SCENES - 1)
            scenesRow.removeFromLeft (3);
    }

    // Status bar at bottom
    auto statusArea = area.removeFromBottom (24);
    ramLabel.setBounds (statusArea.removeFromRight (90).reduced (4, 2));
    cpuLabel.setBounds (statusArea.removeFromRight (90).reduced (4, 2));
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
        auto strip = area.removeFromLeft (stripWidth).reduced (stripPadding);

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
        inputRouter.setLoopPlaying (! inputRouter.isLoopPlaying());
        playLoopButton.setButtonText (inputRouter.isLoopPlaying() ? "Pause" : "Play");
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
        tunerButton.setButtonText (showing ? "Tuner" : "Tuner ON");
        resized();
    }

    // Panic
    else if (b == &panicButton) { sendMidiPanic(); }

    // Gate toggle
    else if (b == &gateToggle) { noiseGate.enabled = gateToggle.getToggleState(); }

    // Record
    else if (b == &recordButton)
    {
        Recorder::Mode mode = Recorder::Mode::Both;

        juce::File folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                .getChildFile ("UpStage Recordings");
        if (currentProject.recordOutputFolder.isNotEmpty())
            folder = juce::File (currentProject.recordOutputFolder);

        recorder.startRecording (folder, mode);
        recordButton    .setEnabled (false);
        stopRecordButton.setEnabled (true);
    }
    else if (b == &stopRecordButton)
    {
        recorder.stopRecording();
        recordButton    .setEnabled (true);
        stopRecordButton.setEnabled (false);

        if (looper.getState() != Looper::State::Idle)
        {
            looper.stop();
            loopRecButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a2a));
        }
    }

    // Metronome
    else if (b == &metronomeButton)
    {
        metronome.setEnabled (! metronome.isEnabled());
        if (metronome.isEnabled())
            metronome.setBPM (tapTempo.getBPM());
        metronomeButton.setColour (juce::TextButton::buttonColourId,
            metronome.isEnabled() ? juce::Colour (0xff4444aa) : juce::Colour (0xff2a2a3a));
    }

    // Looper
    else if (b == &loopRecButton)
    {
        looper.toggleRecord();
        auto ls = looper.getState();
        if (ls == Looper::State::Recording)
            loopRecButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff882222));
        else if (ls == Looper::State::Playing)
            loopRecButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff228822));
        else if (ls == Looper::State::Overdubbing)
            loopRecButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff886622));
        else
            loopRecButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a2a));
    }

    // Routing mode
    else if (b == &routingModeButton)
    {
        parallelRouting = ! parallelRouting;
        routingModeButton.setButtonText (parallelRouting ? ">>" : ">");
        routingModeButton.setColour (juce::TextButton::buttonColourId,
            parallelRouting ? juce::Colour (0xff2a3a3a) : juce::Colour (0xff3a3a2a));
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

    // ASIO / Setlist
    else if (b == &asioButton)    { openAsioSettings(); }
    else if (b == &setlistButton) { showSetlistPanel(); }

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
            ChannelStrip* ptrs[NUM_CHANNELS];
            for (int c = 0; c < NUM_CHANNELS; ++c) ptrs[c] = channels[c].get();
            if (! sceneManager.applyScene (i, ptrs))
                return;

            activeSceneIndex = i;

            // Sync UI to new channel states
            for (int c = 0; c < NUM_CHANNELS; ++c)
            {
                float outGainDb = juce::Decibels::gainToDecibels (channels[c]->getOutputGain());
                float inGainDb  = juce::Decibels::gainToDecibels (channels[c]->getInputGain());
                outputFaders[c].setValue (outGainDb, juce::dontSendNotification);
                outputGainKnobs[c].setValue (channels[c]->getPan(), juce::dontSendNotification);
                inputTrimKnobs[c].setValue (inGainDb, juce::dontSendNotification);
                updateFaderLabel (c);
                channelStripPanels[c]->refresh();
            }

            // Arm soft takeover after scene recall
            midiLearnManager.setParameterTarget ("loopVolume",
                (float) loopVolumeSlider.getValue());
            midiLearnManager.setParameterTarget ("gateThresh",
                noiseGate.thresholdDb);
            midiLearnManager.setParameterTarget ("inputTrim",
                (float) inputTrimSlider.getValue());
            updateSceneButtonStates();
            return;
        }
        if (b == &saveSceneButtons[i])
        {
            ChannelStrip* ptrs[NUM_CHANNELS];
            for (int c = 0; c < NUM_CHANNELS; ++c) ptrs[c] = channels[c].get();
            sceneManager.captureScene (i, ptrs);
            updateSceneButtonStates();
            projectDirty = true;
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

    // Update regular channel meters
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        levelMeters[i]->pushLevel (channelLevelL[i].load(), channelLevelR[i].load());
        inputLevelMeters[i]->pushLevel (channelInputLevelL[i].load(), channelInputLevelR[i].load());
    }

    // Update master bus meters
    if (fxBusPanel)
        fxBusPanel->pushMeterLevels (masterLevelInL.load(), masterLevelInR.load(),
                                     masterLevelOutL.load(), masterLevelOutR.load());

    // Metronome beat flash
    if (metronome.isEnabled() && metronome.consumeBeatFlash())
    {
        metronomeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff6666cc));
        metroFlashCounter = 4;
    }
    else if (metroFlashCounter > 0)
    {
        if (--metroFlashCounter == 0)
            metronomeButton.setColour (juce::TextButton::buttonColourId,
                metronome.isEnabled() ? juce::Colour (0xff4444aa) : juce::Colour (0xff2a2a3a));
    }

    // Re-layout if FX bus panel height changed
    if (fxBusPanel)
    {
        static int lastFxBusH = 0;
        int curH = fxBusPanel->getPreferredHeight();
        if (curH != lastFxBusH) { lastFxBusH = curH; resized(); }
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
        // MidiLearnManager normalises to 0–1; map back to -24…+24 dB
        const float db = juce::jmap (value, 0.0f, 1.0f, -24.0f, 24.0f);
        inputTrimSlider.setValue (db, juce::dontSendNotification);
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
        case MenuSetlist:      showSetlistPanel(); break;
        case MenuEditUIColors: uiEditMode = !uiEditMode; repaint(); break;
        case MenuQuit:         juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        default:
            if (id >= MenuRecentBase && id < MenuRecentBase + 10)
                openRecentProject (id - MenuRecentBase);
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
                if (projectState.loadFromFile (result, data))
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
    if (projectState.saveToFile (currentProjectFile, data))
    {
        projectDirty = false;
        saveLastProjectPath();
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
                if (projectState.saveToFile (file, data))
                {
                    currentProjectFile = file;
                    projectDirty = false;
                    saveLastProjectPath();
                }
                updateStatusBar();
            }
        });
}

void MainComponent::loadProjectData (const ProjectData& data)
{
    currentProject = data;
    setActiveChannel (data.activeChannel);
    midiTranslator.setRules (data.midiRules);
    tapTempo.setBPM (data.tapTempoBPM);

    noiseGate.enabled     = data.gateEnabled;
    noiseGate.thresholdDb = data.gateThreshDb;
    gateToggle      .setToggleState (data.gateEnabled,   juce::dontSendNotification);
    gateThreshSlider.setValue       (data.gateThreshDb,  juce::dontSendNotification);

    inputTrimSlider.setValue (data.inputTrimDb, juce::dontSendNotification);

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

    // Load channel plugins
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        channels[i]->setActive     (i == data.activeChannel);
        channels[i]->setName       (data.channels[i].name);
        channels[i]->setInputGain  (data.channels[i].inputGain);
        channels[i]->setOutputGain (data.channels[i].outputGain);
        channels[i]->setPan        (data.channels[i].pan);
        outputGainKnobs[i].setValue (data.channels[i].pan, juce::dontSendNotification);

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
                if (! ok) return;
                if (stateBlob.getSize() > 0)
                    if (auto* proc = channels[chanIdx]->getPlugin (slotIdx))
                        proc->setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
                channels[chanIdx]->setPluginBypassed (slotIdx, bypassed);
                juce::MessageManager::callAsync ([this, chanIdx] {
                    channelStripPanels[chanIdx]->refresh();
                });
            });
        }
    }

    // Restore input channel plugins
    inputChannel->setInputGain (data.inputChannelState.inputGain);
    inputChannel->setOutputGain (data.inputChannelState.outputGain);
    inputDirectLevel.store (data.inputDirectMix, std::memory_order_relaxed);
    inputDirectKnob.setValue (data.inputDirectMix, juce::dontSendNotification);
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
            if (! ok) return;
            if (stateBlob.getSize() > 0)
                if (auto* proc = inputChannel->getPlugin (slotIdx))
                    proc->setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
            inputChannel->setPluginBypassed (slotIdx, bypassed);
            juce::MessageManager::callAsync ([this] { inputChannelPanel->refresh(); });
        });
    }

    // Restore master insert chain state
    {
        FxBus::State fbs;
        fbs.bypassed = data.fxBusState.bypassed;
        fxBus->setState (fbs);
    }
    fxBusPanel->syncFromBus();

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
            if (! ok) return;
            if (stateBlob.getSize() > 0)
                if (auto* proc = fxBus->getPlugin (slotIdx))
                    proc->setStateInformation (stateBlob.getData(), (int) stateBlob.getSize());
            fxBus->setPluginBypassed (slotIdx, bypassed);
            juce::MessageManager::callAsync ([this] { fxBusPanel->refresh(); });
        });
    }

    projectDirty = false;
    updateActiveIndicators();
    updateSceneButtonStates();
    updateTransportUI();
    updateStatusBar();
}

ProjectData MainComponent::collectProjectData() const
{
    ProjectData data = currentProject;
    data.activeChannel  = activeChannel;
    data.inputTrimDb    = (float) inputTrimSlider.getValue();
    for (int i = 0; i < NUM_CHANNELS; ++i)
        data.channels[i] = channels[i]->getState();
    data.inputChannelState = inputChannel->getState();
    data.inputDirectMix  = inputDirectLevel.load();
    data.midiRules       = midiTranslator.getRules();
    data.useLoopFile     = (inputRouter.getMode() == InputRouter::Mode::Loop);
    data.loopFilePath    = inputRouter.getLoopFileName();
    data.tapTempoBPM     = tapTempo.getBPM();
    data.gateEnabled     = noiseGate.enabled;
    data.gateThreshDb    = noiseGate.thresholdDb;

    // Master insert chain state
    FxBus::State fbs = fxBus->getState();
    data.fxBusState.bypassed = fbs.bypassed;
    data.fxBusState.plugins  = fbs.plugins;

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
        if (projectState.loadFromFile (file, data))
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
        channels[i]->setActive (i == idx);
    updateActiveIndicators();
    updateStatusBar();  // This will update the signal chain view too
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
        ProjectData data;
        if (setlistManager.loadSongAtIndex (idx, data))
            loadProjectData (data);
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel);
    opts.dialogTitle            = "Setlist";
    opts.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    opts.useNativeTitleBar      = true;
    opts.resizable              = true;
    opts.launchAsync();
}

//==============================================================================
// Removed - replaced by updateActiveIndicators()

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
    if (noiseGate.enabled)   s << "   Gate " << (int) noiseGate.thresholdDb << "dB";
    if (metronome.isEnabled()) s << "   [METRO]";
    if (looper.getState() == Looper::State::Recording)   s << "   [LOOP REC]";
    if (looper.getState() == Looper::State::Playing)     s << "   [LOOP PLAY]";
    if (looper.getState() == Looper::State::Overdubbing) s << "   [LOOP DUB]";
    if (recorder.isRecording()) s << "   [REC]";
    if (tunerPanel.isVisible()) s << "   [TUNER/MUTED]";
    statusLabel.setText (s, juce::dontSendNotification);

    // Signal chain view removed - using mixer-style layout

    updateActiveIndicators();
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

        if (isActive)
        {
            // Active channel - green background with brighter text
            channelLabels[i].setColour (juce::Label::backgroundColourId, juce::Colour (0xff2d5030));
            channelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xffbbffbb));
            channelLabels[i].setColour (juce::Label::outlineColourId, juce::Colour (0xff3a7040));
        }
        else
        {
            // Inactive - normal gray
            channelLabels[i].setColour (juce::Label::backgroundColourId, juce::Colour (0xff2a2a2a));
            channelLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xff888888));
            channelLabels[i].setColour (juce::Label::outlineColourId, juce::Colour (0xff1a1a1a));

            channelLabels[i].setColour (juce::Label::backgroundColourId, juce::Colour (0xff222222));
        }
    }
    repaint();
}
