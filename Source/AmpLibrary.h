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

    juce::String id;                 // Uuid string, stable forever
    Kind         kind = Kind::rig;
    juce::String name, creator, notes;
    juce::StringArray tags;
    juce::File   folder;             // this entry's subfolder
    juce::File   namFile;            // rigs only
    juce::File   irFile;             // cabs only: the IR wav
    juce::File   pictureFile;        // optional (invalid File if none)
    juce::String pairedCabId;        // rigs: optional cab entry id
    bool         cabBakedIn = false; // rigs: capture already includes cabinet
    juce::int64  dateAddedMs = 0;
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

    /** Imports a .nam capture. Returns the new entry id, or {} with errorOut
        set. pairedIrToImport may be an invalid File (no pairing), an IR wav to
        import alongside, and picture may be invalid (no picture). */
    juce::String importNamFile (const juce::File& src, const juce::String& name,
                                const juce::File& picture, const juce::StringArray& tags,
                                bool cabBakedIn, const juce::File& pairedIrToImport,
                                juce::String& errorOut);

    /** Imports a standalone cabinet IR wav. Same contract as importNamFile. */
    juce::String importIrFile (const juce::File& src, const juce::String& name,
                               const juce::File& picture, const juce::StringArray& tags,
                               juce::String& errorOut);

    /** A2-only gate. Parses the .nam JSON and accepts file format >= 0.7.0
        (the A2 generation). Sets a user-facing message on rejection. */
    static bool isA2NamFile (const juce::File& f, juce::String& errorOut);

    /** Fired on the message thread after any import / update / delete / rescan. */
    std::function<void()> onLibraryChanged;

    /** Rendezvous used by amp editors to ask the app to show the browser in
        pick mode. MainComponent installs the handler at startup. */
    std::function<void (AmpLibraryEntry::Kind,
                        std::function<void (juce::String /*entryId*/)>)> onPickRequested;

    void requestPick (AmpLibraryEntry::Kind k, std::function<void (juce::String)> cb)
    {
        if (onPickRequested)
            onPickRequested (k, std::move (cb));
    }

private:
    AmpLibrary() = default;

    juce::String importInternal (AmpLibraryEntry::Kind kind, const juce::File& src,
                                 const juce::String& name, const juce::File& picture,
                                 const juce::StringArray& tags, bool cabBakedIn,
                                 const juce::String& pairedCabId, juce::String& errorOut);
    void writeSidecar (const AmpLibraryEntry& e);
    void notifyChanged();

    juce::OwnedArray<AmpLibraryEntry> entries;

    JUCE_DECLARE_NON_COPYABLE (AmpLibrary)
};
