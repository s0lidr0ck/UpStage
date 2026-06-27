#pragma once
#include <JuceHeader.h>
#include "SetlistManager.h"

/**
 * SetlistPanel
 *
 * UI panel showing the song list for the current setlist.
 * Displayed in a side panel or popup window.
 *
 * Features:
 *   - Add/remove/reorder songs
 *   - Current song highlighted in green, queued song in amber
 *   - "Next up" label shows the upcoming song
 *   - Advance/previous buttons + queue support
 *   - Save current state as a new song
 *   - Save/load .setlist files
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

    void refresh();

    std::function<void(int index)>  onSongSelected;
    std::function<void()>           onSaveSongRequested;

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
    juce::TextButton addButton       { "+ Add Song" };
    juce::TextButton removeButton    { "Remove" };
    juce::TextButton upButton        { "Up" };
    juce::TextButton downButton      { "Dn" };
    juce::TextButton prevButton      { "<< Prev" };
    juce::TextButton nextButton      { "Next >>" };
    juce::TextButton queueButton     { "Queue" };
    juce::TextButton saveSongButton  { "Save as Song" };
    juce::TextButton saveSetlistButton { "Save Setlist" };
    juce::TextButton loadSetlistButton { "Load Setlist" };
    juce::Label      nextUpLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetlistPanel)
};
