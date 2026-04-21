#include "SetlistPanel.h"

SetlistPanel::SetlistPanel (SetlistManager& mgr) : manager (mgr)
{
    addAndMakeVisible (listBox);
    listBox.setModel (this);
    listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff111122));
    listBox.setRowHeight (28);

    for (auto* btn : { &addButton, &removeButton, &upButton, &downButton,
                       &prevButton, &nextButton, &saveSetlistButton, &loadSetlistButton })
    {
        btn->addListener (this);
        addAndMakeVisible (btn);
    }

    setSize (320, 480);
}

SetlistPanel::~SetlistPanel() {}

void SetlistPanel::refresh()
{
    listBox.updateContent();
    listBox.selectRow (manager.getCurrentIndex());
    repaint();
}

//==============================================================================
void SetlistPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a2e));
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
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
    listBox.setBounds (area.removeFromBottom (area.getHeight() - 80));

    area.removeFromTop (4);
    auto navRow = area.removeFromTop (30);
    prevButton.setBounds (navRow.removeFromLeft (80));
    navRow.removeFromLeft (4);
    nextButton.setBounds (navRow.removeFromLeft (80));

    area.removeFromTop (4);
    auto fileRow = area;
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
    if (selected || isCurrent)
        g.fillAll (juce::Colour (isCurrent ? 0xff4040a0 : 0xff2a2a5a));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font(juce::FontOptions().withHeight(13.0f)));

    juce::String text = juce::String (row + 1) + ".  " + manager.getSongName (row);
    g.drawText (text, 8, 0, width - 8, height, juce::Justification::centredLeft);
}

void SetlistPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    manager.setCurrentIndex (row);
    if (onSongSelected) onSongSelected (row);
    listBox.repaintRow (row);
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
