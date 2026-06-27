#pragma once
#include <JuceHeader.h>
#include "SetlistManager.h"

/**
 * SongBar — persistent song/setlist navigation strip.
 *
 * Sits below the menu bar, always visible.  Styled as a brushed-metal
 * hardware panel strip with recessed LCD-style song readout.
 *
 *  [ ◀ ]  [ 3. Paranoid ]  [ ▶ ]        Next: 4. Iron Man
 */
class SongBar : public juce::Component,
                public juce::Button::Listener
{
public:
    explicit SongBar (SetlistManager& manager);

    void refresh();

    std::function<void(int index)> onSongSelected;
    std::function<void()>          onOpenSetlist;
    std::function<void()>          onSaveSetlist;
    std::function<void()>          onClearSetlist;
    std::function<void(const juce::File&)> onLoadSetlistFile;

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void buttonClicked (juce::Button* b) override;

    static juce::StringArray loadRecentSetlists();
    static void addToRecentSetlists (const juce::File& setlistFile);

private:
    SetlistManager& manager;

    juce::TextButton prevButton  { "<<" };
    juce::TextButton nextButton  { ">>" };
    juce::TextButton songButton;
    juce::Label      nextUpLabel;

    void showSongMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongBar)
};
