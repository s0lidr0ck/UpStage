#pragma once

#include <JuceHeader.h>
#include "AmpLibrary.h"
#include "HardwareModuleWindow.h"

/**
 * AmpLibraryBrowserWindow - the amp locker.
 *
 * A wall of rig cards (picture-forward) with a CABINETS section below, search
 * and tag filtering, and an IMPORT button. Two modes:
 *   - manage: opened from the AMPS toolbar button; right-click cards to
 *     rename / tag / set picture / pair cabs / delete.
 *   - pick: opened by an amp editor's BROWSE buttons; clicking a card fires
 *     the pick callback and hides the window.
 */
class AmpLibraryBrowserContent : public juce::Component
{
public:
    AmpLibraryBrowserContent()
    {
        modeLabel.setComponentID ("strip_label");
        modeLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (modeLabel);

        searchBox.setTextToShowWhenEmpty ("Search...", juce::Colour (0xff777364));
        searchBox.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (searchBox);

        tagFilter.onChange = [this] { rebuild(); };
        addAndMakeVisible (tagFilter);

        importButton.onClick = [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> (
                "Import NAM captures and/or cabinet IRs", juce::File{}, "*.nam;*.wav");
            juce::Component::SafePointer<AmpLibraryBrowserContent> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectMultipleItems,
                [safe, chooser] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr || ! safe->onImportRequested)
                        return;
                    juce::Array<juce::File> files;
                    for (const auto& f : fc.getResults())
                        if (f.existsAsFile())
                            files.add (f);
                    if (! files.isEmpty())
                        safe->onImportRequested (files);
                });
        };
        addAndMakeVisible (importButton);

        cardHolder = std::make_unique<juce::Component>();
        viewport.setViewedComponent (cardHolder.get(), false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        AmpLibrary::instance().onLibraryChanged = [safe = this] { safe->refreshTagsAndRebuild(); };
        refreshTagsAndRebuild();
        enterManageMode();
    }

    ~AmpLibraryBrowserContent() override
    {
        AmpLibrary::instance().onLibraryChanged = nullptr;
    }

    //==========================================================================
    void enterManageMode()
    {
        pickMode = false;
        pickCallback = nullptr;
        modeLabel.setText ("AMP LOCKER", juce::dontSendNotification);
        rebuild();
    }

    void enterPickMode (AmpLibraryEntry::Kind kind, std::function<void (juce::String)> cb)
    {
        pickMode = true;
        pickKind = kind;
        pickCallback = std::move (cb);
        modeLabel.setText (kind == AmpLibraryEntry::Kind::rig ? "PICK A RIG" : "PICK A CAB",
                           juce::dontSendNotification);
        rebuild();
    }

    std::function<void (const juce::Array<juce::File>&)> onImportRequested;

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        juce::ColourGradient grad (juce::Colour (0xff32302b), 0, 0,
                                   juce::Colour (0xff1f1e1a), 0, (float) getHeight(), false);
        g.setGradientFill (grad);
        g.fillAll();
        g.setColour (juce::Colours::white.withAlpha (0.02f));
        for (int y = 0; y < getHeight(); y += 3)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 10);
        auto top = area.removeFromTop (26);

        modeLabel.setBounds (top.removeFromLeft (150));
        top.removeFromLeft (8);
        importButton.setBounds (top.removeFromRight (86));
        top.removeFromRight (8);
        tagFilter.setBounds (top.removeFromRight (140));
        top.removeFromRight (8);
        searchBox.setBounds (top);

        area.removeFromTop (8);
        viewport.setBounds (area);
        layoutCards();
    }

private:
    //==========================================================================
    class Card : public juce::Component
    {
    public:
        Card (AmpLibraryBrowserContent& o, const AmpLibraryEntry& e)
            : owner (o), entryId (e.id), kind (e.kind), name (e.name),
              cabBakedIn (e.cabBakedIn), tags (e.tags)
        {
            if (e.pictureFile.existsAsFile())
                picture = juce::ImageCache::getFromFile (e.pictureFile);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (2);

            // seated card with drop shadow
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (b.translated (1.5f, 2.0f), 5.0f);
            juce::ColourGradient grad (juce::Colour (0xff3d3a34), b.getX(), b.getY(),
                                       juce::Colour (0xff2b2925), b.getX(), b.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (hovered ? juce::Colour (0xffd8a740) : juce::Colour (0xff1a1916));
            g.drawRoundedRectangle (b, 5.0f, hovered ? 1.6f : 1.0f);

            // picture area
            auto pic = getLocalBounds().reduced (6).removeFromTop (getHeight() - 40);
            g.setColour (juce::Colour (0xff16150f));
            g.fillRoundedRectangle (pic.toFloat(), 3.0f);
            if (picture.isValid())
                g.drawImage (picture, pic.toFloat().reduced (2),
                             juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
            else
            {
                g.setColour (juce::Colour (0xff45423a));
                g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
                g.drawText (kind == AmpLibraryEntry::Kind::rig ? "AMP" : "CAB",
                            pic, juce::Justification::centred);
            }

            // badges
            int bx = pic.getX() + 3;
            auto badge = [&g, &bx, pic] (const juce::String& text, juce::Colour col)
            {
                auto f = juce::Font (juce::FontOptions().withHeight (9.0f).withStyle ("Bold"));
                g.setFont (f);
                const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, text)) + 8;
                juce::Rectangle<int> r (bx, pic.getY() + 3, w, 12);
                g.setColour (juce::Colours::black.withAlpha (0.65f));
                g.fillRoundedRectangle (r.toFloat(), 2.5f);
                g.setColour (col);
                g.drawText (text, r, juce::Justification::centred);
                bx += w + 3;
            };
            if (kind == AmpLibraryEntry::Kind::rig && cabBakedIn)
                badge ("FULL RIG", juce::Colour (0xff7ec97e));
            for (int i = 0; i < juce::jmin (2, tags.size()); ++i)
                badge (tags[i].toUpperCase(), juce::Colour (0xffb8b4a5));

            // name strip
            auto nameArea = getLocalBounds().reduced (6).removeFromBottom (28);
            g.setColour (juce::Colour (0xff191813));
            g.fillRoundedRectangle (nameArea.toFloat(), 3.0f);
            g.setColour (juce::Colour (0xffe8e2ce));
            g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            g.drawFittedText (name, nameArea.reduced (4, 2), juce::Justification::centred, 2);
        }

        void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { hovered = false; repaint(); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isRightButtonDown())
            {
                if (! owner.pickMode)
                    owner.showCardMenu (entryId);
                return;
            }
            if (e.mouseWasClicked())
                owner.cardClicked (entryId);
        }

    private:
        AmpLibraryBrowserContent& owner;
        juce::String entryId;
        AmpLibraryEntry::Kind kind;
        juce::String name;
        bool cabBakedIn;
        juce::StringArray tags;
        juce::Image picture;
        bool hovered = false;
    };

    class SectionLabel : public juce::Component
    {
    public:
        explicit SectionLabel (const juce::String& t) : text (t) {}
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff8d8878));
            g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f).withStyle ("Bold")));
            g.drawText (text, getLocalBounds().withTrimmedLeft (4), juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xff45423a));
            const float y = getHeight() * 0.5f;
            g.drawLine (90.0f, y, (float) getWidth() - 4, y, 1.0f);
        }
    private:
        juce::String text;
    };

    //==========================================================================
    bool matchesFilter (const AmpLibraryEntry& e) const
    {
        if (pickMode && e.kind != pickKind)
            return false;

        const auto search = searchBox.getText().trim();
        if (search.isNotEmpty())
        {
            const auto hay = (e.name + " " + e.creator + " " + e.tags.joinIntoString (" "));
            if (! hay.containsIgnoreCase (search))
                return false;
        }

        if (tagFilter.getSelectedId() > 1)
        {
            const auto tag = tagFilter.getText();
            if (! e.tags.contains (tag, true))
                return false;
        }
        return true;
    }

    void refreshTagsAndRebuild()
    {
        juce::StringArray allTags;
        for (const auto* e : AmpLibrary::instance().getEntries())
            for (const auto& t : e->tags)
                allTags.addIfNotAlreadyThere (t, true);
        allTags.sortNatural();

        const auto current = tagFilter.getText();
        tagFilter.clear (juce::dontSendNotification);
        tagFilter.addItem ("All tags", 1);
        int id = 2;
        for (const auto& t : allTags)
            tagFilter.addItem (t, id++);
        tagFilter.setSelectedId (juce::jmax (1, tagFilter.getSelectedId()), juce::dontSendNotification);
        for (int i = 0; i < tagFilter.getNumItems(); ++i)
            if (tagFilter.getItemText (i) == current)
                tagFilter.setSelectedItemIndex (i, juce::dontSendNotification);

        rebuild();
    }

    void rebuild()
    {
        cards.clear();
        sections.clear();

        auto& lib = AmpLibrary::instance();
        const bool showRigs = ! pickMode || pickKind == AmpLibraryEntry::Kind::rig;
        const bool showCabs = ! pickMode || pickKind == AmpLibraryEntry::Kind::cab;

        if (showRigs)
        {
            auto* sec = sections.add (new SectionLabel ("RIGS"));
            cardHolder->addAndMakeVisible (sec);
            for (const auto* e : lib.getEntries())
                if (e->kind == AmpLibraryEntry::Kind::rig && matchesFilter (*e))
                    cardHolder->addAndMakeVisible (cards.add (new Card (*this, *e)));
        }
        if (showCabs)
        {
            auto* sec = sections.add (new SectionLabel ("CABINETS"));
            cardHolder->addAndMakeVisible (sec);
            for (const auto* e : lib.getEntries())
                if (e->kind == AmpLibraryEntry::Kind::cab && matchesFilter (*e))
                    cardHolder->addAndMakeVisible (cards.add (new Card (*this, *e)));
        }

        layoutCards();
    }

    void layoutCards()
    {
        const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
        const int cardW = 152, cardH = 124, gap = 8;
        const int perRow = juce::jmax (1, (w - gap) / (cardW + gap));

        int y = 4;
        int cardIdx = 0;
        int sectionIdx = 0;

        // Sections were added in creation order: each section label then its cards.
        // Walk children of cardHolder in order.
        for (int childIdx = 0; childIdx < cardHolder->getNumChildComponents();)
        {
            auto* child = cardHolder->getChildComponent (childIdx);
            if (dynamic_cast<SectionLabel*> (child) != nullptr)
            {
                child->setBounds (4, y, w - 8, 20);
                y += 24;
                ++childIdx;

                // lay out the run of cards that follows
                int col = 0;
                int rowY = y;
                int placed = 0;
                while (childIdx < cardHolder->getNumChildComponents()
                       && dynamic_cast<SectionLabel*> (cardHolder->getChildComponent (childIdx)) == nullptr)
                {
                    auto* card = cardHolder->getChildComponent (childIdx);
                    card->setBounds (4 + col * (cardW + gap), rowY, cardW, cardH);
                    ++col;
                    ++placed;
                    if (col >= perRow) { col = 0; rowY += cardH + gap; }
                    ++childIdx;
                }
                y = rowY + (col > 0 || placed == 0 ? cardH + gap : 0);
                if (placed == 0)
                    y = rowY + 8;   // empty section: small gap only
            }
            else
                ++childIdx;
        }

        cardHolder->setSize (w, juce::jmax (y + 8, viewport.getHeight()));
        juce::ignoreUnused (cardIdx, sectionIdx);
    }

    //==========================================================================
    void cardClicked (const juce::String& entryId)
    {
        if (pickMode && pickCallback)
        {
            auto cb = pickCallback;
            cb (entryId);
            if (auto* win = findParentComponentOfClass<juce::DocumentWindow>())
                win->setVisible (false);
            enterManageMode();
            return;
        }
        showCardMenu (entryId);
    }

    void showCardMenu (const juce::String& entryId)
    {
        const auto* e = AmpLibrary::instance().findById (entryId);
        if (e == nullptr)
            return;
        const bool isRig = e->kind == AmpLibraryEntry::Kind::rig;

        juce::PopupMenu menu;
        menu.addItem (1, "Rename...");
        menu.addItem (2, "Edit Tags...");
        menu.addItem (3, "Set Picture...");
        if (isRig)
        {
            menu.addItem (4, juce::String (e->cabBakedIn ? "Unmark" : "Mark") + " as Full Rig (cab baked in)");
            juce::PopupMenu cabMenu;
            cabMenu.addItem (100, "None", true, e->pairedCabId.isEmpty());
            int id = 101;
            for (const auto* cab : AmpLibrary::instance().getEntries())
                if (cab->kind == AmpLibraryEntry::Kind::cab)
                    cabMenu.addItem (id++, cab->name, true, e->pairedCabId == cab->id);
            menu.addSubMenu ("Pair Cab IR", cabMenu);
        }
        menu.addSeparator();
        menu.addItem (5, "Delete...");

        juce::Component::SafePointer<AmpLibraryBrowserContent> safe (this);
        menu.showMenuAsync ({}, [safe, entryId] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            safe->handleCardMenuResult (entryId, result);
        });
    }

    void handleCardMenuResult (const juce::String& entryId, int result)
    {
        auto& lib = AmpLibrary::instance();
        const auto* found = lib.findById (entryId);
        if (found == nullptr)
            return;
        AmpLibraryEntry entry = *found;

        switch (result)
        {
            case 1: // rename
            case 2: // tags
            {
                const bool rename = result == 1;
                auto* w = new juce::AlertWindow (rename ? "Rename" : "Edit Tags",
                                                 rename ? "New name:" : "Comma-separated tags:",
                                                 juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("text", rename ? entry.name : entry.tags.joinIntoString (", "));
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                juce::Component::SafePointer<AmpLibraryBrowserContent> safe (this);
                w->enterModalState (true, juce::ModalCallbackFunction::create (
                    [safe, w, entryId, rename] (int r)
                    {
                        if (safe != nullptr && r == 1)
                        {
                            auto& lib2 = AmpLibrary::instance();
                            if (const auto* cur = lib2.findById (entryId))
                            {
                                AmpLibraryEntry updated = *cur;
                                const auto text = w->getTextEditorContents ("text").trim();
                                if (rename)
                                {
                                    if (text.isNotEmpty())
                                        updated.name = text;
                                }
                                else
                                {
                                    updated.tags = juce::StringArray::fromTokens (text, ",", "");
                                    updated.tags.trim();
                                    updated.tags.removeEmptyStrings();
                                }
                                lib2.updateEntry (updated);
                            }
                        }
                    }), true);
                break;
            }

            case 3: // set picture
            {
                auto chooser = std::make_shared<juce::FileChooser> (
                    "Choose a picture", juce::File{}, "*.png;*.jpg;*.jpeg");
                juce::Component::SafePointer<AmpLibraryBrowserContent> safe (this);
                chooser->launchAsync (juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles,
                    [safe, chooser, entryId] (const juce::FileChooser& fc)
                    {
                        auto pic = fc.getResult();
                        if (safe == nullptr || ! pic.existsAsFile())
                            return;
                        auto& lib2 = AmpLibrary::instance();
                        if (const auto* cur = lib2.findById (entryId))
                        {
                            AmpLibraryEntry updated = *cur;
                            auto dest = updated.folder.getChildFile ("picture" + pic.getFileExtension());
                            if (updated.pictureFile.existsAsFile())
                                updated.pictureFile.deleteFile();
                            if (pic.copyFileTo (dest))
                            {
                                updated.pictureFile = dest;
                                lib2.updateEntry (updated);
                            }
                        }
                    });
                break;
            }

            case 4: // toggle cab baked in
                entry.cabBakedIn = ! entry.cabBakedIn;
                lib.updateEntry (entry);
                break;

            case 5: // delete
            {
                juce::Component::SafePointer<AmpLibraryBrowserContent> safe (this);
                juce::AlertWindow::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Delete \"" + entry.name + "\"?")
                        .withMessage ("This removes the files from your amp library. "
                                      "Projects using it will show a MISSING rig.")
                        .withButton ("Delete")
                        .withButton ("Cancel"),
                    [safe, entryId] (int r)
                    {
                        if (safe != nullptr && r == 1)
                            AmpLibrary::instance().deleteEntry (entryId);
                    });
                break;
            }

            default: // pair cab submenu
                if (result == 100)
                {
                    entry.pairedCabId.clear();
                    lib.updateEntry (entry);
                }
                else if (result > 100)
                {
                    int id = 101;
                    for (const auto* cab : lib.getEntries())
                    {
                        if (cab->kind == AmpLibraryEntry::Kind::cab)
                        {
                            if (id == result)
                            {
                                entry.pairedCabId = cab->id;
                                lib.updateEntry (entry);
                                break;
                            }
                            ++id;
                        }
                    }
                }
                break;
        }
    }

    //==========================================================================
    juce::Label modeLabel;
    juce::TextEditor searchBox;
    juce::ComboBox tagFilter;
    juce::TextButton importButton { "IMPORT..." };
    juce::Viewport viewport;
    std::unique_ptr<juce::Component> cardHolder;
    juce::OwnedArray<Card> cards;
    juce::OwnedArray<SectionLabel> sections;

    bool pickMode = false;
    AmpLibraryEntry::Kind pickKind = AmpLibraryEntry::Kind::rig;
    std::function<void (juce::String)> pickCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpLibraryBrowserContent)
};

//==============================================================================
class AmpLibraryBrowserWindow : public HardwareModuleWindow
{
public:
    AmpLibraryBrowserWindow()
        : HardwareModuleWindow ("Amp Locker", new AmpLibraryBrowserContent(), 760, 540)
    {
    }

    AmpLibraryBrowserContent& getContent()
    {
        return *static_cast<AmpLibraryBrowserContent*> (getContentComponent());
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpLibraryBrowserWindow)
};
