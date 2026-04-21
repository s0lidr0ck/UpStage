#pragma once
#include <JuceHeader.h>

class PluginManagerWindow : public juce::Component,
                            public juce::TableListBoxModel,
                            public juce::Timer
{
public:
    PluginManagerWindow (juce::KnownPluginList& list,
                         juce::AudioPluginFormatManager& fm);
    ~PluginManagerWindow() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void(bool clearCache)> onScanPlugins;

    void refreshTable();

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics& g, int rowNumber, int width, int height, bool selected) override;
    void paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool selected) override;
    void sortOrderChanged (int newSortColumnId, bool isForwards) override;

private:
    juce::KnownPluginList& knownPluginList;
    juce::AudioPluginFormatManager& formatManager;

    juce::Array<juce::PluginDescription> filteredPlugins;
    juce::String searchFilter;
    int sortColumnId = 1;
    bool sortForwards = true;

    juce::TableListBox table;
    juce::TextEditor   searchBox;
    juce::TextButton   scanNewButton       { "Scan New" };
    juce::TextButton   rescanAllButton     { "Re-scan All" };
    juce::TextButton   clearBlacklistButton { "Clear Blacklist" };
    juce::TextButton   removePluginButton  { "Remove Selected" };
    juce::Label        countLabel;

    juce::StringArray  blacklist;

    void timerCallback() override;
    void applyFilterAndSort();
    void loadBlacklist();
    void saveBlacklist();
    int lastKnownCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManagerWindow)
};
