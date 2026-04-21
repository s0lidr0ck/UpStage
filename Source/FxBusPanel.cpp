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

    // Master fader
    masterFader.setName ("master_fader");
    masterFader.setComponentID ("master_fader");
    masterFader.setSliderStyle (juce::Slider::LinearVertical);
    masterFader.setRange (-60.0, 12.0, 0.1);
    masterFader.setValue (0.0);
    masterFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterFader.setSkewFactorFromMidPoint (-12.0);
    masterFader.setDoubleClickReturnValue (true, 0.0);
    masterFader.addListener (this);
    addAndMakeVisible (masterFader);

    masterFaderLabel.setText ("0.0 dB", juce::dontSendNotification);
    masterFaderLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    masterFaderLabel.setJustificationType (juce::Justification::centred);
    masterFaderLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    masterFaderLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1a1a1a));
    masterFaderLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff333333));
    masterFaderLabel.setEditable (false, true, false);
    masterFaderLabel.onTextChange = [this] {
        auto text = masterFaderLabel.getText().trimCharactersAtEnd (" dB").trim();
        double db = juce::jlimit (-60.0, 12.0, text.getDoubleValue());
        masterFader.setValue (db);
    };
    addAndMakeVisible (masterFaderLabel);

    addAndMakeVisible (meterIn);
    addAndMakeVisible (meterOut);

    meterInLabel.setText ("IN", juce::dontSendNotification);
    meterInLabel.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    meterInLabel.setJustificationType (juce::Justification::centred);
    meterInLabel.setColour (juce::Label::textColourId, juce::Colour (0xff998844));
    addAndMakeVisible (meterInLabel);

    meterOutLabel.setText ("OUT", juce::dontSendNotification);
    meterOutLabel.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    meterOutLabel.setJustificationType (juce::Justification::centred);
    meterOutLabel.setColour (juce::Label::textColourId, juce::Colour (0xff449944));
    addAndMakeVisible (meterOutLabel);

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

    // Warm gradient background - slightly warmer than channel strips
    juce::ColourGradient bg (
        juce::Colour (0xff342e28), bounds.getCentreX(), bounds.getY(),
        juce::Colour (0xff241e1a), bounds.getCentreX(), bounds.getBottom(), false);
    bg.addColour (0.3, juce::Colour (0xff2e2822));
    g.setGradientFill (bg);
    g.fillAll();

    // Subtle horizontal brushed texture
    juce::Random rng (99);
    for (int ty = 0; ty < getHeight(); ty += 2)
    {
        float noiseAlpha = 0.01f + rng.nextFloat() * 0.012f;
        g.setColour (juce::Colours::white.withAlpha (noiseAlpha));
        g.drawHorizontalLine (ty, 0.0f, bounds.getWidth());
    }

    // Left border groove
    g.setColour (juce::Colour (0xff060504));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawVerticalLine (1, 0.0f, (float) getHeight());

    // Draw insert slots
    int slotStartY = headerLabel.getBottom() + 4;

    for (int i = 0; i < FxBus::MAX_FX_SLOTS; ++i)
    {
        int sy = slotStartY + i * (kSlotHeight + kSlotPadding);
        auto slotRect = juce::Rectangle<int> (6, sy, getWidth() - 12, kSlotHeight);

        // Slot background
        if (slots[i].empty)
        {
            g.setColour (juce::Colour (0xff1a1816));
            g.fillRoundedRectangle (slotRect.toFloat(), 3.0f);

            // Dashed outline for empty
            g.setColour (juce::Colour (0xff3a3530));
            g.drawRoundedRectangle (slotRect.toFloat(), 3.0f, 1.0f);

            g.setColour (juce::Colour (0xff4a4540));
            g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
            g.drawText ("Insert " + juce::String (i + 1), slotRect, juce::Justification::centred);
        }
        else
        {
            // Filled slot
            g.setColour (juce::Colour (0xff252220));
            g.fillRoundedRectangle (slotRect.toFloat(), 3.0f);

            // Bypass dot
            auto dotRect = slotRect.removeFromLeft (16);
            auto dotCentre = dotRect.getCentre().toFloat();
            g.setColour (slots[i].bypassed ? juce::Colour (0xff555555) : juce::Colour (0xff27ae60));
            g.fillEllipse (dotCentre.x - 3.0f, dotCentre.y - 3.0f, 6.0f, 6.0f);

            // Plugin name
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

    // Slots are painted, not laid out as children - skip their area
    int slotsHeight = FxBus::MAX_FX_SLOTS * (kSlotHeight + kSlotPadding);
    strip.removeFromTop (slotsHeight);
    strip.removeFromTop (2);

    // Add insert button
    addFxButton.setBounds (strip.removeFromTop (22));
    strip.removeFromTop (4);

    // Bypass toggle
    bypassToggle.setBounds (strip.removeFromTop (18).reduced (2, 0));
    strip.removeFromTop (4);

    // Master fader dB label
    masterFaderLabel.setBounds (strip.removeFromTop (18));
    strip.removeFromTop (2);

    // Meter labels at bottom
    auto meterLabelRow = strip.removeFromBottom (12);
    meterInLabel.setBounds (meterLabelRow.getX(), meterLabelRow.getY(), 18, 12);
    meterOutLabel.setBounds (meterLabelRow.getRight() - 22, meterLabelRow.getY(), 22, 12);

    // Meters beside fader: IN meter | fader | OUT meter
    meterIn.setBounds (strip.removeFromLeft (14));
    strip.removeFromLeft (1);
    meterOut.setBounds (strip.removeFromRight (14));
    strip.removeFromRight (1);

    // Master fader fills remaining space
    masterFader.setBounds (strip);
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
        // Click empty slot = add plugin
        if (e.mods.isLeftButtonDown())
        {
            if (onAddPluginClicked) onAddPluginClicked();
        }
    }
    else if (e.mods.isLeftButtonDown() && ! e.mods.isRightButtonDown())
    {
        // Left click = toggle bypass
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
    meterIn.pushLevel (inL, inR);
    meterOut.pushLevel (outL, outR);
}

void FxBusPanel::setMasterFaderDb (float db)
{
    masterFader.setValue (db, juce::dontSendNotification);
}

void FxBusPanel::sliderValueChanged (juce::Slider* s)
{
    if (s == &masterFader)
    {
        float db = (float) s->getValue();
        juce::String text = (db <= -59.9f) ? "-INF" : juce::String (db, 1) + " dB";
        masterFaderLabel.setText (text, juce::dontSendNotification);
        if (onMasterFaderChanged) onMasterFaderChanged (db);
    }
}
