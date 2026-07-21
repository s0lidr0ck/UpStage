#pragma once
#include <JuceHeader.h>

/**
 * HardwareModuleWindow
 *
 * Base class for the floating "hardware module" panels that toolbar icons open
 * (Cassette Deck, Loop Station, Metronome, Reel Recorder).  These behave like
 * the plugin editor windows and the MIDI Monitor: a non-native DocumentWindow
 * whose close button just hides it (the MainComponent owns the lifetime), so it
 * can be re-shown without reconstruction.
 *
 * The window adopts the application's default LookAndFeel (MixerLookAndFeel),
 * so child controls inherit the console styling for free.
 */
class HardwareModuleWindow : public juce::DocumentWindow
{
public:
    HardwareModuleWindow (const juce::String& name, juce::Component* contentToOwn,
                          int w, int h)
        : DocumentWindow (name, juce::Colour (0xff1a1a1a), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        setContentOwned (contentToOwn, false);
        setResizable (false, false);
        setSize (w, h);
        centreWithSize (w, h);
    }

    // Hide rather than destroy — MainComponent keeps the unique_ptr alive.
    void closeButtonPressed() override { setVisible (false); }

    /** Toggle visibility; bring to front when shown. */
    void toggleVisible()
    {
        setVisible (! isVisible());
        if (isVisible())
            toFront (true);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareModuleWindow)
};
