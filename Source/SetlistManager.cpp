#include "SetlistManager.h"

SetlistManager::SetlistManager() {}

//==============================================================================
void SetlistManager::clear()
{
    songs.clear();
    currentIndex = 0;
}

void SetlistManager::addSong (const juce::File& f)
{
    songs.add (f);
}

void SetlistManager::removeSong (int index)
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        songs.remove (index);
    currentIndex = juce::jlimit (0, juce::jmax (0, songs.size() - 1), currentIndex);
}

void SetlistManager::moveSong (int from, int to)
{
    songs.move (from, to);
}

juce::String SetlistManager::getSongName (int index) const
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        return songs[index].getFileNameWithoutExtension();
    return {};
}

juce::File SetlistManager::getSongFile (int index) const
{
    if (juce::isPositiveAndBelow (index, songs.size()))
        return songs[index];
    return {};
}

void SetlistManager::setCurrentIndex (int index)
{
    currentIndex = juce::jlimit (0, juce::jmax (0, songs.size() - 1), index);
}

//==============================================================================
bool SetlistManager::saveSetlist (const juce::File& file) const
{
    juce::XmlElement root ("Setlist");
    for (const auto& f : songs)
    {
        auto* item = root.createNewChildElement ("Song");
        item->setAttribute ("path", f.getFullPathName());
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
            songs.add (juce::File (child->getStringAttribute ("path")));
    }
    currentIndex = juce::jlimit (0, juce::jmax (0, songs.size() - 1),
                                  xml->getIntAttribute ("currentIndex", 0));
    return true;
}

//==============================================================================
void SetlistManager::advance()
{
    if (! canAdvance()) return;
    ++currentIndex;
    ProjectData data;
    if (loadCurrentSong (data) && onSongChanged)
        onSongChanged (currentIndex, data);
}

void SetlistManager::previous()
{
    if (! canGoBack()) return;
    --currentIndex;
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
    return projectState.loadFromFile (songs[index], dataOut);
}
