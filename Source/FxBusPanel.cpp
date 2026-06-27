#include "FxBusPanel.h"

//==============================================================================
FxBusPanel::FxBusPanel (FxBus& b)
    : bus (b)
{
    headerLabel.setText ("MASTER", juce::dontSendNotification);
    headerLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
    headerLabel.setJustificationType (juce::Justification::centred);
    headerLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffcc99));
    // Transparent background: the accent header band drawn in paint() shows through (#8).
    headerLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (headerLabel);

    bypassToggle.setButtonText ("FX BYPASS");
    bypassToggle.onStateChange = [this] { bus.setBypassed (bypassToggle.getToggleState()); };
    bypassToggle.setToggleState (bus.isBypassed(), juce::dontSendNotification);
    bypassToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (0xff99887a));
    bypassToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffffaa44));
    bypassToggle.setTooltip ("Bypass the entire master insert chain.");
    addAndMakeVisible (bypassToggle);

    addFxButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2220));
    addFxButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88775a));
    addFxButton.onClick = [this] { if (onAddPluginClicked) onAddPluginClicked(); };
    addAndMakeVisible (addFxButton);

    // Master knob (replaces fader)
    masterKnob.setName ("master_knob");
    masterKnob.setComponentID ("master_knob");
    masterKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterKnob.setRange (-60.0, 12.0, 0.1);
    masterKnob.setValue (0.0);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterKnob.setDoubleClickReturnValue (true, 0.0);
    masterKnob.addListener (this);
    addAndMakeVisible (masterKnob);

    masterKnobCaption.setText ("VOLUME", juce::dontSendNotification);
    masterKnobCaption.setFont (juce::Font (juce::FontOptions().withHeight (10.0f).withStyle ("Bold")));
    masterKnobCaption.setJustificationType (juce::Justification::centred);
    masterKnobCaption.setColour (juce::Label::textColourId, juce::Colour (0xff998866));
    addAndMakeVisible (masterKnobCaption);

    masterKnobLabel.setText ("0.0 dB", juce::dontSendNotification);
    masterKnobLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    masterKnobLabel.setJustificationType (juce::Justification::centred);
    masterKnobLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    masterKnobLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1a1a1a));
    masterKnobLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff333333));
    masterKnobLabel.setEditable (false, true, false);
    masterKnobLabel.onTextChange = [this] {
        auto text = masterKnobLabel.getText().trimCharactersAtEnd (" dB").trim();
        double db = juce::jlimit (-60.0, 12.0, text.getDoubleValue());
        masterKnob.setValue (db);
    };
    addAndMakeVisible (masterKnobLabel);

    vuMeterIn.setLabel ("IN");
    vuMeterIn.setMeterLabel ("VU");
    vuMeterOut.setLabel ("OUT");
    vuMeterOut.setMeterLabel ("VU");
    peakMeter.setLabel ("PEAK");
    peakMeter.setMeterLabel ("PEAK");
    peakMeter.setFastMode (true);
    lufsMeter.setLabel ("LUFS");
    lufsMeter.setMeterLabel ("LUFS");

    addAndMakeVisible (vuMeterIn);
    addAndMakeVisible (vuMeterOut);
    addAndMakeVisible (peakMeter);
    addAndMakeVisible (lufsMeter);
    addAndMakeVisible (spreadMeter);
    addAndMakeVisible (goniometer);

    rebuildSlots();
}

FxBusPanel::~FxBusPanel()
{
}

//==============================================================================
void FxBusPanel::rebuildSlots()
{
    int numPlugins = bus.getNumPlugins();

    for (int i = 0; i < FxBus::MAX_FX_SLOTS; ++i)
    {
        slots[i].index = i;
        if (i < numPlugins)
        {
            auto* proc = bus.getPlugin (i);
            slots[i].name = (proc != nullptr) ? proc->getName() : "(unknown)";
            slots[i].bypassed = bus.isPluginBypassed (i);
            slots[i].empty = false;

            auto it = pluginAppearance.find (slots[i].name);
            slots[i].nickname = (it != pluginAppearance.end()) ? it->second.nickname : juce::String();
        }
        else
        {
            slots[i].name = "";
            slots[i].nickname = "";
            slots[i].bypassed = false;
            slots[i].empty = true;
        }
    }
}

void FxBusPanel::refresh()
{
    rebuildSlots();
    repaint();
}

void FxBusPanel::syncFromBus()
{
    bypassToggle.setToggleState (bus.isBypassed(), juce::dontSendNotification);
    rebuildSlots();
}

int FxBusPanel::getPreferredHeight() const
{
    return 600;
}

//==============================================================================
void FxBusPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg (
        juce::Colour (0xff342e28), bounds.getCentreX(), bounds.getY(),
        juce::Colour (0xff241e1a), bounds.getCentreX(), bounds.getBottom(), false);
    bg.addColour (0.3, juce::Colour (0xff2e2822));
    g.setGradientFill (bg);
    g.fillAll();

    juce::Random rng (99);
    for (int ty = 0; ty < getHeight(); ty += 2)
    {
        float noiseAlpha = 0.01f + rng.nextFloat() * 0.012f;
        g.setColour (juce::Colours::white.withAlpha (noiseAlpha));
        g.drawHorizontalLine (ty, 0.0f, bounds.getWidth());
    }

    g.setColour (juce::Colour (0xff060504));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawVerticalLine (1, 0.0f, (float) getHeight());

    // Master-section prominence (#8): brighter bezel + accent header band so the
    // master strip reads as a distinct section, not "channel 5".
    {
        auto full = bounds;
        // Accent band behind the MASTER header label.
        auto band = juce::Rectangle<float> (3.0f, 2.0f,
                                            full.getWidth() - 6.0f,
                                            (float) headerLabel.getBottom());
        juce::ColourGradient hg (
            juce::Colour (0xff4a3a5a), band.getX(), band.getY(),
            juce::Colour (0xff2a2230), band.getX(), band.getBottom(), false);
        g.setGradientFill (hg);
        g.fillRoundedRectangle (band, 3.0f);

        // Brighter metallic frame around the whole strip.
        g.setColour (juce::Colour (0xff4a4452));
        g.drawRoundedRectangle (full.reduced (1.5f), 5.0f, 1.5f);
    }

    int slotStartY = headerLabel.getBottom() + 4;

    for (int i = 0; i < FxBus::MAX_FX_SLOTS; ++i)
    {
        int sy = slotStartY + i * (kSlotHeight + kSlotPadding);
        auto slotRect = juce::Rectangle<int> (6, sy, getWidth() - 12, kSlotHeight);

        if (slots[i].empty)
        {
            g.setColour (juce::Colour (0xff1a1816));
            g.fillRoundedRectangle (slotRect.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xff3a3530));
            g.drawRoundedRectangle (slotRect.toFloat(), 3.0f, 1.0f);
            g.setColour (juce::Colour (0xff4a4540));
            g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
            g.drawText ("Insert " + juce::String (i + 1), slotRect, juce::Justification::centred);
        }
        else
        {
            g.setColour (juce::Colour (0xff252220));
            g.fillRoundedRectangle (slotRect.toFloat(), 3.0f);

            auto dotRect = slotRect.removeFromLeft (16);
            auto dotCentre = dotRect.getCentre().toFloat();
            g.setColour (slots[i].bypassed ? juce::Colour (0xff555555) : juce::Colour (0xff27ae60));
            g.fillEllipse (dotCentre.x - 3.0f, dotCentre.y - 3.0f, 6.0f, 6.0f);

            g.setColour (slots[i].bypassed ? juce::Colour (0xff777777) : juce::Colour (0xffcccccc));
            g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            auto displayName = slots[i].nickname.isNotEmpty() ? slots[i].nickname : slots[i].name;
            g.drawText (displayName, slotRect.reduced (4, 0), juce::Justification::centredLeft, true);
        }
    }

    // ---- Separator grooves above and below the insert-slot bank ----
    {
        auto groove = [&g, this] (int gy)
        {
            g.setColour (juce::Colour (0xff0a0908));
            g.drawHorizontalLine (gy, 4.0f, (float) getWidth() - 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawHorizontalLine (gy + 1, 4.0f, (float) getWidth() - 4.0f);
        };
        groove (slotStartY - 3);
        groove (slotStartY + FxBus::MAX_FX_SLOTS * (kSlotHeight + kSlotPadding) + 1);
    }

    // ---- Master VOLUME knob: its own bolted sub-plate with corner screws ----
    {
        auto kb = masterKnob.getBounds().toFloat();
        // Plate spans the knob column, from just under the dB readout to the
        // bottom of the strip; gives the knob a distinct "separate module" feel.
        auto plate = juce::Rectangle<float> (4.0f, masterKnobCaption.getY() - 4.0f,
                                             getWidth() - 8.0f,
                                             (float) getHeight() - (masterKnobCaption.getY() - 4.0f) - 4.0f);

        // Recessed dark border, then a raised brushed-metal face.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (plate.expanded (1.0f), 6.0f);
        juce::ColourGradient pg (juce::Colour (0xff3c3833), plate.getCentreX(), plate.getY(),
                                 juce::Colour (0xff262320), plate.getCentreX(), plate.getBottom(), false);
        pg.addColour (0.5, juce::Colour (0xff322e2a));
        g.setGradientFill (pg);
        g.fillRoundedRectangle (plate, 5.0f);
        // Top highlight + frame for the raised look.
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) plate.getY() + 1, plate.getX() + 4.0f, plate.getRight() - 4.0f);
        g.setColour (juce::Colour (0xff4a4640));
        g.drawRoundedRectangle (plate, 5.0f, 1.0f);

        auto screw = [&g] (float cx, float cy)
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

        // One screw in each corner of the knob area (inset from the plate edge).
        float inset = 11.0f;
        float kcx = kb.getCentreX(), kcy = kb.getCentreY();
        float half = juce::jmax (kb.getWidth(), kb.getHeight()) * 0.5f + 6.0f;
        float left = juce::jmax (plate.getX() + inset, kcx - half);
        float right = juce::jmin (plate.getRight() - inset, kcx + half);
        float topYs = juce::jmax (plate.getY() + inset, kcy - half);
        float botYs = juce::jmin (plate.getBottom() - inset, kcy + half);
        screw (left, topYs);  screw (right, topYs);
        screw (left, botYs);  screw (right, botYs);
    }
}

//==============================================================================
void FxBusPanel::resized()
{
    auto strip = getLocalBounds().reduced (4);

    headerLabel.setBounds (strip.removeFromTop (28));
    strip.removeFromTop (2);

    int slotsHeight = FxBus::MAX_FX_SLOTS * (kSlotHeight + kSlotPadding);
    strip.removeFromTop (slotsHeight);
    strip.removeFromTop (2);

    addFxButton.setBounds (strip.removeFromTop (22));
    strip.removeFromTop (2);

    bypassToggle.setBounds (strip.removeFromTop (18).reduced (2, 0));
    strip.removeFromTop (4);

    // VU meters
    int vuH = VuMeter::preferredHeight();
    vuMeterIn.setBounds (strip.removeFromTop (vuH).reduced (2, 0));
    strip.removeFromTop (2);
    vuMeterOut.setBounds (strip.removeFromTop (vuH).reduced (2, 0));
    strip.removeFromTop (2);

    // Peak and LUFS meters
    peakMeter.setBounds (strip.removeFromTop (vuH).reduced (2, 0));
    strip.removeFromTop (2);
    lufsMeter.setBounds (strip.removeFromTop (vuH).reduced (2, 0));
    strip.removeFromTop (2);

    // Stereo spread
    spreadMeter.setBounds (strip.removeFromTop (StereoSpreadMeter::preferredHeight()).reduced (2, 0));
    strip.removeFromTop (2);

    // Goniometer
    goniometer.setBounds (strip.removeFromTop (GoniometerMeter::preferredHeight()).reduced (2, 0));
    strip.removeFromTop (4);

    // Master knob caption + dB readout
    masterKnobCaption.setBounds (strip.removeFromTop (12));
    masterKnobLabel.setBounds (strip.removeFromTop (18));
    strip.removeFromTop (2);

    // Master knob — centered, fills remaining
    int knobSize = juce::jmin (strip.getWidth() - 8, strip.getHeight());
    knobSize = juce::jmax (40, knobSize);
    masterKnob.setBounds (strip.withSizeKeepingCentre (knobSize, knobSize));
}

//==============================================================================
int FxBusPanel::getSlotAt (int y) const
{
    int slotStartY = headerLabel.getBottom() + 4;

    for (int i = 0; i < FxBus::MAX_FX_SLOTS; ++i)
    {
        int sy = slotStartY + i * (kSlotHeight + kSlotPadding);
        if (y >= sy && y < sy + kSlotHeight)
            return i;
    }
    return -1;
}

void FxBusPanel::mouseDown (const juce::MouseEvent& e)
{
    int slotIndex = getSlotAt (e.y);
    if (slotIndex < 0) return;

    if (slots[slotIndex].empty)
    {
        if (e.mods.isLeftButtonDown())
        {
            if (onAddPluginClicked) onAddPluginClicked();
        }
    }
    else if (e.mods.isLeftButtonDown() && ! e.mods.isRightButtonDown())
    {
        bool bp = bus.isPluginBypassed (slotIndex);
        bus.setPluginBypassed (slotIndex, ! bp);
        refresh();
    }
    else if (e.mods.isRightButtonDown())
    {
        showSlotContextMenu (slotIndex);
    }
}

void FxBusPanel::showSlotContextMenu (int slotIndex)
{
    juce::PopupMenu menu;
    bool hasPlugin = slotIndex >= 0 && slotIndex < bus.getNumPlugins() && ! slots[slotIndex].empty;
    auto& clipboard = ChannelStripPanel::getClipboard();
    bool clipboardHasData = clipboard.pluginIdentifier.isNotEmpty();

    if (hasPlugin)
    {
        menu.addItem (1, "Open Editor");
        menu.addItem (6, "Nickname...");
        menu.addSeparator();

        bool bypassed = bus.isPluginBypassed (slotIndex);
        menu.addItem (2, bypassed ? "Enable" : "Bypass");
        menu.addSeparator();

        menu.addItem (10, "Copy");
        if (clipboardHasData)
        {
            bool samePlugin = clipboard.pluginIdentifier == bus.getPluginIdentifier (slotIndex);
            menu.addItem (11, samePlugin ? "Paste Settings" : "Paste (Replace)");
        }
        menu.addSeparator();
        menu.addItem (3, "Remove");
    }
    else
    {
        if (clipboardHasData)
            menu.addItem (11, "Paste");
    }

    menu.showMenuAsync ({}, [this, slotIndex, hasPlugin] (int result)
    {
        switch (result)
        {
            case 1: bus.openPluginEditor (slotIndex); break;
            case 2:
            {
                bool bp = bus.isPluginBypassed (slotIndex);
                bus.setPluginBypassed (slotIndex, ! bp);
                refresh();
                break;
            }
            case 3:
                bus.removePlugin (slotIndex);
                refresh();
                break;
            case 6:
                showNicknameDialog (slotIndex);
                break;

            case 10:
            {
                auto* proc = bus.getPlugin (slotIndex);
                if (proc != nullptr)
                {
                    auto& cb = ChannelStripPanel::getClipboard();
                    cb.pluginIdentifier = bus.getPluginIdentifier (slotIndex);
                    cb.pluginName = proc->getName();
                    cb.stateData.reset();
                    proc->getStateInformation (cb.stateData);
                    cb.bypassed = bus.isPluginBypassed (slotIndex);
                }
                break;
            }

            case 11:
            {
                auto& cb = ChannelStripPanel::getClipboard();
                // If pasting onto an existing slot of the SAME plugin, just restore
                // its settings; otherwise add a new instance via the host callback.
                if (hasPlugin && cb.pluginIdentifier == bus.getPluginIdentifier (slotIndex))
                {
                    auto* proc = bus.getPlugin (slotIndex);
                    if (proc != nullptr && cb.stateData.getSize() > 0)
                        proc->setStateInformation (cb.stateData.getData(), (int) cb.stateData.getSize());
                }
                else if (onPastePlugin)
                {
                    onPastePlugin (cb.pluginIdentifier, cb.stateData, cb.bypassed);
                }
                refresh();
                break;
            }
            default: break;
        }
    });
}

void FxBusPanel::showNicknameDialog (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= bus.getNumPlugins()) return;

    auto* aw = new juce::AlertWindow ("Plugin Nickname",
                                       "Enter a display name for this plugin.\nLeave blank to use the original name.",
                                       juce::AlertWindow::NoIcon);
    aw->addTextEditor ("nickname", slots[slotIndex].nickname, "Nickname:");
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, slotIndex, aw] (int result)
        {
            if (result == 1 && slotIndex < bus.getNumPlugins())
            {
                auto nick = aw->getTextEditorContents ("nickname").trim();
                slots[slotIndex].nickname = nick;
                if (auto* proc = bus.getPlugin (slotIndex))
                    pluginAppearance[proc->getName()].nickname = nick;
                refresh();
            }
            delete aw;
        }), true);
}

juce::Array<PluginAppearanceState> FxBusPanel::getAppearances() const
{
    juce::Array<PluginAppearanceState> result;
    for (const auto& [name, app] : pluginAppearance)
    {
        PluginAppearanceState pas;
        pas.pluginName = name;
        pas.nickname   = app.nickname;
        result.add (pas);
    }
    return result;
}

void FxBusPanel::setAppearances (const juce::Array<PluginAppearanceState>& appearances)
{
    pluginAppearance.clear();
    for (const auto& pas : appearances)
        pluginAppearance[pas.pluginName].nickname = pas.nickname;
    refresh();
}

//==============================================================================
void FxBusPanel::pushMeterLevels (float inL, float inR, float outL, float outR)
{
    vuMeterIn.pushLevel (inL, inR);
    vuMeterOut.pushLevel (outL, outR);
    peakMeter.pushLevel (outL, outR);
}

void FxBusPanel::pushStereo (float left, float right)
{
    spreadMeter.pushStereo (left, right);
}

void FxBusPanel::pushLufs (float lufsDb)
{
    lufsMeter.pushDb (lufsDb);
}

void FxBusPanel::pushGoniometerSamples (const float* leftData, const float* rightData, int numSamples)
{
    goniometer.pushSamples (leftData, rightData, numSamples);
}

void FxBusPanel::setMasterFaderDb (float db)
{
    masterKnob.setValue (db, juce::dontSendNotification);
}

void FxBusPanel::sliderValueChanged (juce::Slider* s)
{
    if (s == &masterKnob)
    {
        float db = (float) s->getValue();
        juce::String text = (db <= -59.9f) ? "-INF" : juce::String (db, 1) + " dB";
        masterKnobLabel.setText (text, juce::dontSendNotification);
        if (onMasterFaderChanged) onMasterFaderChanged (db);
    }
}
