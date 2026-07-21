#pragma once
#include <JuceHeader.h>
#include "HardwareModuleWindow.h"
#include "InputRouter.h"

/**
 * CassetteDeckWindow
 *
 * A skeuomorphic cassette tape deck that plays a chosen audio file through the
 * signal chain (in place of the live guitar input) so the player can audition
 * tones against a backing track.  Wraps InputRouter's loop-file player.
 *
 *   LOAD   choose a .wav/.aiff/.mp3/.flac file
 *   PLAY   route the file into the chain and roll the tape (loops at the end)
 *   PAUSE  halt the tape, keep position
 *   STOP   halt, rewind to 0, hand the input back to the live guitar
 *
 * The reels spin while playing; the tape counter shows elapsed position.
 */
class CassetteDeckContent : public juce::Component,
                            private juce::Timer
{
public:
    explicit CassetteDeckContent (InputRouter& routerToUse)
        : router (routerToUse)
    {
        auto styleTransport = [this] (juce::TextButton& b, juce::Colour face, juce::Colour text)
        {
            b.setColour (juce::TextButton::buttonColourId, face);
            b.setColour (juce::TextButton::textColourOffId, text);
            b.setColour (juce::TextButton::textColourOnId, text.brighter (0.5f));
            addAndMakeVisible (b);
        };

        loadButton.onClick = [this] { chooseFile(); };
        styleTransport (loadButton, juce::Colour (0xff2a2a30), juce::Colour (0xffccccdd));

        playButton.onClick = [this] { play(); };
        styleTransport (playButton, juce::Colour (0xff1a3a1a), juce::Colour (0xff66dd66));
        playButton.setClickingTogglesState (false);

        pauseButton.onClick = [this] { pause(); };
        styleTransport (pauseButton, juce::Colour (0xff3a3a1a), juce::Colour (0xffdddd66));

        stopButton.onClick = [this] { stopAndRewind(); };
        styleTransport (stopButton, juce::Colour (0xff3a1a1a), juce::Colour (0xffdd6666));

        loopToggle.setButtonText ("LOOP");
        loopToggle.setClickingTogglesState (true);
        loopToggle.setToggleState (true, juce::dontSendNotification);
        loopToggle.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a30));
        loopToggle.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a5a2a));
        loopToggle.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff888899));
        loopToggle.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff88dd88));
        addAndMakeVisible (loopToggle);

        positionSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        positionSlider.setRange (0.0, 1.0, 0.001);
        positionSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        positionSlider.onDragStart = [this] { scrubbing = true; };
        positionSlider.onDragEnd   = [this]
        {
            router.setLoopPosition (positionSlider.getValue());
            scrubbing = false;
        };
        addAndMakeVisible (positionSlider);

        volumeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        volumeSlider.setRange (0.0, 1.0, 0.01);
        volumeSlider.setValue (router.loopVolume.load(), juce::dontSendNotification);
        volumeSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
        volumeSlider.setTextValueSuffix ("");
        volumeSlider.onValueChange = [this]
        {
            router.loopVolume.store ((float) volumeSlider.getValue());
        };
        addAndMakeVisible (volumeSlider);

        volumeLabel.setText ("VOLUME", juce::dontSendNotification);
        volumeLabel.setJustificationType (juce::Justification::centred);
        volumeLabel.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
        volumeLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888899));
        addAndMakeVisible (volumeLabel);

        startTimerHz (20);
    }

    ~CassetteDeckContent() override { stopTimer(); }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Load Tape", juce::File{}, "*.wav;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile() && router.loadLoopFile (result))
                {
                    fileName = result.getFileName();
                    repaint();
                }
            });
    }

    void play()
    {
        if (router.getLoopFileName().isEmpty() && fileName.isEmpty())
            return;
        router.setMode (InputRouter::Mode::Loop);
        router.setLoopPlaying (true);
    }

    void pause()
    {
        router.setLoopPlaying (false);  // mode stays Loop, position retained
    }

    void stopAndRewind()
    {
        router.setLoopPlaying (false);
        router.setLoopPosition (0.0);
        router.setMode (InputRouter::Mode::Live);  // hand input back to the guitar
    }

    void timerCallback() override
    {
        bool playing = router.isLoopPlaying();
        if (playing)
            reelAngle += 0.18f;

        if (! scrubbing)
            positionSlider.setValue (router.getLoopPosition(), juce::dontSendNotification);

        // Repaint only the animated areas.
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // ---- Deck body: brushed dark metal ----
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c30), 0, 0,
                                                 juce::Colour (0xff161618), 0, b.getHeight(), false));
        g.fillRect (b);

        // ---- Cassette window recess ----
        auto window = juce::Rectangle<float> (24, 38, b.getWidth() - 48, 120);
        g.setColour (juce::Colour (0xff0c0c0e));
        g.fillRoundedRectangle (window, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (window, 8.0f, 1.5f);

        // ---- Cassette shell inside the window ----
        auto shell = window.reduced (12.0f);
        g.setColour (juce::Colour (0xff1c1c22));
        g.fillRoundedRectangle (shell, 6.0f);
        g.setColour (juce::Colour (0xff33333c));
        g.drawRoundedRectangle (shell, 6.0f, 1.0f);

        // Label strip on the cassette
        auto labelStrip = shell.reduced (10.0f).removeFromTop (26.0f);
        g.setColour (juce::Colour (0xffd8d0c0));
        g.fillRoundedRectangle (labelStrip, 3.0f);
        g.setColour (juce::Colour (0xff222222));
        g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
        g.drawFittedText (fileName.isEmpty() ? "— no tape loaded —" : fileName,
                          labelStrip.toNearestInt().reduced (6, 0),
                          juce::Justification::centredLeft, 1);

        // ---- Two reels ----
        float reelY = shell.getCentreY() + 12.0f;
        float reelR = 26.0f;
        float fill  = (float) juce::jlimit (0.0, 1.0, router.getLoopPosition());
        // Left reel empties, right fills as the tape advances.
        drawReel (g, shell.getX() + shell.getWidth() * 0.28f, reelY, reelR,
                  reelAngle, 1.0f - fill * 0.6f);
        drawReel (g, shell.getX() + shell.getWidth() * 0.72f, reelY, reelR,
                  reelAngle, 0.4f + fill * 0.6f);

        // Tape spanning the reels
        g.setColour (juce::Colour (0xff3a2a18));
        g.drawLine (shell.getX() + shell.getWidth() * 0.28f, reelY - reelR - 2.0f,
                    shell.getX() + shell.getWidth() * 0.72f, reelY - reelR - 2.0f, 3.0f);

        // ---- Status LED ----
        bool playing = router.isLoopPlaying();
        auto led = juce::Rectangle<float> (b.getRight() - 30, 14, 12, 12);
        g.setColour (playing ? juce::Colour (0xff33ee55) : juce::Colour (0xff224422));
        g.fillEllipse (led);
        if (playing)
        {
            g.setColour (juce::Colour (0x5533ee55));
            g.fillEllipse (led.expanded (4.0f));
        }
        g.setColour (juce::Colour (0xff888899));
        g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        g.drawText ("PLAY", (int) b.getX() + 24, 14, 60, 14, juce::Justification::left);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (158);  // space taken by the painted cassette window

        // Transport row
        auto row = area.removeFromTop (34);
        int bw = (row.getWidth() - 24) / 5;
        loadButton .setBounds (row.removeFromLeft (bw).reduced (2));
        playButton .setBounds (row.removeFromLeft (bw).reduced (2));
        pauseButton.setBounds (row.removeFromLeft (bw).reduced (2));
        stopButton .setBounds (row.removeFromLeft (bw).reduced (2));
        loopToggle .setBounds (row.removeFromLeft (bw).reduced (2));

        area.removeFromTop (8);

        // Position scrubber
        positionSlider.setBounds (area.removeFromTop (24).reduced (4, 0));

        area.removeFromTop (8);

        // Volume knob on the right
        auto vol = area.removeFromRight (80);
        volumeLabel.setBounds (vol.removeFromTop (14));
        volumeSlider.setBounds (vol.reduced (4));
    }

private:
    static void drawReel (juce::Graphics& g, float cx, float cy, float r,
                          float angle, float tapeAmount)
    {
        // Tape pack (brown disc whose radius reflects how much tape is wound on)
        float packR = juce::jmap (juce::jlimit (0.0f, 1.0f, tapeAmount), 0.45f, 1.0f) * r;
        g.setColour (juce::Colour (0xff4a3420));
        g.fillEllipse (cx - packR, cy - packR, packR * 2.0f, packR * 2.0f);

        // Hub
        g.setColour (juce::Colour (0xff111114));
        g.fillEllipse (cx - r * 0.45f, cy - r * 0.45f, r * 0.9f, r * 0.9f);
        g.setColour (juce::Colour (0xff55555f));
        g.drawEllipse (cx - r * 0.45f, cy - r * 0.45f, r * 0.9f, r * 0.9f, 1.0f);

        // Three spokes that rotate
        g.setColour (juce::Colour (0xffaaaab0));
        for (int i = 0; i < 3; ++i)
        {
            float a = angle + i * juce::MathConstants<float>::twoPi / 3.0f;
            float x2 = cx + std::cos (a) * r * 0.4f;
            float y2 = cy + std::sin (a) * r * 0.4f;
            g.drawLine (cx, cy, x2, y2, 2.5f);
        }
        g.setColour (juce::Colour (0xff222228));
        g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    }

    InputRouter& router;
    juce::String fileName;
    float reelAngle = 0.0f;
    bool  scrubbing = false;

    juce::TextButton loadButton  { "LOAD" };
    juce::TextButton playButton  { "PLAY" };
    juce::TextButton pauseButton { "PAUSE" };
    juce::TextButton stopButton  { "STOP" };
    juce::TextButton loopToggle;
    juce::Slider     positionSlider;
    juce::Slider     volumeSlider;
    juce::Label      volumeLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CassetteDeckContent)
};

class CassetteDeckWindow : public HardwareModuleWindow
{
public:
    explicit CassetteDeckWindow (InputRouter& router)
        : HardwareModuleWindow ("Cassette Deck", new CassetteDeckContent (router), 360, 320)
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CassetteDeckWindow)
};
