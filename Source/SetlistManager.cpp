#include "SetlistManager.h"

SetlistManager::SetlistManager() {}

//==============================================================================
void SetlistManager::clear()
{
    songs.clear();
    currentIndex = 0;
    queuedIndex  = -1;
}

void SetlistManager::addSong (const juce::File& f)
{
    Song s;
    s.name     = f.getFileNameWithoutExtension();
    s.filePath = f;
    songs.add (s);
}

void SetlistManager::addSong (const Song& song)
{
    songs.add (song);
}

void SetlistManager::removeSong (int index)
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        songs.remove (index);
    currentIndex = juce::jlimit (0, juce::jmax (0, songs.size() - 1), currentIndex);
    if (queuedIndex >= songs.size()) queuedIndex = -1;
}

void SetlistManager::moveSong (int from, int to)
{
    songs.move (from, to);
}

juce::String SetlistManager::getSongName (int index) const
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        return songs[index].name;
    return {};
}

juce::File SetlistManager::getSongFile (int index) const
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        return songs[index].filePath;
    return {};
}

const SetlistManager::Song* SetlistManager::getSong (int index) const
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        return &songs.getReference (index);
    return nullptr;
}

void SetlistManager::setCurrentIndex (int index)
{
    currentIndex = juce::jlimit (0, juce::jmax (0, songs.size() - 1), index);
}

void SetlistManager::setQueuedIndex (int index)
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        queuedIndex = index;
}

void SetlistManager::clearQueue()
{
    queuedIndex = -1;
}

//==============================================================================
bool SetlistManager::saveSetlist (const juce::File& file) const
{
    juce::XmlElement root ("Setlist");
    for (const auto& s : songs)
    {
        auto* item = root.createNewChildElement ("Song");
        item->setAttribute ("path", s.filePath.getFullPathName());
        item->setAttribute ("name", s.name);
        item->setAttribute ("preferredScene", s.preferredSceneIndex);
    }
    root.setAttribute ("currentIndex", currentIndex);
    return root.writeTo (file);
}

bool SetlistManager::loadSetlist (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || xml->getTagName() != "Setlist") return false;

    songs.clear();
    for (auto* child : xml->getChildIterator())
    {
        if (child->getTagName() == "Song")
        {
            Song s;
            s.filePath            = juce::File (child->getStringAttribute ("path"));
            s.name                = child->getStringAttribute ("name",
                                        s.filePath.getFileNameWithoutExtension());
            s.preferredSceneIndex = child->getIntAttribute ("preferredScene", 0);
            songs.add (s);
        }
    }
    currentIndex = juce::jlimit (0, juce::jmax (0, songs.size() - 1),
                                  xml->getIntAttribute ("currentIndex", 0));
    queuedIndex = -1;
    return true;
}

//==============================================================================
void SetlistManager::advance()
{
    if (queuedIndex >= 0 && queuedIndex != currentIndex)
    {
        currentIndex = queuedIndex;
        queuedIndex = -1;
    }
    else if (canAdvance())
    {
        ++currentIndex;
    }
    else
    {
        return;
    }

    ProjectData data;
    if (loadCurrentSong (data) && onSongChanged)
        onSongChanged (currentIndex, data);
}

void SetlistManager::previous()
{
    if (! canGoBack()) return;
    --currentIndex;
    queuedIndex = -1;
    ProjectData data;
    if (loadCurrentSong (data) && onSongChanged)
        onSongChanged (currentIndex, data);
}

bool SetlistManager::loadCurrentSong (ProjectData& dataOut)
{
    return loadSongAtIndex (currentIndex, dataOut);
}

bool SetlistManager::loadSongAtIndex (int index, ProjectData& dataOut)
{
    if (! juce::isPositiveAndBelow (index, songs.size())) return false;
    return projectState.loadFromFile (songs[index].filePath, dataOut, sceneMgr, midiLearnMgr);
}

bool SetlistManager::loadSongStateData (int index, ProjectData& dataOut)
{
    return loadSongAtIndex (index, dataOut);
}
