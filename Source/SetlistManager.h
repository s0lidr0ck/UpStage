#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

/**
 * SetlistManager
 *
 * Maintains an ordered list of .upstage project files (songs).
 * Advancing through the setlist loads the next song's ProjectData.
 *
 * Typical live workflow:
 *   - Before the gig: add songs in order, save setlist as .setlist file
 *   - On stage: MIDI PC message (configurable) advances to next song
 *
 * MIDI integration (handled in MainComponent):
 *   - advancePCNumber (default: 127) triggers advance()
 *   - previousPCNumber (default: 126) triggers previous()
 */
class SetlistManager
{
public:
    SetlistManager();

    //==========================================================================
    // Setlist file management
    void            clear();
    void            addSong   (const juce::File& projectFile);
    void            removeSong (int index);
    void            moveSong  (int fromIndex, int toIndex);
    int             getNumSongs()  const { return songs.size(); }
    juce::String    getSongName    (int index) const;
    juce::File      getSongFile    (int index) const;
    int             getCurrentIndex() const { return currentIndex; }
    void            setCurrentIndex (int index);

    bool            saveSetlist (const juce::File& file) const;
    bool            loadSetlist (const juce::File& file);

    //==========================================================================
    // Navigation
    bool            canAdvance()  const { return currentIndex < songs.size() - 1; }
    bool            canGoBack()   const { return currentIndex > 0; }
    void            advance();
    void            previous();

    /** Returns the ProjectData for the current song, or false if none/invalid. */
    bool            loadCurrentSong (ProjectData& dataOut);
    bool            loadSongAtIndex (int index, ProjectData& dataOut);

    //==========================================================================
    // MIDI trigger PC numbers
    int             advancePCNumber  = 127;
    int             previousPCNumber = 126;

    //==========================================================================
    // Callback fired when the current song changes (call from message thread)
    std::function<void(int newIndex, const ProjectData&)> onSongChanged;

private:
    juce::Array<juce::File> songs;
    int                     currentIndex = 0;
    ProjectState            projectState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetlistManager)
};
