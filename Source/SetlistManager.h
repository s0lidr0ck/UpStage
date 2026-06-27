#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

class SceneManager;
class MidiLearnManager;

/**
 * SetlistManager
 *
 * Maintains an ordered list of songs (each backed by an .upstage file).
 * Two loading modes:
 *   - Full load (loadProjectData): replaces entire plugin chain + state
 *   - State-only load: restores channel states without reloading plugins
 *
 * Typical live workflow:
 *   - Before the gig: add songs in order, save setlist as .setlist file
 *   - On stage: MIDI PC or numpad +/- advances through songs
 *   - Plugin chains stay loaded; only fader/bypass/plugin-param states change
 *
 * MIDI integration (handled in MainComponent):
 *   - advancePCNumber (default: 127) triggers advance()
 *   - previousPCNumber (default: 126) triggers previous()
 */
class SetlistManager
{
public:
    SetlistManager();

    struct Song
    {
        juce::String name;
        juce::File   filePath;
        int          preferredSceneIndex = 0;
    };

    //==========================================================================
    // Setlist file management
    void            clear();
    void            addSong   (const juce::File& projectFile);
    void            addSong   (const Song& song);
    void            removeSong (int index);
    void            moveSong  (int fromIndex, int toIndex);
    int             getNumSongs()  const { return songs.size(); }
    juce::String    getSongName    (int index) const;
    juce::File      getSongFile    (int index) const;
    const Song*     getSong        (int index) const;
    int             getCurrentIndex() const { return currentIndex; }
    void            setCurrentIndex (int index);

    // Queue: select next song without loading it
    int             getQueuedIndex() const { return queuedIndex; }
    void            setQueuedIndex (int index);
    void            clearQueue();

    bool            saveSetlist (const juce::File& file) const;
    bool            loadSetlist (const juce::File& file);

    //==========================================================================
    // Navigation
    bool            canAdvance()  const { return ! songs.isEmpty() && currentIndex < songs.size() - 1; }
    bool            canGoBack()   const { return currentIndex > 0; }
    void            advance();
    void            previous();

    /** Load song's full ProjectData (replaces plugins). */
    bool            loadCurrentSong (ProjectData& dataOut);
    bool            loadSongAtIndex (int index, ProjectData& dataOut);

    /** Parse a song file and return only the channel/scene state data.
        Does not trigger callbacks — caller uses the data directly. */
    bool            loadSongStateData (int index, ProjectData& dataOut);

    //==========================================================================
    // MIDI trigger PC numbers
    int             advancePCNumber  = 127;
    int             previousPCNumber = 126;

    //==========================================================================
    // Callbacks
    std::function<void(int newIndex, const ProjectData&)> onSongChanged;

    void setSceneManager (SceneManager* sm)       { sceneMgr = sm; }
    void setMidiLearnManager (MidiLearnManager* m) { midiLearnMgr = m; }

private:
    juce::Array<Song> songs;
    int               currentIndex = 0;
    int               queuedIndex  = -1;
    ProjectState      projectState;
    SceneManager*     sceneMgr     = nullptr;
    MidiLearnManager* midiLearnMgr = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetlistManager)
};
