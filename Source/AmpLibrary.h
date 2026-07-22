#pragma once

#include <JuceHeader.h>

/**
 * One entry in the user's amp library: either a NAM rig capture (.nam plus
 * optional picture / paired cab) or a standalone cabinet IR (.wav).
 * Entries live in Documents/UpStage/Amp Library/<id>/ with an entry.json
 * sidecar; all file references inside the sidecar are relative to that folder.
 */
struct AmpLibraryEntry
{
    enum class Kind { rig, cab };

    /** NAM capture taxonomy. Heads, full rigs, and pedals are .nam captures
        (Kind::rig); cabs and spaces (room/reverb IRs) are wavs (Kind::cab). */
    enum class Category { head, fullRig, pedal, cab, space };

    juce::String id;                 // Uuid string, stable forever
    Kind         kind = Kind::rig;
    Category     category = Category::head;
    juce::String name, creator, notes;
    juce::String url;                // maker / capture web page (optional)
    juce::StringArray tags;
    juce::File   folder;             // this entry's subfolder
    juce::File   namFile;            // rigs only
    juce::File   irFile;             // cabs only: the IR wav
    juce::File   pictureFile;        // optional (invalid File if none)
    juce::String pairedCabId;        // rigs: optional cab/space entry id
    juce::int64  dateAddedMs = 0;

    bool isFullRig() const { return category == Category::fullRig; }

    static Kind kindForCategory (Category c)
    {
        return (c == Category::cab || c == Category::space) ? Kind::cab : Kind::rig;
    }

    static juce::String categoryToString (Category c)
    {
        switch (c)
        {
            case Category::head:    return "head";
            case Category::fullRig: return "fullrig";
            case Category::pedal:   return "pedal";
            case Category::cab:     return "cab";
            case Category::space:   return "space";
        }
        return "head";
    }

    static Category categoryFromString (const juce::String& s, Kind fallbackKind)
    {
        if (s == "head")    return Category::head;
        if (s == "fullrig") return Category::fullRig;
        if (s == "pedal")   return Category::pedal;
        if (s == "cab")     return Category::cab;
        if (s == "space")   return Category::space;
        return fallbackKind == Kind::cab ? Category::cab : Category::head;
    }

    static juce::String categoryDisplayName (Category c)
    {
        switch (c)
        {
            case Category::head:    return "Head";
            case Category::fullRig: return "Full Rig";
            case Category::pedal:   return "Pedal";
            case Category::cab:     return "Cab";
            case Category::space:   return "Space";
        }
        return "Head";
    }
};

/** Everything the import flow collects about a new entry. */
struct AmpImportInfo
{
    juce::String name, creator, url;
    juce::StringArray tags;
    AmpLibraryEntry::Category category = AmpLibraryEntry::Category::head;
    juce::File picture;
    juce::File pairedIrToImport;     // rigs only: import this wav as a cab and pair it
};

/**
 * Owner of the on-disk amp library. Message thread only — the audio side
 * never touches this; NamAmpProcessor resolves entries on the message/loader
 * threads and only ever hands loaded DSP objects to the audio thread.
 */
class AmpLibrary
{
public:
    static AmpLibrary& instance();

    juce::File getRootFolder() const;
    void rescan();
    const juce::OwnedArray<AmpLibraryEntry>& getEntries() const { return entries; }
    const AmpLibraryEntry* findById (const juce::String& id) const;

    /** Rewrites the sidecar for an existing entry (rename / tags / pairing /
        picture / cabBakedIn changes). Returns false if the id is unknown. */
    bool updateEntry (const AmpLibraryEntry& updated);

    /** Deletes an entry's folder and removes it from the list. */
    bool deleteEntry (const juce::String& id);

    /** Imports a .nam capture (head / full rig / pedal). Returns the new entry
        id, or {} with errorOut set. info.pairedIrToImport, if valid, is
        imported as a cab entry and paired. */
    juce::String importNamFile (const juce::File& src, const AmpImportInfo& info,
                                juce::String& errorOut);

    /** Imports a standalone IR wav (cab / space). Same contract. */
    juce::String importIrFile (const juce::File& src, const AmpImportInfo& info,
                               juce::String& errorOut);

    /** A2-only gate. Parses the .nam JSON and accepts file format >= 0.7.0
        (the A2 generation). Sets a user-facing message on rejection. */
    static bool isA2NamFile (const juce::File& f, juce::String& errorOut);

    /** Fired on the message thread after any import / update / delete / rescan. */
    std::function<void()> onLibraryChanged;

    /** Rendezvous used by amp/IR editors to ask the app to show the browser in
        pick mode, filtered to the given categories. MainComponent installs the
        handler at startup. */
    std::function<void (juce::Array<AmpLibraryEntry::Category>,
                        std::function<void (juce::String /*entryId*/)>)> onPickRequested;

    void requestPick (juce::Array<AmpLibraryEntry::Category> categories,
                      std::function<void (juce::String)> cb)
    {
        if (onPickRequested)
            onPickRequested (std::move (categories), std::move (cb));
    }

private:
    AmpLibrary() = default;

    juce::String importInternal (const juce::File& src, const AmpImportInfo& info,
                                 const juce::String& pairedCabId, juce::String& errorOut);
    void writeSidecar (const AmpLibraryEntry& e);
    void notifyChanged();

    juce::OwnedArray<AmpLibraryEntry> entries;
    bool scannedOnce = false;

    JUCE_DECLARE_NON_COPYABLE (AmpLibrary)
};
