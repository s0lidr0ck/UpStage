#pragma once
#include <JuceHeader.h>
#include "SetlistManager.h"

/**
 * SetlistPanel
 *
 * UI panel showing the song list for the current setlist.
 * Displayed in a side panel or popup window.
 *
 * Lets the user:
 *   - Add songs (browse for .upstage files)
 *   - Remove songs
 *   - Reorder songs (drag-to-move buttons)
 *   - See which song is currently loaded (highlighted)
 *   - Advance / go back manually
 *
 * Wire onSongSelected to load the selected project in MainComponent.
 */
class SetlistPanel : public juce::Component,
                     public juce::ListBoxModel,
                     public juce::Button::Listener
{
public:
    explicit SetlistPanel (SetlistManager& manager);
    ~SetlistPanel() override;

    void refresh(); // call when setlist contents change

    std::function<void(int index)> onSongSelected;

    // Component
    void paint   (juce::Graphics& g) override;
    void resized () override;

    // ListBoxModel
    int     getNumRows() override;
    void    paintListBoxItem (int row, juce::Graphics& g,
                              int width, int height,
                              bool rowIsSelected) override;
    void    listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    // Button::Listener
    void buttonClicked (juce::Button* b) override;

private:
    SetlistManager& manager;

    juce::ListBox    listBox;
    juce::TextButton addButton    { "+ Add Song" };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton upButton     { "\xE2\x96\xB2" };
    juce::TextButton downButton   { "\xE2\x96\xBC" };
    juce::TextButton prevButton   { "\xE2\x97\x80 Prev" };
    juce::TextButton nextButton   { "Next \xE2\x96\xB6" };
    juce::TextButton saveSetlistButton { "Save Setlist" };
    juce::TextButton loadSetlistButton { "Load Setlist" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetlistPanel)
};
