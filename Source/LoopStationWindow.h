#pragma once
#include <JuceHeader.h>
#include "HardwareModuleWindow.h"
#include "Looper.h"

/**
 * LoopStationWindow
 *
 * A loop-station / stompbox face for the Looper.  Replaces the old single
 * toolbar button that cycled record→overdub→play on click (with a hidden
 * double-click for overdub and a right-click menu for every setting).  Here
 * each transport action is its own clearly-labelled, lit footswitch:
 *
 *   REC      arm + record the first layer (honours count-in), or finish recording
 *   OVERDUB  layer on top of the playing loop
 *   PLAY     play / stop the loop
 *   CLEAR    erase the loop
 *
 * Settings that used to hide in the right-click menu (count-in, meter, loop
 * length, capture point, export) are visible controls below the footswitches.
 */
class LoopStationContent : public juce::Component,
                           private juce::Timer
{
public:
    LoopStationContent (Looper& looperToUse, std::function<double()> bpmProvider)
        : looper (looperToUse), getBPM (std::move (bpmProvider))
    {
        auto styleSwitch = [this] (juce::TextButton& b, juce::Colour on)
        {
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242428));
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffbbbbc4));
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            addAndMakeVisible (b);
        };

        recButton.onClick = [this]
        {
            looper.setBPM (getBPM());
            looper.toggleRecord();
        };
        styleSwitch (recButton, juce::Colour (0xffcc2222));

        overdubButton.onClick = [this]
        {
            looper.setBPM (getBPM());
            looper.startOverdub();
        };
        styleSwitch (overdubButton, juce::Colour (0xffccaa22));

        playButton.onClick = [this] { looper.togglePlayStop(); };
        styleSwitch (playButton, juce::Colour (0xff22aa44));

        clearButton.onClick = [this] { looper.clear(); };
        styleSwitch (clearButton, juce::Colour (0xff666688));

        // ---- Settings combos ----
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

        styleCombo (countInBox, countInLabel, "Count-In");
        countInBox.addItem ("Off",    1);
        countInBox.addItem ("2 beats", 2);
        countInBox.addItem ("4 beats", 3);
        countInBox.addItem ("8 beats", 4);
        countInBox.onChange = [this]
        {
            const int beats[] = { 0, 2, 4, 8 };
            looper.setCountInBeats (beats[countInBox.getSelectedId() - 1]);
        };

        styleCombo (meterBox, meterLabel, "Meter");
        const char* meters[] = { "3/4", "4/4", "5/4", "6/8", "7/8" };
        for (int i = 0; i < 5; ++i) meterBox.addItem (meters[i], i + 1);
        meterBox.onChange = [this]
        {
            const int nums[] = { 3, 4, 5, 6, 7 };
            const int dens[] = { 4, 4, 4, 8, 8 };
            int i = meterBox.getSelectedId() - 1;
            looper.setMeter (nums[i], dens[i]);
        };

        styleCombo (barsBox, barsLabel, "Loop Length");
        const char* bars[] = { "Free", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };
        for (int i = 0; i < 6; ++i) barsBox.addItem (bars[i], i + 1);
        barsBox.onChange = [this]
        {
            const int b[] = { 0, 1, 2, 4, 8, 16 };
            looper.setLoopBars (b[barsBox.getSelectedId() - 1]);
        };

        styleCombo (captureBox, captureLabel, "Capture");
        captureBox.addItem ("Output (post-FX)", 1);
        captureBox.addItem ("Input (pre-FX)",   2);
        captureBox.onChange = [this]
        {
            looper.setCapturePoint (captureBox.getSelectedId() == 2
                ? Looper::CapturePoint::Input : Looper::CapturePoint::Output);
        };

        exportButton.onClick = [this] { exportLoop(); };
        exportButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a30));
        exportButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffccccdd));
        addAndMakeVisible (exportButton);

        syncFromLooper();
        startTimerHz (15);
    }

    ~LoopStationContent() override { stopTimer(); }

    void syncFromLooper()
    {
        const int ci = looper.getCountInBeats();
        countInBox.setSelectedId (ci == 0 ? 1 : ci == 2 ? 2 : ci == 4 ? 3 : 4, juce::dontSendNotification);

        int n = looper.getMeterNum(), d = looper.getMeterDen();
        int meterId = (n == 3) ? 1 : (n == 4) ? 2 : (n == 5) ? 3 : (n == 6) ? 4 : 5;
        meterBox.setSelectedId (meterId, juce::dontSendNotification);

        int b = looper.getLoopBars();
        int barsId = (b == 0) ? 1 : (b == 1) ? 2 : (b == 2) ? 3 : (b == 4) ? 4 : (b == 8) ? 5 : 6;
        barsBox.setSelectedId (barsId, juce::dontSendNotification);

        captureBox.setSelectedId (looper.getCapturePoint() == Looper::CapturePoint::Input ? 2 : 1,
                                  juce::dontSendNotification);
    }

    void exportLoop()
    {
        if (looper.getLoopLengthSamples() <= 0)
            return;
        auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                          .getChildFile ("UpStage Recordings");
        folder.createDirectory();
        chooser = std::make_unique<juce::FileChooser> (
            "Export Loop", folder.getChildFile ("UpStage_Loop.wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file != juce::File() && looper.exportToFile (file))
                    file.revealToUser();
            });
    }

    void timerCallback() override
    {
        auto s = looper.getState();
        recButton.setToggleState (s == Looper::State::Recording || s == Looper::State::CountIn,
                                  juce::dontSendNotification);
        overdubButton.setToggleState (s == Looper::State::Overdubbing || s == Looper::State::OverdubPending,
                                      juce::dontSendNotification);
        playButton.setToggleState (s == Looper::State::Playing || s == Looper::State::Overdubbing
                                    || s == Looper::State::OverdubPending,
                                   juce::dontSendNotification);
        exportButton.setEnabled (looper.getLoopLengthSamples() > 0);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff26262c), 0, 0,
                                                 juce::Colour (0xff121214), 0, b.getHeight(), false));
        g.fillRect (b);

        // Status readout strip at top
        auto strip = juce::Rectangle<float> (12, 10, b.getWidth() - 24, 30);
        g.setColour (juce::Colour (0xff0a0a0c));
        g.fillRoundedRectangle (strip, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (strip, 5.0f, 1.0f);

        auto s = looper.getState();
        juce::String stateText;
        juce::Colour stateCol;
        switch (s)
        {
            case Looper::State::Idle:
                stateText = looper.getLoopLengthSamples() > 0 ? "LOOP READY" : "EMPTY";
                stateCol  = juce::Colour (0xff667788); break;
            case Looper::State::CountIn:   stateText = "COUNT-IN";  stateCol = juce::Colour (0xffffcc44); break;
            case Looper::State::Recording: stateText = "RECORDING"; stateCol = juce::Colour (0xffff4444); break;
            case Looper::State::Playing:   stateText = "PLAYING";   stateCol = juce::Colour (0xff44dd66); break;
            case Looper::State::Overdubbing:
            case Looper::State::OverdubPending: stateText = "OVERDUB"; stateCol = juce::Colour (0xffddbb44); break;
        }
        g.setColour (stateCol);
        g.setFont (juce::Font (juce::FontOptions().withHeight (16.0f).withStyle ("Bold")));
        g.drawText (stateText, strip.reduced (10, 0), juce::Justification::centredLeft);

        // Position bar within the strip
        if (looper.getLoopLengthSamples() > 0)
        {
            float pos = (float) looper.getPositionNormalised();
            auto pb = strip.reduced (10, 8).removeFromRight (strip.getWidth() * 0.45f);
            g.setColour (juce::Colour (0xff1a1a22));
            g.fillRoundedRectangle (pb, 2.0f);
            g.setColour (stateCol.withAlpha (0.8f));
            g.fillRoundedRectangle (pb.withWidth (pb.getWidth() * pos), 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (36);  // painted status strip

        // Footswitch row
        auto row = area.removeFromTop (60);
        int bw = (row.getWidth() - 18) / 4;
        recButton    .setBounds (row.removeFromLeft (bw).reduced (3));
        overdubButton.setBounds (row.removeFromLeft (bw).reduced (3));
        playButton   .setBounds (row.removeFromLeft (bw).reduced (3));
        clearButton  .setBounds (row.removeFromLeft (bw).reduced (3));

        area.removeFromTop (12);

        // Settings grid: label above each combo, two per row
        auto placeRow = [&] (juce::Label& l1, juce::ComboBox& c1,
                             juce::Label& l2, juce::ComboBox& c2)
        {
            auto r = area.removeFromTop (44);
            auto left = r.removeFromLeft (r.getWidth() / 2).reduced (2);
            l1.setBounds (left.removeFromTop (14));
            c1.setBounds (left.removeFromTop (24));
            auto right = r.reduced (2);
            l2.setBounds (right.removeFromTop (14));
            c2.setBounds (right.removeFromTop (24));
        };
        placeRow (countInLabel, countInBox, meterLabel, meterBox);
        placeRow (barsLabel, barsBox, captureLabel, captureBox);

        area.removeFromTop (8);
        exportButton.setBounds (area.removeFromTop (28).reduced (2, 0));
    }

private:
    Looper& looper;
    std::function<double()> getBPM;

    juce::TextButton recButton     { "REC" };
    juce::TextButton overdubButton { "OVERDUB" };
    juce::TextButton playButton    { "PLAY" };
    juce::TextButton clearButton   { "CLEAR" };

    juce::Label    countInLabel, meterLabel, barsLabel, captureLabel;
    juce::ComboBox countInBox, meterBox, barsBox, captureBox;
    juce::TextButton exportButton { "Export Loop as WAV..." };

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopStationContent)
};

class LoopStationWindow : public HardwareModuleWindow
{
public:
    LoopStationWindow (Looper& looper, std::function<double()> bpmProvider)
        : HardwareModuleWindow ("Loop Station",
                                new LoopStationContent (looper, std::move (bpmProvider)),
                                380, 320)
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopStationWindow)
};
