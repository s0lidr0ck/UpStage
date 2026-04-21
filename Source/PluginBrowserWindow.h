#pragma once
#include <JuceHeader.h>

class PluginBrowserWindow : public juce::Component
{
public:
    enum SortMode { Flat, Folder, Vendor, Type };

    PluginBrowserWindow (juce::KnownPluginList& list);
    ~PluginBrowserWindow() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void (const juce::PluginDescription&)> onPluginSelected;

    void closeDialog();

private:
    class PluginLeafItem : public juce::TreeViewItem
    {
    public:
        PluginLeafItem (const juce::PluginDescription& d, PluginBrowserWindow& o)
            : desc (d), owner (o) {}

        bool mightContainSubItems() override { return false; }
        int getItemHeight() const override { return 22; }
        juce::String getUniqueName() const override { return desc.createIdentifierString(); }
        bool canBeSelected() const override { return true; }

        void paintItem (juce::Graphics& g, int width, int height) override
        {
            if (isSelected())
                g.fillAll (juce::Colour (0xff3a5a7a));

            g.setColour (isSelected() ? juce::Colours::white : juce::Colour (0xffcccccc));
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
            g.drawText (desc.name, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }

        void itemClicked (const juce::MouseEvent&) override
        {
            if (owner.onPluginSelected)
                owner.onPluginSelected (desc);
            owner.closeDialog();
        }

        juce::PluginDescription desc;
        PluginBrowserWindow& owner;
    };

    class PluginGroupItem : public juce::TreeViewItem
    {
    public:
        PluginGroupItem (const juce::String& name) : groupName (name) {}

        bool mightContainSubItems() override { return true; }
        int getItemHeight() const override { return 24; }
        juce::String getUniqueName() const override { return groupName; }
        bool canBeSelected() const override { return false; }

        void paintItem (juce::Graphics& g, int width, int height) override
        {
            g.setColour (juce::Colour (0xffaaaaaa));
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f).withStyle ("Bold")));
            g.drawText (groupName, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }

        juce::String groupName;
    };

    class RootItem : public juce::TreeViewItem
    {
    public:
        bool mightContainSubItems() override { return true; }
        juce::String getUniqueName() const override { return "root"; }
    };

    juce::KnownPluginList& knownPluginList;
    SortMode sortMode = Vendor;
    juce::String searchFilter;

    juce::TextButton flatButton    { "Flat" };
    juce::TextButton folderButton  { "Folder" };
    juce::TextButton vendorButton  { "Vendor" };
    juce::TextButton typeButton    { "Type" };
    juce::TextEditor searchBox;
    juce::TextButton clearButton   { "X" };
    juce::TreeView   treeView;
    std::unique_ptr<RootItem> rootItem;

    void setSortMode (SortMode mode);
    void rebuildTree();
    void updateButtonStates();

    static juce::String extractFolder (const juce::PluginDescription& desc);
    static juce::String extractType (const juce::PluginDescription& desc);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowserWindow)
};
