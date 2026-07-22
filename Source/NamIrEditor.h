#pragma once

#include <JuceHeader.h>
#include "NamIrProcessor.h"
#include "AmpLibrary.h"
#include "MixerLookAndFeel.h"

/**
 * NamIrEditor - compact faceplate for the standalone Cab IR / Space IR rows.
 * Picture well + name strip + BROWSE, with MIX (spaces) and OUTPUT knobs.
 */
class NamIrEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit NamIrEditor (NamIrProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p)
    {
        setLookAndFeel (&lnf);
        const bool space = proc.getRole() == NamIrProcessor::Role::space;
        setSize (460, 250);

        titleLabel.setComponentID ("strip_label");
        titleLabel.setText (space ? "SPACE" : "CABINET", juce::dontSendNotification);
        titleLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (titleLabel);

        nameStrip.setComponentID ("readout");
        nameStrip.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (nameStrip);

        browseButton.onClick = [this]
        {
            juce::Component::SafePointer<NamIrEditor> safe (this);
            AmpLibrary::instance().requestPick (
                { proc.getRole() == NamIrProcessor::Role::space ? AmpLibraryEntry::Category::space
                                                                : AmpLibraryEntry::Category::cab },
                [safe] (juce::String id)
                {
                    if (safe != nullptr)
                        safe->proc.setIr (id);
                });
        };
        addAndMakeVisible (browseButton);

        auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& name,
                                 double lo, double hi, double def, std::atomic<float>& target)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setRange (lo, hi, 0.01);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setDoubleClickReturnValue (true, def);
            s.setValue ((double) target.load(), juce::dontSendNotification);
            s.onValueChange = [&s, &target] { target = (float) s.getValue(); };
            addAndMakeVisible (s);

            l.setText (name, juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centred);
            l.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            l.setColour (juce::Label::textColourId, juce::Colour (0xffb5b1a6));
            addAndMakeVisible (l);
        };

        setupKnob (mixKnob, mixLabel, "MIX", 0.0, 1.0, space ? 0.3 : 1.0, proc.mix);
        setupKnob (outKnob, outLabel, "OUTPUT", -24.0, 24.0, 0.0, proc.outputGainDb);
        // Cabs run fully wet by design; the MIX knob only makes sense on spaces.
        mixKnob.setVisible (space);
        mixLabel.setVisible (space);

        proc.onEngineStateChanged = [safe = juce::Component::SafePointer<NamIrEditor> (this)]
        {
            if (safe != nullptr)
                safe->refresh();
        };
        refresh();
        startTimerHz (10);
    }

    ~NamIrEditor() override
    {
        proc.onEngineStateChanged = nullptr;
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient grad (juce::Colour (0xff35322e), 0, 0,
                                   juce::Colour (0xff211f1c), 0, bounds.getHeight(), false);
        g.setGradientFill (grad);
        g.fillAll();
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (int y = 0; y < getHeight(); y += 3)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());

        auto screw = [&g] (float cx, float cy)
        {
            g.setColour (juce::Colour (0xff15140f));
            g.fillEllipse (cx - 5, cy - 5, 10, 10);
            g.setColour (juce::Colour (0xff6a675f));
            g.drawEllipse (cx - 5, cy - 5, 10, 10, 1.0f);
            g.drawLine (cx - 3, cy, cx + 3, cy, 1.4f);
        };
        screw (14, 14); screw (getWidth() - 14.0f, 14);
        screw (14, getHeight() - 14.0f); screw (getWidth() - 14.0f, getHeight() - 14.0f);

        // recessed picture well
        auto well = pictureWell.toFloat();
        g.setColour (juce::Colour (0xff141310));
        g.fillRoundedRectangle (well, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (well.reduced (0.5f), 4.0f, 1.5f);

        if (picture.isValid())
            g.drawImage (picture, well.reduced (3),
                         juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        else
        {
            g.setColour (juce::Colour (0xff3a372f));
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
            g.drawText (proc.getIrId().isEmpty() ? "NO IR"
                        : (proc.isMissing() ? "MISSING" : "NO PICTURE"),
                        pictureWell, juce::Justification::centred);
        }

        if (proc.isMissing())
        {
            auto f = juce::Font (juce::FontOptions().withHeight (10.0f).withStyle ("Bold"));
            g.setFont (f);
            juce::Rectangle<int> r (pictureWell.getX() + 4, pictureWell.getY() + 4, 58, 14);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xffe05a4e));
            g.drawText ("MISSING", r, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (22, 8);
        titleLabel.setBounds (area.removeFromTop (24).reduced (80, 0));
        area.removeFromTop (6);

        auto left = area.removeFromLeft (200);
        pictureWell = left.removeFromTop (120);
        left.removeFromTop (6);
        nameStrip.setBounds (left.removeFromTop (22));
        left.removeFromTop (6);
        browseButton.setBounds (left.removeFromTop (24).reduced (30, 0));

        auto knobs = area.reduced (10, 4);
        const int kw = knobs.getWidth() / 2;
        auto place = [] (juce::Slider& s, juce::Label& l, juce::Rectangle<int> cell)
        {
            l.setBounds (cell.removeFromBottom (14));
            s.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 64),
                                                     juce::jmin (cell.getHeight(), 64)));
        };
        place (mixKnob, mixLabel, knobs.removeFromLeft (kw));
        place (outKnob, outLabel, knobs);
    }

private:
    void refresh()
    {
        const auto name = proc.getIrName();
        nameStrip.setText (name.isEmpty() ? "-" : name, juce::dontSendNotification);

        juce::String picPath;
        if (const auto* e = AmpLibrary::instance().findById (proc.getIrId()))
            if (e->pictureFile.existsAsFile())
                picPath = e->pictureFile.getFullPathName();
        if (picPath != pictureSourcePath)
        {
            pictureSourcePath = picPath;
            picture = picPath.isEmpty() ? juce::Image()
                                        : juce::ImageCache::getFromFile (juce::File (picPath));
        }
        repaint();
    }

    void timerCallback() override { refresh(); }

    NamIrProcessor& proc;
    MixerLookAndFeel lnf;
    juce::Label titleLabel, nameStrip, mixLabel, outLabel;
    juce::TextButton browseButton { "BROWSE..." };
    juce::Slider mixKnob, outKnob;
    juce::Rectangle<int> pictureWell;
    juce::Image picture;
    juce::String pictureSourcePath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamIrEditor)
};
