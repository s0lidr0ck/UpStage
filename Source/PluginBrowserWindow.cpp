#include "PluginBrowserWindow.h"

PluginBrowserWindow::PluginBrowserWindow (juce::KnownPluginList& list)
    : knownPluginList (list)
{
    flatButton.onClick   = [this] { setSortMode (Flat); };
    folderButton.onClick = [this] { setSortMode (Folder); };
    vendorButton.onClick = [this] { setSortMode (Vendor); };
    typeButton.onClick   = [this] { setSortMode (Type); };

    for (auto* b : { &flatButton, &folderButton, &vendorButton, &typeButton })
        addAndMakeVisible (b);

    searchBox.setTextToShowWhenEmpty ("Search...", juce::Colour (0xff666666));
    searchBox.onTextChange = [this] {
        searchFilter = searchBox.getText();
        rebuildTree();
    };
    addAndMakeVisible (searchBox);

    clearButton.onClick = [this] {
        searchBox.clear();
        searchFilter.clear();
        rebuildTree();
    };
    addAndMakeVisible (clearButton);

    treeView.setColour (juce::TreeView::backgroundColourId, juce::Colour (0xff1e1e1e));
    treeView.setDefaultOpenness (true);
    addAndMakeVisible (treeView);

    updateButtonStates();
    rebuildTree();

    setSize (350, 550);
}

PluginBrowserWindow::~PluginBrowserWindow()
{
    treeView.setRootItem (nullptr);
}

void PluginBrowserWindow::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}

void PluginBrowserWindow::resized()
{
    auto area = getLocalBounds().reduced (4);

    // Sort buttons row
    auto sortRow = area.removeFromTop (28);
    auto sortLabel = sortRow.removeFromLeft (50);
    // no label, just buttons
    int bw = (sortRow.getWidth() - 12) / 4;
    flatButton.setBounds (sortRow.removeFromLeft (bw));
    sortRow.removeFromLeft (4);
    folderButton.setBounds (sortRow.removeFromLeft (bw));
    sortRow.removeFromLeft (4);
    vendorButton.setBounds (sortRow.removeFromLeft (bw));
    sortRow.removeFromLeft (4);
    typeButton.setBounds (sortRow);

    area.removeFromTop (4);

    // Search row
    auto searchRow = area.removeFromTop (26);
    clearButton.setBounds (searchRow.removeFromRight (26));
    searchRow.removeFromRight (2);
    searchBox.setBounds (searchRow);

    area.removeFromTop (4);

    // Tree fills the rest
    treeView.setBounds (area);
}

void PluginBrowserWindow::closeDialog()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

void PluginBrowserWindow::setSortMode (SortMode mode)
{
    sortMode = mode;
    updateButtonStates();
    rebuildTree();
}

void PluginBrowserWindow::updateButtonStates()
{
    auto active   = juce::Colour (0xff3a5a7a);
    auto inactive = juce::Colour (0xff3a3a3a);

    flatButton.setColour   (juce::TextButton::buttonColourId, sortMode == Flat   ? active : inactive);
    folderButton.setColour (juce::TextButton::buttonColourId, sortMode == Folder ? active : inactive);
    vendorButton.setColour (juce::TextButton::buttonColourId, sortMode == Vendor ? active : inactive);
    typeButton.setColour   (juce::TextButton::buttonColourId, sortMode == Type   ? active : inactive);
}

void PluginBrowserWindow::rebuildTree()
{
    treeView.setRootItem (nullptr);
    rootItem = std::make_unique<RootItem>();

    auto allPlugins = knownPluginList.getTypes();

    // Filter
    juce::Array<juce::PluginDescription> filtered;
    for (const auto& p : allPlugins)
    {
        if (searchFilter.isEmpty())
        {
            filtered.add (p);
        }
        else
        {
            auto lf = searchFilter.toLowerCase();
            if (p.name.toLowerCase().contains (lf)
                || p.manufacturerName.toLowerCase().contains (lf)
                || p.category.toLowerCase().contains (lf))
            {
                filtered.add (p);
            }
        }
    }

    // Sort by name as baseline
    std::sort (filtered.begin(), filtered.end(),
        [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
        { return a.name.compareIgnoreCase (b.name) < 0; });

    if (sortMode == Flat)
    {
        for (const auto& p : filtered)
            rootItem->addSubItem (new PluginLeafItem (p, *this));
    }
    else
    {
        // Group plugins
        std::map<juce::String, juce::Array<juce::PluginDescription>> groups;

        for (const auto& p : filtered)
        {
            juce::String key;
            switch (sortMode)
            {
                case Vendor: key = p.manufacturerName.isEmpty() ? "Unknown" : p.manufacturerName; break;
                case Folder: key = extractFolder (p); break;
                case Type:   key = extractType (p); break;
                default:     key = "Other"; break;
            }
            groups[key].add (p);
        }

        for (auto& [groupName, plugins] : groups)
        {
            auto* group = new PluginGroupItem (groupName);

            for (const auto& p : plugins)
                group->addSubItem (new PluginLeafItem (p, *this));

            rootItem->addSubItem (group);
        }
    }

    treeView.setRootItem (rootItem.get());
    treeView.setRootItemVisible (false);
}

juce::String PluginBrowserWindow::extractFolder (const juce::PluginDescription& desc)
{
    juce::File pluginFile (desc.fileOrIdentifier);
    auto parent = pluginFile.getParentDirectory();
    auto parentPath = parent.getFullPathName().toLowerCase();

    if (parentPath.endsWith ("vst3") || parentPath.endsWith ("common files"))
        return "Root";

    return parent.getFileName();
}

juce::String PluginBrowserWindow::extractType (const juce::PluginDescription& desc)
{
    auto cat = desc.category;
    if (cat.isEmpty())
        return "Uncategorized";

    if (cat.contains ("|"))
        return cat.fromLastOccurrenceOf ("|", false, false).trim();

    return cat;
}
