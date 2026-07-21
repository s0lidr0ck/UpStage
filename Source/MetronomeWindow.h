#pragma once
#include <JuceHeader.h>
#include "HardwareModuleWindow.h"
#include "Metronome.h"

/**
 * MetronomeWindow
 *
 * A mechanical-metronome face for the Metronome engine.  Replaces the
 * right-click pop-up menu with visible controls: on/off, BPM, time signature,
 * subdivision, click sound, accent, and volume.  A swinging pendulum animates
 * in time with the beat.
 */
class MetronomeContent : public juce::Component,
                         private juce::Timer
{
public:
    MetronomeContent (Metronome& metro, std::function<void (double)> bpmSetter)
        : metronome (metro), setBpmEverywhere (std::move (bpmSetter))
    {
        startStopButton.setClickingTogglesState (true);
        startStopButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242428));
        startStopButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4444bb));
        startStopButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffbbbbc4));
        startStopButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        startStopButton.onClick = [this]
        {
            metronome.setEnabled (startStopButton.getToggleState());
        };
        addAndMakeVisible (startStopButton);

        bpmSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        bpmSlider.setRange (40.0, 240.0, 1.0);
        bpmSlider.setValue (metronome.getBPM(), juce::dontSendNotification);
        bpmSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        bpmSlider.setTextValueSuffix (" BPM");
        bpmSlider.onValueChange = [this]
        {
            double bpm = bpmSlider.getValue();
            metronome.setBPM (bpm);
            if (setBpmEverywhere) setBpmEverywhere (bpm);
        };
        addAndMakeVisible (bpmSlider);

        auto styleCombo = [this] (juce::ComboBox& c, juce::Label& lbl, const juce::String& text)
        {
            lbl.setText (text, juce::dontSendNotification);
            lbl.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            lbl.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
            addAndMakeVisible (lbl);
            c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff15151a));
            c.setColour (juce::ComboBox::textColourId, juce::Colour (0xffccccdd));
            addAndMakeVisible (c);
        };

        styleCombo (meterBox, meterLabel, "Meter");
        const char* meters[] = { "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "12/8" };
        for (int i = 0; i < 7; ++i) meterBox.addItem (meters[i], i + 1);
        meterBox.onChange = [this]
        {
            const int nums[] = { 2, 3, 4, 5, 6, 7, 12 };
            const int dens[] = { 4, 4, 4, 4, 8, 8, 8 };
            int i = meterBox.getSelectedId() - 1;
            metronome.setTimeSignature (nums[i], dens[i]);
        };

        styleCombo (subBox, subLabel, "Subdivision");
        subBox.addItem ("None (quarter)", 1);
        subBox.addItem ("8th notes",      2);
        subBox.addItem ("8th triplets",   3);
        subBox.addItem ("16th notes",     4);
        subBox.onChange = [this] { metronome.setSubdivision (subBox.getSelectedId()); };

        styleCombo (soundBox, soundLabel, "Sound");
        soundBox.addItem ("Sine (clean)",  1);
        soundBox.addItem ("Tick (snappy)", 2);
        soundBox.addItem ("Woodblock",     3);
        soundBox.onChange = [this]
        {
            metronome.setClickSound (static_cast<Metronome::ClickSound> (soundBox.getSelectedId() - 1));
        };

        accentToggle.setButtonText ("Accent first beat");
        accentToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffbbbbc4));
        accentToggle.onClick = [this] { metronome.setAccentEnabled (accentToggle.getToggleState()); };
        addAndMakeVisible (accentToggle);

        volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        volumeSlider.setRange (0.0, 1.0, 0.01);
        volumeSlider.setValue (metronome.getVolume(), juce::dontSendNotification);
        volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        volumeSlider.onValueChange = [this] { metronome.setVolume ((float) volumeSlider.getValue()); };
        addAndMakeVisible (volumeSlider);

        volumeLabel.setText ("Volume", juce::dontSendNotification);
        volumeLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        volumeLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
        addAndMakeVisible (volumeLabel);

        syncFromMetronome();
        startTimerHz (30);
    }

    ~MetronomeContent() override { stopTimer(); }

    void syncFromMetronome()
    {
        startStopButton.setToggleState (metronome.isEnabled(), juce::dontSendNotification);

        int n = metronome.getNumerator(), d = metronome.getDenominator();
        int id = (n == 2) ? 1 : (n == 3) ? 2 : (n == 4) ? 3 : (n == 5) ? 4
               : (n == 6) ? 5 : (n == 7) ? 6 : 7;
        meterBox.setSelectedId (id, juce::dontSendNotification);

        subBox.setSelectedId (juce::jlimit (1, 4, metronome.getSubdivision()), juce::dontSendNotification);
        soundBox.setSelectedId ((int) metronome.getClickSound() + 1, juce::dontSendNotification);
        accentToggle.setToggleState (metronome.isAccentEnabled(), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        startStopButton.setButtonText (metronome.isEnabled() ? "STOP" : "START");
        startStopButton.setToggleState (metronome.isEnabled(), juce::dontSendNotification);

        // Drive the pendulum from the beat flash.
        if (metronome.isEnabled() && metronome.consumeBeatFlash())
            swingTarget = -swingTarget;  // flip side each beat

        // Ease the pendulum toward its target side.
        swing += (swingTarget - swing) * 0.25f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a30), 0, 0,
                                                 juce::Colour (0xff141418), 0, b.getHeight(), false));
        g.fillRect (b);

        // ---- Wooden metronome body (trapezoid) on the left ----
        auto body = juce::Rectangle<float> (16, 14, 120, 180);
        juce::Path trap;
        trap.addQuadrilateral (body.getCentreX() - 26, body.getY(),
                               body.getCentreX() + 26, body.getY(),
                               body.getRight(),        body.getBottom(),
                               body.getX(),            body.getBottom());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6b4a2a), body.getX(), body.getY(),
                                                 juce::Colour (0xff3a2814), body.getRight(), body.getBottom(), false));
        g.fillPath (trap);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.strokePath (trap, juce::PathStrokeType (1.5f));

        // Pendulum: pivots at the bottom of the body, swings by `swing`.
        float pivotX = body.getCentreX();
        float pivotY = body.getBottom() - 14.0f;
        float armLen = 150.0f;
        float angle  = swing * 0.5f;  // radians
        float tipX = pivotX + std::sin (angle) * armLen;
        float tipY = pivotY - std::cos (angle) * armLen;
        g.setColour (juce::Colour (0xffd0d0d8));
        g.drawLine (pivotX, pivotY, tipX, tipY, 2.5f);
        // Weight on the arm
        float wX = pivotX + std::sin (angle) * armLen * 0.7f;
        float wY = pivotY - std::cos (angle) * armLen * 0.7f;
        g.setColour (juce::Colour (0xffc0a060));
        g.fillRect (wX - 7, wY - 4, 14.0f, 8.0f);
        // Pivot screw
        g.setColour (juce::Colour (0xff222226));
        g.fillEllipse (pivotX - 4, pivotY - 4, 8.0f, 8.0f);

        // BPM tempo indicator LED above the dial
        bool on = metronome.isEnabled();
        auto led = juce::Rectangle<float> (b.getRight() - 28, 16, 12, 12);
        g.setColour (on ? juce::Colour (0xff5577ff) : juce::Colour (0xff223066));
        g.fillEllipse (led);
        if (on)
        {
            g.setColour (juce::Colour (0x555577ff));
            g.fillEllipse (led.expanded (4.0f));
        }
    }

    void resized() override
    {
        // Right column holds the controls; left ~150px is the painted body.
        auto area = getLocalBounds().reduced (12);
        area.removeFromLeft (140);

        startStopButton.setBounds (area.removeFromTop (34).reduced (2));
        area.removeFromTop (6);
        bpmSlider.setBounds (area.removeFromTop (84).reduced (2));
        area.removeFromTop (4);

        meterLabel.setBounds (area.removeFromTop (14));
        meterBox.setBounds (area.removeFromTop (22).reduced (0, 1));
        subLabel.setBounds (area.removeFromTop (14));
        subBox.setBounds (area.removeFromTop (22).reduced (0, 1));
        soundLabel.setBounds (area.removeFromTop (14));
        soundBox.setBounds (area.removeFromTop (22).reduced (0, 1));

        accentToggle.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        volumeLabel.setBounds (area.removeFromTop (14));
        volumeSlider.setBounds (area.removeFromTop (24));
    }

private:
    Metronome& metronome;
    std::function<void (double)> setBpmEverywhere;

    float swing = 0.0f;       // current pendulum position (-1..1)
    float swingTarget = 1.0f; // side it is easing toward

    juce::TextButton startStopButton { "START" };
    juce::Slider     bpmSlider;
    juce::Label      meterLabel, subLabel, soundLabel, volumeLabel;
    juce::ComboBox   meterBox, subBox, soundBox;
    juce::ToggleButton accentToggle;
    juce::Slider     volumeSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeContent)
};

class MetronomeWindow : public HardwareModuleWindow
{
public:
    MetronomeWindow (Metronome& metro, std::function<void (double)> bpmSetter)
        : HardwareModuleWindow ("Metronome",
                                new MetronomeContent (metro, std::move (bpmSetter)),
                                360, 320)
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeWindow)
};
