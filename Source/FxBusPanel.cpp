#include "FxBusPanel.h"

//==============================================================================
FxBusPanel::FxBusPanel (FxBus& b)
    : bus (b)
{
    headerLabel.setText ("MASTER", juce::dontSendNotification);
    headerLabel.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
    headerLabel.setJustificationType (juce::Justification::centred);
    headerLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffcc99));
    headerLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff3d3428));
    addAndMakeVisible (headerLabel);

    bypassToggle.onStateChange = [this] { bus.setBypassed (bypassToggle.getToggleState()); };
    bypassToggle.setToggleState (bus.isBypassed(), juce::dontSendNotification);
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
        }
        else
        {
            slots[i].name = "";
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
            g.drawText (slots[i].name, slotRect.reduced (4, 0), juce::Justification::centredLeft, true);
        }
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

    // Master knob label
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

    menu.addItem (1, "Open Editor");
    menu.addSeparator();

    bool bypassed = bus.isPluginBypassed (slotIndex);
    menu.addItem (2, bypassed ? "Enable" : "Bypass");
    menu.addSeparator();
    menu.addItem (3, "Remove");

    menu.showMenuAsync ({}, [this, slotIndex] (int result)
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
            default: break;
        }
    });
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
