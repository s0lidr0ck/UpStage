#include "SetlistPanel.h"

SetlistPanel::SetlistPanel (SetlistManager& mgr) : manager (mgr)
{
    addAndMakeVisible (listBox);
    listBox.setModel (this);
    listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff111122));
    listBox.setRowHeight (28);

    for (auto* btn : { &addButton, &removeButton, &upButton, &downButton,
                       &prevButton, &nextButton, &queueButton, &saveSongButton,
                       &saveSetlistButton, &loadSetlistButton })
    {
        btn->addListener (this);
        addAndMakeVisible (btn);
    }

    nextUpLabel.setColour (juce::Label::textColourId, juce::Colour (0xffddbb44));
    nextUpLabel.setFont (juce::Font (juce::FontOptions().withHeight (13.0f).withStyle ("Italic")));
    addAndMakeVisible (nextUpLabel);

    setSize (340, 520);
    refresh();
}

SetlistPanel::~SetlistPanel() {}

void SetlistPanel::refresh()
{
    listBox.updateContent();
    listBox.selectRow (manager.getCurrentIndex());

    // Update "next up" label
    int nextIdx = manager.getQueuedIndex();
    if (nextIdx < 0)
        nextIdx = manager.getCurrentIndex() + 1;

    if (nextIdx >= 0 && nextIdx < manager.getNumSongs())
        nextUpLabel.setText ("Next: " + manager.getSongName (nextIdx),
                            juce::dontSendNotification);
    else
        nextUpLabel.setText ("", juce::dontSendNotification);

    repaint();
}

//==============================================================================
void SetlistPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a2e));
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
    g.drawText ("Setlist", getLocalBounds().removeFromTop (28), juce::Justification::centred);
}

void SetlistPanel::resized()
{
    auto area = getLocalBounds().reduced (6);
    area.removeFromTop (28); // title

    // Top toolbar
    auto toolbar = area.removeFromTop (30);
    addButton   .setBounds (toolbar.removeFromLeft (90));
    toolbar.removeFromLeft (4);
    removeButton.setBounds (toolbar.removeFromLeft (70));
    toolbar.removeFromLeft (4);
    upButton    .setBounds (toolbar.removeFromLeft (30));
    downButton  .setBounds (toolbar.removeFromLeft (30));

    area.removeFromTop (4);

    // Bottom controls
    auto bottom = area.removeFromBottom (110);

    // Next up label
    nextUpLabel.setBounds (area.removeFromBottom (22));

    // List takes remaining space
    area.removeFromBottom (4);
    listBox.setBounds (area);

    // Navigation row
    auto navRow = bottom.removeFromTop (30);
    prevButton.setBounds (navRow.removeFromLeft (80));
    navRow.removeFromLeft (4);
    nextButton.setBounds (navRow.removeFromLeft (80));
    navRow.removeFromLeft (4);
    queueButton.setBounds (navRow.removeFromLeft (60));

    bottom.removeFromTop (4);

    // Save song row
    auto songRow = bottom.removeFromTop (30);
    saveSongButton.setBounds (songRow.removeFromLeft (120));

    bottom.removeFromTop (4);

    // Setlist file row
    auto fileRow = bottom.removeFromTop (30);
    saveSetlistButton.setBounds (fileRow.removeFromLeft (110));
    fileRow.removeFromLeft (4);
    loadSetlistButton.setBounds (fileRow.removeFromLeft (110));
}

//==============================================================================
int SetlistPanel::getNumRows()
{
    return manager.getNumSongs();
}

void SetlistPanel::paintListBoxItem (int row, juce::Graphics& g,
                                     int width, int height, bool selected)
{
    bool isCurrent = (row == manager.getCurrentIndex());
    bool isQueued  = (row == manager.getQueuedIndex());

    if (isCurrent)
        g.fillAll (juce::Colour (0xff2a6a2a));  // green for active
    else if (isQueued)
        g.fillAll (juce::Colour (0xff6a5a1a));  // amber for queued
    else if (selected)
        g.fillAll (juce::Colour (0xff2a2a5a));

    g.setColour (isCurrent ? juce::Colour (0xff88ff88)
                           : (isQueued ? juce::Colour (0xffffdd66) : juce::Colours::white));
    g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));

    juce::String text = juce::String (row + 1) + ".  " + manager.getSongName (row);
    g.drawText (text, 8, 0, width - 8, height, juce::Justification::centredLeft);
}

void SetlistPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    manager.setCurrentIndex (row);
    manager.clearQueue();
    if (onSongSelected) onSongSelected (row);
    refresh();
}

//==============================================================================
void SetlistPanel::buttonClicked (juce::Button* b)
{
    if (b == &addButton)
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Add Song to Setlist", juce::File{}, "*.upstage");
        chooser->launchAsync (juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile()) { manager.addSong (result); refresh(); }
            });
    }
    else if (b == &removeButton)
    {
        int sel = listBox.getSelectedRow();
        if (sel >= 0) { manager.removeSong (sel); refresh(); }
    }
    else if (b == &upButton)
    {
        int sel = listBox.getSelectedRow();
        if (sel > 0) { manager.moveSong (sel, sel - 1); listBox.selectRow (sel - 1); refresh(); }
    }
    else if (b == &downButton)
    {
        int sel = listBox.getSelectedRow();
        if (sel >= 0 && sel < manager.getNumSongs() - 1)
            { manager.moveSong (sel, sel + 1); listBox.selectRow (sel + 1); refresh(); }
    }
    else if (b == &prevButton)
    {
        manager.previous();
        refresh();
    }
    else if (b == &nextButton)
    {
        manager.advance();
        refresh();
    }
    else if (b == &queueButton)
    {
        int sel = listBox.getSelectedRow();
        if (sel >= 0 && sel != manager.getCurrentIndex())
        {
            manager.setQueuedIndex (sel);
            refresh();
        }
    }
    else if (b == &saveSongButton)
    {
        if (onSaveSongRequested) onSaveSongRequested();
    }
    else if (b == &saveSetlistButton)
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Save Setlist", juce::File{}, "*.setlist");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode |
                              juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result != juce::File{})
                    manager.saveSetlist (result.withFileExtension ("setlist"));
            });
    }
    else if (b == &loadSetlistButton)
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Load Setlist", juce::File{}, "*.setlist");
        chooser->launchAsync (juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile()) { manager.loadSetlist (result); refresh(); }
            });
    }
}
