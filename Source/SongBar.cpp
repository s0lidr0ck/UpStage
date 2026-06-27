#include "SongBar.h"

SongBar::SongBar (SetlistManager& mgr) : manager (mgr)
{
    // Prev / Next arrows — raised beveled buttons
    for (auto* btn : { &prevButton, &nextButton })
    {
        btn->addListener (this);
        btn->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3632));
        btn->setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffddc888));
        addAndMakeVisible (btn);
    }

    // Song name button — deep recessed LCD display
    songButton.addListener (this);
    songButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff080c06));
    songButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff33ee55));
    addAndMakeVisible (songButton);

    // "Next up" label — warm amber, overlays on LCD
    nextUpLabel.setColour (juce::Label::textColourId, juce::Colour (0xffbb8833));
    nextUpLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x00000000));
    nextUpLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    nextUpLabel.setJustificationType (juce::Justification::centredRight);
    nextUpLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nextUpLabel);

    refresh();
}

void SongBar::refresh()
{
    int idx = manager.getCurrentIndex();
    int num = manager.getNumSongs();

    if (num == 0)
    {
        songButton.setButtonText ("-- No Setlist --");
        songButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff667766));
        nextUpLabel.setText ("", juce::dontSendNotification);
    }
    else
    {
        songButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff33ee55));
        songButton.setButtonText (
            juce::String (idx + 1) + " / " + juce::String (num)
            + "   " + manager.getSongName (idx));

        int nextIdx = manager.getQueuedIndex();
        if (nextIdx < 0)
            nextIdx = idx + 1;

        if (nextIdx >= 0 && nextIdx < num)
            nextUpLabel.setText ("NEXT: " + manager.getSongName (nextIdx),
                                juce::dontSendNotification);
        else
            nextUpLabel.setText ("", juce::dontSendNotification);
    }

    prevButton.setEnabled (manager.canGoBack());
    nextButton.setEnabled (manager.canAdvance() || manager.getQueuedIndex() >= 0);

    prevButton.setAlpha (prevButton.isEnabled() ? 1.0f : 0.35f);
    nextButton.setAlpha (nextButton.isEnabled() ? 1.0f : 0.35f);

    repaint();
}

//==============================================================================
void SongBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float w = bounds.getWidth();
    float h = bounds.getHeight();

    // Solid dark recessed panel — distinctly darker than surroundings
    g.setColour (juce::Colour (0xff151311));
    g.fillRect (bounds);

    // Top inner shadow (recessed groove)
    g.setColour (juce::Colour (0xff0a0908));
    g.fillRect (0.0f, 0.0f, w, 2.0f);

    // Bottom highlight (raised edge below)
    g.setColour (juce::Colour (0xff3a3836));
    g.fillRect (0.0f, h - 1.0f, w, 1.0f);

    // Separator lines — top and bottom grooves
    g.setColour (juce::Colour (0xff080706));
    g.drawHorizontalLine (0, 0.0f, w);
    g.setColour (juce::Colour (0x20ffffff));
    g.drawHorizontalLine ((int) h - 1, 0.0f, w);

    // "SETLIST" label etched into the panel on the far left
    g.setColour (juce::Colour (0xff555045));
    g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText ("SETLIST", 6, 0, 44, (int) h, juce::Justification::centredLeft);
}

void SongBar::resized()
{
    auto area = getLocalBounds().reduced (4, 3);

    // "SETLIST" label space on far left
    area.removeFromLeft (46);

    // Prev button on left
    prevButton.setBounds (area.removeFromLeft (30));
    area.removeFromLeft (3);

    // Next button on far right
    nextButton.setBounds (area.removeFromRight (30));
    area.removeFromRight (3);

    // Next-up label overlaps the right side of the LCD area
    // Position it absolutely so it doesn't steal space from the LCD
    int nextW = juce::jmin (180, area.getWidth() / 3);
    nextUpLabel.setBounds (area.getRight() - nextW, area.getY(),
                           nextW, area.getHeight());

    // Song LCD button fills the entire remaining center
    songButton.setBounds (area);
}

//==============================================================================
void SongBar::buttonClicked (juce::Button* b)
{
    if (b == &prevButton)
    {
        manager.previous();
        refresh();
    }
    else if (b == &nextButton)
    {
        manager.advance();
        refresh();
    }
    else if (b == &songButton)
    {
        showSongMenu();
    }
}

void SongBar::showSongMenu()
{
    juce::PopupMenu menu;

    if (manager.getNumSongs() > 0)
    {
        int current = manager.getCurrentIndex();
        for (int i = 0; i < manager.getNumSongs(); ++i)
            menu.addItem (i + 1, manager.getSongName (i), true, i == current);

        menu.addSeparator();
        menu.addItem (10000, "Save Setlist...");
        menu.addItem (10001, "Clear Setlist");
    }
    else
    {
        auto recentSetlists = loadRecentSetlists();
        if (! recentSetlists.isEmpty())
        {
            for (int i = 0; i < recentSetlists.size(); ++i)
            {
                auto f = juce::File (recentSetlists[i]);
                menu.addItem (20000 + i, f.getFileNameWithoutExtension());
            }
            menu.addSeparator();
        }

        menu.addItem (10002, "Open Setlist...");
    }

    menu.showMenuAsync ({}, [this] (int result)
    {
        if (result <= 0) return;

        if (result < 10000)
        {
            int idx = result - 1;
            manager.setCurrentIndex (idx);
            if (onSongSelected) onSongSelected (idx);
            refresh();
        }
        else if (result == 10000)
        {
            if (onSaveSetlist) onSaveSetlist();
        }
        else if (result == 10001)
        {
            if (onClearSetlist) onClearSetlist();
            refresh();
        }
        else if (result == 10002)
        {
            if (onOpenSetlist) onOpenSetlist();
        }
        else if (result >= 20000)
        {
            auto recents = loadRecentSetlists();
            int idx = result - 20000;
            if (idx < recents.size())
            {
                juce::File f (recents[idx]);
                if (f.existsAsFile() && onLoadSetlistFile)
                {
                    onLoadSetlistFile (f);
                    refresh();
                }
            }
        }
    });
}

juce::StringArray SongBar::loadRecentSetlists()
{
    auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("UpStage").getChildFile ("RecentSetlists.txt");
    juce::StringArray result;
    if (file.existsAsFile())
    {
        file.readLines (result);
        result.removeEmptyStrings();
    }
    return result;
}

void SongBar::addToRecentSetlists (const juce::File& setlistFile)
{
    auto recentsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile ("UpStage").getChildFile ("RecentSetlists.txt");
    auto existing = loadRecentSetlists();
    existing.removeString (setlistFile.getFullPathName());
    existing.insert (0, setlistFile.getFullPathName());
    while (existing.size() > 8)
        existing.remove (existing.size() - 1);

    recentsFile.getParentDirectory().createDirectory();
    recentsFile.replaceWithText (existing.joinIntoString ("\n"));
}
