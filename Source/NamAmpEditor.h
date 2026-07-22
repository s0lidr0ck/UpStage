#pragma once

#include <JuceHeader.h>
#include "NamAmpProcessor.h"
#include "AmpLibrary.h"
#include "MixerLookAndFeel.h"
#include "ImageLoader.h"

/**
 * NamAmpEditor - skeuomorphic amp-head faceplate for NamAmpProcessor.
 *
 * Opened through the existing ChannelStrip::openPluginEditor window path.
 * Single mode: one rig zone + shared knobs. Dual mode: mirrored A/B rig zones
 * with blend / per-side pan / polarity controls between them.
 */
class NamAmpEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit NamAmpEditor (NamAmpProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p)
    {
        setLookAndFeel (&lnf);

        titleLabel.setComponentID ("strip_label");
        titleLabel.setText (proc.getRole() == NamAmpProcessor::Role::pedal ? "NAM PEDAL" : "NAM AMP",
                            juce::dontSendNotification);
        titleLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (titleLabel);

        dualButton.setClickingTogglesState (true);
        dualButton.setButtonText ("DUAL");
        dualButton.setToggleState (proc.dualMode.load(), juce::dontSendNotification);
        dualButton.onClick = [this]
        {
            proc.dualMode = dualButton.getToggleState();
            applyModeLayout();
        };
        addAndMakeVisible (dualButton);

        setupSideUi (0);
        setupSideUi (1);

        setupKnob (inputKnob,  "INPUT",  -24.0, 24.0, 0.0, proc.inputGainDb);
        setupKnob (bassKnob,   "BASS",     0.0, 10.0, 5.0, proc.bassKnob, true);
        setupKnob (midKnob,    "MID",      0.0, 10.0, 5.0, proc.midKnob, true);
        setupKnob (trebleKnob, "TREBLE",   0.0, 10.0, 5.0, proc.trebleKnob, true);
        setupKnob (outputKnob, "OUTPUT", -24.0, 24.0, 0.0, proc.outputGainDb);

        setupKnob (blendKnob, "BLEND", 0.0, 1.0, 0.5, proc.blend);
        setupKnob (panAKnob,  "PAN A", -1.0, 1.0, 0.0, proc.panA);
        panAKnob.slider.setComponentID ("amp_panA");     // "pan" triggers the bipolar arc
        setupKnob (panBKnob,  "PAN B", -1.0, 1.0, 0.0, proc.panB);
        panBKnob.slider.setComponentID ("amp_panB");

        polarityButton.setClickingTogglesState (true);
        polarityButton.setButtonText (juce::String::fromUTF8 ("\xc3\x98 B"));
        polarityButton.setToggleState (proc.polarityFlipB.load(), juce::dontSendNotification);
        polarityButton.onClick = [this] { proc.polarityFlipB = polarityButton.getToggleState(); };
        addAndMakeVisible (polarityButton);

        liteButton.setClickingTogglesState (true);
        liteButton.setButtonText ("LITE");
        liteButton.setToggleState (proc.useLite.load(), juce::dontSendNotification);
        liteButton.onClick = [this] { proc.setUseLite (liteButton.getToggleState()); };
        liteButton.setTooltip ("Run the A2 model at its Lite size to save CPU");
        addAndMakeVisible (liteButton);

        proc.onEngineStateChanged = [safe = juce::Component::SafePointer<NamAmpEditor> (this)]
        {
            if (safe != nullptr)
                safe->refreshFromProcessor();
        };

        refreshFromProcessor();
        applyModeLayout();
        startTimerHz (10);
    }

    ~NamAmpEditor() override
    {
        proc.onEngineStateChanged = nullptr;
        setLookAndFeel (nullptr);
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        // Brushed-metal amp face, same family as the module windows.
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient grad (juce::Colour (0xff35322e), 0, 0,
                                   juce::Colour (0xff211f1c), 0, bounds.getHeight(), false);
        grad.addColour (0.5, juce::Colour (0xff2c2a26));
        g.setGradientFill (grad);
        g.fillAll();

        // fine brush lines
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (int y = 0; y < getHeight(); y += 3)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());

        // corner screws
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

        paintSideZone (g, 0);
        if (proc.dualMode.load())
            paintSideZone (g, 1);
    }

    void resized() override { layout(); }

private:
    //==========================================================================
    struct LabelledKnob
    {
        juce::Slider slider;
        juce::Label  label;
    };

    struct SideUi
    {
        juce::Rectangle<int> zone;          // whole side zone (for paint)
        juce::Rectangle<int> pictureWell;   // recessed picture area
        juce::Label  rigStrip;
        juce::TextButton browseRig  { "BROWSE..." };
        juce::Label  cabReadout;
        juce::TextButton cabToggle  { "CAB" };
        juce::TextButton browseCab  { "IR..." };
        juce::Slider trimKnob;              // dual only
        juce::Label  trimLabel;
        juce::Image  picture;
        juce::String pictureSourcePath;
    };

    void setupSideUi (int i)
    {
        auto& s = side[i];

        s.rigStrip.setComponentID ("strip_label");
        s.rigStrip.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (s.rigStrip);

        s.browseRig.onClick = [this, i]
        {
            juce::Component::SafePointer<NamAmpEditor> safe (this);
            // Pedal rows browse pedals; amp rows browse any capture type.
            juce::Array<AmpLibraryEntry::Category> cats;
            if (proc.getRole() == NamAmpProcessor::Role::pedal)
                cats = { AmpLibraryEntry::Category::pedal };
            else
                cats = { AmpLibraryEntry::Category::head, AmpLibraryEntry::Category::fullRig,
                         AmpLibraryEntry::Category::pedal };
            AmpLibrary::instance().requestPick (std::move (cats),
                [safe, i] (juce::String id)
                {
                    if (safe != nullptr)
                        safe->proc.loadRig (i, id);
                });
        };
        addAndMakeVisible (s.browseRig);

        s.cabReadout.setComponentID ("readout");
        s.cabReadout.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (s.cabReadout);

        s.cabToggle.setClickingTogglesState (true);
        s.cabToggle.setToggleState (proc.isCabEnabled (i), juce::dontSendNotification);
        s.cabToggle.onClick = [this, i] { proc.setCabEnabled (i, side[i].cabToggle.getToggleState()); };
        addAndMakeVisible (s.cabToggle);

        s.browseCab.onClick = [this, i]
        {
            juce::Component::SafePointer<NamAmpEditor> safe (this);
            // wav-backed only: this built-in cab stage is a convolver. NAM cab
            // captures load in the standalone Cab IR row instead.
            AmpLibrary::instance().requestPick ({ AmpLibraryEntry::Category::cab,
                                                  AmpLibraryEntry::Category::space },
                [safe, i] (juce::String id)
                {
                    if (safe != nullptr)
                        safe->proc.setCab (i, id);
                },
                true /*wavIrOnly*/);
        };
        addAndMakeVisible (s.browseCab);

        s.trimKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.trimKnob.setRange (-12.0, 12.0, 0.1);
        s.trimKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.trimKnob.setDoubleClickReturnValue (true, 0.0);
        s.trimKnob.setValue ((double) (i == 0 ? proc.sideTrimDbA : proc.sideTrimDbB).load(),
                             juce::dontSendNotification);
        s.trimKnob.onValueChange = [this, i]
        {
            (i == 0 ? proc.sideTrimDbA : proc.sideTrimDbB) = (float) side[i].trimKnob.getValue();
        };
        addAndMakeVisible (s.trimKnob);

        s.trimLabel.setText ("TRIM", juce::dontSendNotification);
        s.trimLabel.setJustificationType (juce::Justification::centred);
        s.trimLabel.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
        s.trimLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9a968c));
        addAndMakeVisible (s.trimLabel);
    }

    void setupKnob (LabelledKnob& k, const juce::String& name,
                    double lo, double hi, double def, std::atomic<float>& target,
                    bool isTone = false)
    {
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setRange (lo, hi, 0.01);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.slider.setDoubleClickReturnValue (true, def);
        k.slider.setValue ((double) target.load(), juce::dontSendNotification);
        k.slider.onValueChange = [this, &k, &target, isTone]
        {
            target = (float) k.slider.getValue();
            if (isTone)
                proc.toneChanged();
        };
        addAndMakeVisible (k.slider);

        k.label.setText (name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        k.label.setColour (juce::Label::textColourId, juce::Colour (0xffb5b1a6));
        addAndMakeVisible (k.label);
    }

    //==========================================================================
    void applyModeLayout()
    {
        const bool pedal = proc.getRole() == NamAmpProcessor::Role::pedal;
        const bool dual = ! pedal && proc.dualMode.load();
        setSize (pedal ? kPedalWidth : (dual ? kDualWidth : kSingleWidth),
                 pedal ? kPedalHeight : kHeight);

        // Pedal rows: no cab stage, no tone stack, no dual mode.
        dualButton.setVisible (! pedal);
        for (auto* c : { (juce::Component*) &side[0].cabReadout, (juce::Component*) &side[0].cabToggle,
                         (juce::Component*) &side[0].browseCab,
                         (juce::Component*) &bassKnob.slider,   (juce::Component*) &bassKnob.label,
                         (juce::Component*) &midKnob.slider,    (juce::Component*) &midKnob.label,
                         (juce::Component*) &trebleKnob.slider, (juce::Component*) &trebleKnob.label })
            c->setVisible (! pedal);

        for (auto* c : { (juce::Component*) &side[1].rigStrip, (juce::Component*) &side[1].browseRig,
                         (juce::Component*) &side[1].cabReadout, (juce::Component*) &side[1].cabToggle,
                         (juce::Component*) &side[1].browseCab })
            c->setVisible (dual);

        for (auto* c : { (juce::Component*) &blendKnob.slider, (juce::Component*) &blendKnob.label,
                         (juce::Component*) &panAKnob.slider,  (juce::Component*) &panAKnob.label,
                         (juce::Component*) &panBKnob.slider,  (juce::Component*) &panBKnob.label,
                         (juce::Component*) &polarityButton })
            c->setVisible (dual);

        side[0].trimKnob.setVisible (dual);
        side[0].trimLabel.setVisible (dual);
        side[1].trimKnob.setVisible (dual);
        side[1].trimLabel.setVisible (dual);

        layout();
        repaint();
    }

    void layoutSide (SideUi& s, juce::Rectangle<int> zone, bool dual)
    {
        s.zone = zone;
        auto area = zone.reduced (10);

        auto top = area.removeFromTop (18);
        s.rigStrip.setBounds (top);
        area.removeFromTop (4);

        s.pictureWell = area.removeFromTop (dual ? 96 : 120);
        area.removeFromTop (6);

        auto row = area.removeFromTop (24);
        s.browseRig.setBounds (row.removeFromLeft (86));
        row.removeFromLeft (6);
        if (dual)
        {
            auto trimArea = row.removeFromRight (54);
            s.trimKnob.setBounds (trimArea.removeFromLeft (34).withHeight (34));
            s.trimLabel.setBounds (trimArea.withHeight (34));
        }

        area.removeFromTop (6);
        auto cabRow = area.removeFromTop (24);
        s.cabToggle.setBounds (cabRow.removeFromLeft (48));
        cabRow.removeFromLeft (6);
        s.browseCab.setBounds (cabRow.removeFromRight (48));
        cabRow.removeFromRight (6);
        s.cabReadout.setBounds (cabRow);
    }

    void layout()
    {
        const bool pedal = proc.getRole() == NamAmpProcessor::Role::pedal;
        const bool dual = ! pedal && proc.dualMode.load();
        auto area = getLocalBounds().reduced (22, 8);

        auto header = area.removeFromTop (26);
        if (! pedal)
        {
            dualButton.setBounds (header.removeFromRight (64).reduced (0, 2));
            header.removeFromRight (6);
        }
        liteButton.setBounds (header.removeFromRight (56).reduced (0, 2));
        titleLabel.setBounds (header.reduced (60, 0));
        area.removeFromTop (4);

        if (pedal)
        {
            layoutSide (side[0], area.removeFromLeft (240), false);
            auto centre = area.reduced (8, 0);
            auto knobRow = centre.removeFromTop (96);
            const int kw = knobRow.getWidth() / 2;
            auto placePedalKnob = [] (LabelledKnob& k, juce::Rectangle<int> cell)
            {
                k.label.setBounds (cell.removeFromBottom (14));
                auto sq = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 64),
                                                      juce::jmin (cell.getHeight(), 64));
                k.slider.setBounds (sq);
            };
            placePedalKnob (inputKnob,  knobRow.removeFromLeft (kw));
            placePedalKnob (outputKnob, knobRow);
            return;
        }

        const int sideWidth = dual ? 250 : 240;
        layoutSide (side[0], area.removeFromLeft (sideWidth), dual);
        if (dual)
            layoutSide (side[1], area.removeFromRight (sideWidth), dual);

        auto centre = area.reduced (8, 0);

        // main knob row
        auto knobRow = centre.removeFromTop (96);
        const int knobW = knobRow.getWidth() / 5;
        auto placeKnob = [] (LabelledKnob& k, juce::Rectangle<int> cell)
        {
            k.label.setBounds (cell.removeFromBottom (14));
            auto sq = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 64),
                                                  juce::jmin (cell.getHeight(), 64));
            k.slider.setBounds (sq);
        };
        placeKnob (inputKnob,  knobRow.removeFromLeft (knobW));
        placeKnob (bassKnob,   knobRow.removeFromLeft (knobW));
        placeKnob (midKnob,    knobRow.removeFromLeft (knobW));
        placeKnob (trebleKnob, knobRow.removeFromLeft (knobW));
        placeKnob (outputKnob, knobRow);

        if (dual)
        {
            centre.removeFromTop (6);
            auto mixRow = centre.removeFromTop (86);
            const int mixW = mixRow.getWidth() / 4;
            placeKnob (panAKnob,  mixRow.removeFromLeft (mixW));
            placeKnob (blendKnob, mixRow.removeFromLeft (mixW));
            placeKnob (panBKnob,  mixRow.removeFromLeft (mixW));
            polarityButton.setBounds (mixRow.withSizeKeepingCentre (52, 26));
        }
    }

    //==========================================================================
    void paintSideZone (juce::Graphics& g, int i)
    {
        auto& s = side[i];
        if (s.zone.isEmpty())
            return;

        // recessed picture well
        auto well = s.pictureWell.toFloat();
        g.setColour (juce::Colour (0xff141310));
        g.fillRoundedRectangle (well, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (well.reduced (0.5f), 4.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (well.getX() + 2, well.getBottom() - 1, well.getRight() - 2, well.getBottom() - 1, 1.0f);

        if (s.picture.isValid())
        {
            g.drawImage (s.picture, well.reduced (3),
                         juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        }
        else
        {
            g.setColour (juce::Colour (0xff3a372f));
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
            g.drawText (proc.getRigId (i).isEmpty() ? "NO RIG" : "NO PICTURE",
                        s.pictureWell, juce::Justification::centred);
        }

        // warning legends
        auto legendArea = s.pictureWell.removeFromTop (0); // placeholder rect builder below
        int lx = s.pictureWell.getX() + 4;
        const int ly = s.pictureWell.getY() + 4;
        auto legend = [&g, &lx, ly] (const juce::String& text, juce::Colour col)
        {
            auto f = juce::Font (juce::FontOptions().withHeight (10.0f).withStyle ("Bold"));
            g.setFont (f);
            const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, text)) + 10;
            juce::Rectangle<int> r (lx, ly, w, 14);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (col);
            g.drawText (text, r, juce::Justification::centred);
            lx += w + 4;
        };

        if (proc.didModelFail (i))
            legend ("MISSING", juce::Colour (0xffe05a4e));
        if (proc.hasSampleRateMismatch (i))
            legend ("48k!", juce::Colour (0xffe0b34e));
        juce::ignoreUnused (legendArea);
    }

    void refreshFromProcessor()
    {
        for (int i = 0; i < 2; ++i)
        {
            auto& s = side[i];
            const auto rigName = proc.getRigName (i);
            s.rigStrip.setText (rigName.isEmpty() ? "EMPTY" : rigName.toUpperCase(),
                                juce::dontSendNotification);

            const auto cabName = proc.getCabName (i);
            s.cabReadout.setText (cabName.isEmpty() ? "-" : cabName, juce::dontSendNotification);
            s.cabToggle.setToggleState (proc.isCabEnabled (i), juce::dontSendNotification);

            // resolve the rig picture (cache by path)
            juce::String picPath;
            if (const auto* e = AmpLibrary::instance().findById (proc.getRigId (i)))
                if (e->pictureFile.existsAsFile())
                    picPath = e->pictureFile.getFullPathName();

            if (picPath != s.pictureSourcePath)
            {
                s.pictureSourcePath = picPath;
                s.picture = picPath.isEmpty() ? juce::Image()
                                              : UpStageImages::load (juce::File (picPath));
            }
        }

        liteButton.setEnabled (proc.isModelSlimmable (0) || proc.isModelSlimmable (1)
                               || ! (proc.hasModel (0) || proc.hasModel (1)));
        repaint();
    }

    void timerCallback() override { refreshFromProcessor(); }

    //==========================================================================
    static constexpr int kSingleWidth = 640;
    static constexpr int kDualWidth   = 1000;
    static constexpr int kHeight      = 330;
    static constexpr int kPedalWidth  = 500;
    static constexpr int kPedalHeight = 260;

    NamAmpProcessor& proc;
    MixerLookAndFeel lnf;

    juce::Label titleLabel;
    juce::TextButton dualButton { "DUAL" };
    juce::TextButton liteButton { "LITE" };
    juce::TextButton polarityButton;

    SideUi side[2];

    LabelledKnob inputKnob, bassKnob, midKnob, trebleKnob, outputKnob;
    LabelledKnob blendKnob, panAKnob, panBKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamAmpEditor)
};
