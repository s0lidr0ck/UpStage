#pragma once

#include <JuceHeader.h>
#include "AmpLibrary.h"

/**
 * AmpImportDialog - the card that turns a downloaded file into a library entry.
 *
 * Auto-detects kind by extension: .nam -> rig (with cab-baked-in toggle and
 * optional cab IR pairing), anything audio -> cabinet IR. Picture comes from a
 * click (file chooser) or a drop onto the picture well.
 */
class AmpImportDialog : public juce::Component
{
public:
    /** Shows the dialog for a source file. onDone runs after the dialog closes
        (whether or not an import happened) - used to chain multi-file drops. */
    static void show (const juce::File& sourceFile, std::function<void()> onDone = nullptr)
    {
        if (! sourceFile.existsAsFile())
        {
            if (onDone) onDone();
            return;
        }

        const bool isRig = sourceFile.hasFileExtension ("nam");

        // Fast-fail A1 models before showing any UI.
        if (isRig)
        {
            juce::String err;
            if (! AmpLibrary::isA2NamFile (sourceFile, err))
            {
                auto done = onDone;
                juce::AlertWindow::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Can't import " + sourceFile.getFileName())
                        .withMessage (err)
                        .withButton ("OK"),
                    [done] (int) { if (done) done(); });
                return;
            }
        }

        auto* content = new AmpImportDialog (sourceFile, isRig, std::move (onDone));

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (content);
        opts.dialogTitle = juce::String ("Import ") + (isRig ? "Amp Rig" : "Cabinet IR");
        opts.dialogBackgroundColour = juce::Colour (0xff2a2825);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = false;
        opts.resizable = false;
        content->window = opts.launchAsync();
    }

    ~AmpImportDialog() override
    {
        if (onDone)
            onDone();
    }

private:
    AmpImportDialog (const juce::File& src, bool rig, std::function<void()> done)
        : sourceFile (src), isRig (rig), onDone (std::move (done))
    {
        setSize (420, rig ? 342 : 262);

        auto label = [this] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            l.setColour (juce::Label::textColourId, juce::Colour (0xffb5b1a6));
            addAndMakeVisible (l);
        };

        label (fileLabel, src.getFileName());
        fileLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8d8878));

        label (nameLabel, "Name");
        nameEditor.setText (src.getFileNameWithoutExtension());
        addAndMakeVisible (nameEditor);

        label (tagsLabel, "Tags (comma-separated)");
        tagsEditor.setTextToShowWhenEmpty ("clean, crunch, fender...", juce::Colour (0xff777364));
        addAndMakeVisible (tagsEditor);

        addAndMakeVisible (pictureWell);
        pictureWell.onPictureChanged = [this] { repaint(); };

        if (isRig)
        {
            cabBakedToggle.setButtonText ("Capture includes cabinet (full rig)");
            addAndMakeVisible (cabBakedToggle);

            label (pairLabel, "Pair cab IR");
            rebuildCabCombo();
            pairCombo.onChange = [this]
            {
                if (pairCombo.getSelectedId() == 2)   // Browse...
                {
                    auto chooser = std::make_shared<juce::FileChooser> (
                        "Choose a cabinet IR (WAV)", juce::File{}, "*.wav");
                    juce::Component::SafePointer<AmpImportDialog> safe (this);
                    chooser->launchAsync (juce::FileBrowserComponent::openMode |
                                          juce::FileBrowserComponent::canSelectFiles,
                        [safe, chooser] (const juce::FileChooser& fc)
                        {
                            if (safe == nullptr)
                                return;
                            auto f = fc.getResult();
                            if (f.existsAsFile())
                            {
                                safe->browsedIr = f;
                                safe->pairCombo.changeItemText (2, "File: " + f.getFileName());
                                safe->pairCombo.setSelectedId (2, juce::dontSendNotification);
                            }
                            else
                                safe->pairCombo.setSelectedId (1, juce::dontSendNotification);
                        });
                }
                else
                    browsedIr = juce::File();
            };
            addAndMakeVisible (pairCombo);
        }

        importButton.onClick = [this] { doImport(); };
        addAndMakeVisible (importButton);
        cancelButton.onClick = [this] { closeWindow(); };
        addAndMakeVisible (cancelButton);
    }

    //==========================================================================
    class PictureWell : public juce::Component,
                        public juce::FileDragAndDropTarget
    {
    public:
        PictureWell() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff16150f));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.2f);

            if (picture.isValid())
                g.drawImage (picture, b.reduced (3),
                             juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
            else
            {
                g.setColour (juce::Colour (0xff5a564c));
                g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
                g.drawFittedText ("Click or drop a picture\n(optional)",
                                  getLocalBounds().reduced (6), juce::Justification::centred, 2);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasClicked())
                return;
            auto chooser = std::make_shared<juce::FileChooser> (
                "Choose a picture", juce::File{}, "*.png;*.jpg;*.jpeg");
            juce::Component::SafePointer<PictureWell> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                [safe, chooser] (const juce::FileChooser& fc)
                {
                    if (safe != nullptr)
                        safe->setPicture (fc.getResult());
                });
        }

        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            for (const auto& f : files)
                if (f.endsWithIgnoreCase (".png") || f.endsWithIgnoreCase (".jpg")
                    || f.endsWithIgnoreCase (".jpeg"))
                    return true;
            return false;
        }

        void filesDropped (const juce::StringArray& files, int, int) override
        {
            for (const auto& f : files)
            {
                juce::File file (f);
                if (file.hasFileExtension ("png;jpg;jpeg"))
                {
                    setPicture (file);
                    break;
                }
            }
        }

        void setPicture (const juce::File& f)
        {
            if (! f.existsAsFile())
                return;
            auto img = juce::ImageCache::getFromFile (f);
            if (img.isValid())
            {
                pictureFile = f;
                picture = img;
                repaint();
                if (onPictureChanged) onPictureChanged();
            }
        }

        juce::File pictureFile;
        std::function<void()> onPictureChanged;

    private:
        juce::Image picture;
    };

    //==========================================================================
    void rebuildCabCombo()
    {
        pairCombo.clear (juce::dontSendNotification);
        pairCombo.addItem ("None", 1);
        pairCombo.addItem ("Browse for WAV...", 2);
        int id = 10;
        for (const auto* e : AmpLibrary::instance().getEntries())
            if (e->kind == AmpLibraryEntry::Kind::cab)
                pairCombo.addItem (e->name, id++);
        pairCombo.setSelectedId (1, juce::dontSendNotification);
    }

    juce::String selectedExistingCabId() const
    {
        const int sel = pairCombo.getSelectedId();
        if (sel < 10)
            return {};
        int id = 10;
        for (const auto* e : AmpLibrary::instance().getEntries())
        {
            if (e->kind == AmpLibraryEntry::Kind::cab)
            {
                if (id == sel)
                    return e->id;
                ++id;
            }
        }
        return {};
    }

    void doImport()
    {
        auto& lib = AmpLibrary::instance();
        juce::String err;

        auto tags = juce::StringArray::fromTokens (tagsEditor.getText(), ",", "");
        tags.trim();
        tags.removeEmptyStrings();

        juce::String newId;
        if (isRig)
        {
            newId = lib.importNamFile (sourceFile, nameEditor.getText().trim(),
                                       pictureWell.pictureFile, tags,
                                       cabBakedToggle.getToggleState(), browsedIr, err);

            // Pairing with an already-imported cab entry.
            if (newId.isNotEmpty() && browsedIr == juce::File())
            {
                if (auto cabId = selectedExistingCabId(); cabId.isNotEmpty())
                {
                    if (const auto* e = lib.findById (newId))
                    {
                        AmpLibraryEntry updated = *e;
                        updated.pairedCabId = cabId;
                        lib.updateEntry (updated);
                    }
                }
            }
        }
        else
        {
            newId = lib.importIrFile (sourceFile, nameEditor.getText().trim(),
                                      pictureWell.pictureFile, tags, err);
        }

        if (newId.isEmpty())
        {
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle ("Import failed")
                    .withMessage (err)
                    .withButton ("OK"),
                nullptr);
            return;   // keep the dialog open
        }

        closeWindow();
    }

    void closeWindow()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
        if (window != nullptr)
            window->setVisible (false);
        // DialogWindow (launchAsync) deletes itself and its owned content.
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 10);

        fileLabel.setBounds (area.removeFromTop (16));
        area.removeFromTop (6);

        auto row = area.removeFromTop (96);
        pictureWell.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (12);

        auto fields = row;
        nameLabel.setBounds (fields.removeFromTop (16));
        nameEditor.setBounds (fields.removeFromTop (24));
        fields.removeFromTop (8);
        tagsLabel.setBounds (fields.removeFromTop (16));
        tagsEditor.setBounds (fields.removeFromTop (24));

        area.removeFromTop (12);
        if (isRig)
        {
            cabBakedToggle.setBounds (area.removeFromTop (24));
            area.removeFromTop (8);
            pairLabel.setBounds (area.removeFromTop (16));
            pairCombo.setBounds (area.removeFromTop (24));
            area.removeFromTop (8);
        }

        auto buttons = area.removeFromBottom (30);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        importButton.setBounds (buttons.removeFromRight (90));
    }

    //==========================================================================
    juce::File sourceFile;
    bool isRig;
    std::function<void()> onDone;
    juce::Component::SafePointer<juce::DialogWindow> window;

    juce::Label fileLabel, nameLabel, tagsLabel, pairLabel;
    juce::TextEditor nameEditor, tagsEditor;
    PictureWell pictureWell;
    juce::ToggleButton cabBakedToggle;
    juce::ComboBox pairCombo;
    juce::File browsedIr;
    juce::TextButton importButton { "IMPORT" }, cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpImportDialog)
};
