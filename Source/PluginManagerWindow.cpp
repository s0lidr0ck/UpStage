#include "PluginManagerWindow.h"

enum ColumnIds
{
    colName     = 1,
    colVendor   = 2,
    colCategory = 3,
    colFormat   = 4,
    colVersion  = 5
};

//==============================================================================
PluginManagerWindow::PluginManagerWindow (juce::KnownPluginList& list,
                                          juce::AudioPluginFormatManager& fm)
    : knownPluginList (list), formatManager (fm)
{
    auto& header = table.getHeader();
    header.addColumn ("Name",     colName,     220, 100, 400, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Vendor",   colVendor,   150, 80,  300, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Category", colCategory, 100, 60,  200, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Format",   colFormat,    60, 40,  100, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Version",  colVersion,   70, 50,  120, juce::TableHeaderComponent::defaultFlags);

    header.setSortColumnId (colName, true);
    table.setModel (this);
    table.setMultipleSelectionEnabled (false);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1e1e1e));
    addAndMakeVisible (table);

    searchBox.setTextToShowWhenEmpty ("Search plugins...", juce::Colour (0xff666666));
    searchBox.onTextChange = [this] {
        searchFilter = searchBox.getText();
        applyFilterAndSort();
    };
    addAndMakeVisible (searchBox);

    scanNewButton.onClick = [this] {
        if (onScanPlugins) onScanPlugins (false);
    };
    addAndMakeVisible (scanNewButton);

    rescanAllButton.onClick = [this] {
        if (onScanPlugins) onScanPlugins (true);
    };
    addAndMakeVisible (rescanAllButton);

    clearBlacklistButton.onClick = [this] {
        auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("UpStage").getChildFile ("PluginBlacklist.txt");
        file.deleteFile();
        loadBlacklist();
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon, "Blacklist Cleared",
            "Plugin blacklist cleared. Use Re-scan All to scan previously blocked plugins.", "OK");
    };
    addAndMakeVisible (clearBlacklistButton);

    removePluginButton.onClick = [this] {
        int row = table.getSelectedRow();
        if (row >= 0 && row < filteredPlugins.size())
        {
            auto desc = filteredPlugins[row];
            knownPluginList.removeType (desc);
            applyFilterAndSort();
        }
    };
    addAndMakeVisible (removePluginButton);

    countLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    countLabel.setColour (juce::Label::textColourId, juce::Colour (0xff999999));
    addAndMakeVisible (countLabel);

    loadBlacklist();
    applyFilterAndSort();
    lastKnownCount = knownPluginList.getNumTypes();
    startTimerHz (2);
    setSize (800, 600);
}

PluginManagerWindow::~PluginManagerWindow()
{
    stopTimer();
    table.setModel (nullptr);
}

void PluginManagerWindow::timerCallback()
{
    int current = knownPluginList.getNumTypes();
    if (current != lastKnownCount)
    {
        lastKnownCount = current;
        applyFilterAndSort();
    }
}

//==============================================================================
void PluginManagerWindow::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}

void PluginManagerWindow::resized()
{
    auto area = getLocalBounds().reduced (8);

    // Top row: search + buttons
    auto topRow = area.removeFromTop (30);
    searchBox.setBounds (topRow.removeFromLeft (250));
    topRow.removeFromLeft (10);
    scanNewButton.setBounds (topRow.removeFromLeft (90));
    topRow.removeFromLeft (4);
    rescanAllButton.setBounds (topRow.removeFromLeft (90));
    topRow.removeFromLeft (4);
    clearBlacklistButton.setBounds (topRow.removeFromLeft (110));
    topRow.removeFromLeft (4);
    removePluginButton.setBounds (topRow.removeFromLeft (110));

    // Bottom row: count
    auto bottomRow = area.removeFromBottom (22);
    countLabel.setBounds (bottomRow);

    area.removeFromTop (6);
    area.removeFromBottom (4);

    // Table fills the rest
    table.setBounds (area);
}

//==============================================================================
void PluginManagerWindow::refreshTable()
{
    applyFilterAndSort();
}

void PluginManagerWindow::applyFilterAndSort()
{
    auto allPlugins = knownPluginList.getTypes();
    filteredPlugins.clear();

    for (const auto& p : allPlugins)
    {
        if (searchFilter.isEmpty())
        {
            filteredPlugins.add (p);
        }
        else
        {
            auto lowerFilter = searchFilter.toLowerCase();
            if (p.name.toLowerCase().contains (lowerFilter)
                || p.manufacturerName.toLowerCase().contains (lowerFilter)
                || p.category.toLowerCase().contains (lowerFilter))
            {
                filteredPlugins.add (p);
            }
        }
    }

    // Sort
    std::sort (filteredPlugins.begin(), filteredPlugins.end(),
        [this] (const juce::PluginDescription& a, const juce::PluginDescription& b)
        {
            int result = 0;
            switch (sortColumnId)
            {
                case colName:     result = a.name.compareIgnoreCase (b.name); break;
                case colVendor:   result = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
                                  if (result == 0) result = a.name.compareIgnoreCase (b.name);
                                  break;
                case colCategory: result = a.category.compareIgnoreCase (b.category);
                                  if (result == 0) result = a.name.compareIgnoreCase (b.name);
                                  break;
                case colFormat:   result = a.pluginFormatName.compareIgnoreCase (b.pluginFormatName); break;
                case colVersion:  result = a.version.compareIgnoreCase (b.version); break;
                default: break;
            }
            return sortForwards ? result < 0 : result > 0;
        });

    int total = knownPluginList.getNumTypes();
    int showing = filteredPlugins.size();

    juce::String countText = juce::String (showing) + " plugins";
    if (showing != total)
        countText += " (of " + juce::String (total) + " total)";
    if (! blacklist.isEmpty())
        countText += "   |   " + juce::String (blacklist.size()) + " blacklisted";
    countLabel.setText (countText, juce::dontSendNotification);

    table.updateContent();
    table.repaint();
}

//==============================================================================
int PluginManagerWindow::getNumRows()
{
    return filteredPlugins.size();
}

void PluginManagerWindow::paintRowBackground (juce::Graphics& g, int rowNumber,
                                               int width, int height, bool selected)
{
    if (selected)
        g.fillAll (juce::Colour (0xff3a5a7a));
    else if (rowNumber % 2 == 0)
        g.fillAll (juce::Colour (0xff242424));
    else
        g.fillAll (juce::Colour (0xff2c2c2c));
}

void PluginManagerWindow::paintCell (juce::Graphics& g, int rowNumber, int columnId,
                                      int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (rowNumber, filteredPlugins.size()))
        return;

    const auto& plugin = filteredPlugins.getReference (rowNumber);

    g.setColour (selected ? juce::Colours::white : juce::Colour (0xffcccccc));
    g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));

    juce::String text;
    switch (columnId)
    {
        case colName:     text = plugin.name; break;
        case colVendor:   text = plugin.manufacturerName; break;
        case colCategory: text = plugin.category; break;
        case colFormat:   text = plugin.pluginFormatName; break;
        case colVersion:  text = plugin.version; break;
        default: break;
    }

    g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void PluginManagerWindow::sortOrderChanged (int newSortColumnId, bool isForwards)
{
    sortColumnId = newSortColumnId;
    sortForwards = isForwards;
    applyFilterAndSort();
}

//==============================================================================
void PluginManagerWindow::loadBlacklist()
{
    blacklist.clear();
    auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("UpStage").getChildFile ("PluginBlacklist.txt");

    if (file.existsAsFile())
    {
        auto lines = juce::StringArray::fromLines (file.loadFileAsString());
        for (const auto& line : lines)
            if (line.trim().isNotEmpty())
                blacklist.add (line.trim());
    }
}

void PluginManagerWindow::saveBlacklist()
{
    auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("UpStage").getChildFile ("PluginBlacklist.txt");

    juce::String content;
    for (const auto& entry : blacklist)
        content += entry + "\n";

    file.replaceWithText (content);
}
