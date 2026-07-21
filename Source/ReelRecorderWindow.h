#pragma once
#include <JuceHeader.h>
#include "HardwareModuleWindow.h"
#include "Recorder.h"

/**
 * ReelRecorderWindow
 *
 * A reel-to-reel face for the Recorder.  Replaces the old two-button
 * Record/Stop pair (and the hidden right-click "reveal folder") with a single
 * deliberate panel: choose what to capture (dry / wet / both), where to save,
 * then arm REC and STOP.  Reels spin while recording.
 */
class ReelRecorderContent : public juce::Component,
                            private juce::Timer
{
public:
    ReelRecorderContent (Recorder& rec, std::function<juce::File()> defaultFolderProvider)
        : recorder (rec), getDefaultFolder (std::move (defaultFolderProvider))
    {
        outputFolder = getDefaultFolder();

        recButton.onClick  = [this] { startRecording(); };
        recButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242428));
        recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffcc2222));
        recButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc8888));
        recButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (recButton);

        stopButton.onClick = [this] { stopRecording(); };
        stopButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242428));
        stopButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffbbbbc4));
        addAndMakeVisible (stopButton);

        captureLabel.setText ("Capture", juce::dontSendNotification);
        captureLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        captureLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
        addAndMakeVisible (captureLabel);

        captureBox.addItem ("Dry only (pre-FX)",   1);
        captureBox.addItem ("Wet only (post-FX)",  2);
        captureBox.addItem ("Both (two files)",    3);
        captureBox.setSelectedId (3, juce::dontSendNotification);
        captureBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff15151a));
        captureBox.setColour (juce::ComboBox::textColourId, juce::Colour (0xffccccdd));
        addAndMakeVisible (captureBox);

        folderButton.onClick = [this] { chooseFolder(); };
        folderButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a30));
        folderButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffccccdd));
        addAndMakeVisible (folderButton);

        revealButton.onClick = [this]
        {
            auto f = recorder.getLastOutputFolder();
            if (! f.isDirectory()) f = outputFolder;
            if (f.isDirectory()) f.revealToUser();
        };
        revealButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a30));
        revealButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffccccdd));
        addAndMakeVisible (revealButton);

        startTimerHz (20);
    }

    ~ReelRecorderContent() override { stopTimer(); }

    void startRecording()
    {
        if (recorder.isRecording())
            return;
        Recorder::Mode mode = captureBox.getSelectedId() == 1 ? Recorder::Mode::InputOnly
                            : captureBox.getSelectedId() == 2 ? Recorder::Mode::OutputOnly
                            : Recorder::Mode::Both;
        outputFolder.createDirectory();
        recorder.startRecording (outputFolder, mode);
    }

    void stopRecording()
    {
        if (recorder.isRecording())
            recorder.stopRecording();
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Recordings Folder", outputFolder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.isDirectory())
                {
                    outputFolder = f;
                    repaint();
                }
            });
    }

    void timerCallback() override
    {
        bool recording = recorder.isRecording();
        recButton.setToggleState (recording, juce::dontSendNotification);
        recButton.setEnabled (! recording);
        stopButton.setEnabled (recording);
        captureBox.setEnabled (! recording);
        folderButton.setEnabled (! recording);
        if (recording)
            reelAngle += 0.22f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c30), 0, 0,
                                                 juce::Colour (0xff141416), 0, b.getHeight(), false));
        g.fillRect (b);

        // ---- Two open reels on a deck plate ----
        auto deck = juce::Rectangle<float> (16, 14, b.getWidth() - 32, 120);
        g.setColour (juce::Colour (0xff1a1a1e));
        g.fillRoundedRectangle (deck, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (deck, 8.0f, 1.2f);

        bool recording = recorder.isRecording();
        float reelY = deck.getCentreY();
        drawOpenReel (g, deck.getX() + deck.getWidth() * 0.27f, reelY, 38.0f, reelAngle);
        drawOpenReel (g, deck.getX() + deck.getWidth() * 0.73f, reelY, 38.0f, reelAngle * 1.03f);
        // Tape path across the heads
        g.setColour (juce::Colour (0xff2a2018));
        g.drawLine (deck.getX() + deck.getWidth() * 0.27f, reelY + 40.0f,
                    deck.getX() + deck.getWidth() * 0.73f, reelY + 40.0f, 2.5f);

        // ---- REC LED ----
        auto led = juce::Rectangle<float> (deck.getCentreX() - 6, deck.getBottom() - 18, 12, 12);
        g.setColour (recording ? juce::Colour (0xffff3333) : juce::Colour (0xff552222));
        g.fillEllipse (led);
        if (recording)
        {
            g.setColour (juce::Colour (0x55ff3333));
            g.fillEllipse (led.expanded (4.0f));
        }

        // Folder path readout
        g.setColour (juce::Colour (0xff777788));
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
        g.drawFittedText ("Folder: " + outputFolder.getFullPathName(),
                          16, (int) deck.getBottom() + 6, (int) b.getWidth() - 32, 14,
                          juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (134);  // painted deck + folder line

        auto capRow = area.removeFromTop (40);
        captureLabel.setBounds (capRow.removeFromLeft (60).withSizeKeepingCentre (60, 16));
        captureBox.setBounds (capRow.reduced (2, 8));

        area.removeFromTop (4);
        auto transport = area.removeFromTop (40);
        recButton.setBounds  (transport.removeFromLeft (transport.getWidth() / 2).reduced (3));
        stopButton.setBounds (transport.reduced (3));

        area.removeFromTop (6);
        auto fileRow = area.removeFromTop (28);
        folderButton.setBounds (fileRow.removeFromLeft (fileRow.getWidth() / 2).reduced (2, 0));
        revealButton.setBounds (fileRow.reduced (2, 0));
    }

private:
    static void drawOpenReel (juce::Graphics& g, float cx, float cy, float r, float angle)
    {
        // Outer rim
        g.setColour (juce::Colour (0xff3a3a42));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colour (0xff15151a));
        g.fillEllipse (cx - r * 0.85f, cy - r * 0.85f, r * 1.7f, r * 1.7f);
        // Wound tape (dark disc)
        g.setColour (juce::Colour (0xff241c14));
        g.fillEllipse (cx - r * 0.7f, cy - r * 0.7f, r * 1.4f, r * 1.4f);
        // Spokes
        g.setColour (juce::Colour (0xff55555f));
        for (int i = 0; i < 3; ++i)
        {
            float a = angle + i * juce::MathConstants<float>::twoPi / 3.0f;
            float x1 = cx + std::cos (a) * r * 0.28f;
            float y1 = cy + std::sin (a) * r * 0.28f;
            float x2 = cx + std::cos (a) * r * 0.66f;
            float y2 = cy + std::sin (a) * r * 0.66f;
            g.drawLine (x1, y1, x2, y2, 3.0f);
        }
        // Hub
        g.setColour (juce::Colour (0xff888892));
        g.fillEllipse (cx - r * 0.22f, cy - r * 0.22f, r * 0.44f, r * 0.44f);
        g.setColour (juce::Colour (0xff222228));
        g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    }

    Recorder& recorder;
    std::function<juce::File()> getDefaultFolder;
    juce::File outputFolder;
    float reelAngle = 0.0f;

    juce::TextButton recButton    { "REC" };
    juce::TextButton stopButton   { "STOP" };
    juce::Label      captureLabel;
    juce::ComboBox   captureBox;
    juce::TextButton folderButton { "Set Folder..." };
    juce::TextButton revealButton { "Reveal Folder" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelRecorderContent)
};

class ReelRecorderWindow : public HardwareModuleWindow
{
public:
    ReelRecorderWindow (Recorder& rec, std::function<juce::File()> defaultFolderProvider)
        : HardwareModuleWindow ("Reel Recorder",
                                new ReelRecorderContent (rec, std::move (defaultFolderProvider)),
                                380, 300)
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelRecorderWindow)
};
