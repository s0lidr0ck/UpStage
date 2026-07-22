#include "AmpLibrary.h"

AmpLibrary& AmpLibrary::instance()
{
    static AmpLibrary lib;
    if (! lib.scannedOnce)
    {
        // First-access scan so project loads can resolve rig ids even if the
        // browser window has never been opened this session.
        lib.scannedOnce = true;
        lib.rescan();
    }
    return lib;
}

juce::File AmpLibrary::getRootFolder() const
{
    auto root = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                    .getChildFile ("UpStage").getChildFile ("Amp Library");
    root.createDirectory();
    return root;
}

static juce::String kindToString (AmpLibraryEntry::Kind k)
{
    return k == AmpLibraryEntry::Kind::rig ? "rig" : "cab";
}

void AmpLibrary::rescan()
{
    entries.clear();

    for (const auto& dir : getRootFolder().findChildFiles (juce::File::findDirectories, false))
    {
        auto sidecar = dir.getChildFile ("entry.json");
        if (! sidecar.existsAsFile())
            continue;

        auto parsed = juce::JSON::parse (sidecar.loadFileAsString());
        if (! parsed.isObject())
        {
            juce::Logger::writeToLog ("AmpLibrary: skipping unreadable sidecar in " + dir.getFileName());
            continue;
        }

        auto e = std::make_unique<AmpLibraryEntry>();
        e->id          = parsed.getProperty ("id", dir.getFileName()).toString();
        e->kind        = parsed.getProperty ("kind", "rig").toString() == "cab" ? AmpLibraryEntry::Kind::cab
                                                                                : AmpLibraryEntry::Kind::rig;
        e->name        = parsed.getProperty ("name", "Unnamed").toString();
        e->creator     = parsed.getProperty ("creator", "").toString();
        e->url         = parsed.getProperty ("url", "").toString();
        e->notes       = parsed.getProperty ("notes", "").toString();
        e->pairedCabId = parsed.getProperty ("pairedCabId", "").toString();
        e->dateAddedMs = (juce::int64) parsed.getProperty ("dateAddedMs", 0);
        e->folder      = dir;

        // Category, migrating pre-taxonomy sidecars: full-rig flag -> Full Rig,
        // other captures -> Head, IRs -> Cab.
        const auto catStr = parsed.getProperty ("category", "").toString();
        if (catStr.isNotEmpty())
            e->category = AmpLibraryEntry::categoryFromString (catStr, e->kind);
        else if ((bool) parsed.getProperty ("cabBakedIn", false))
            e->category = AmpLibraryEntry::Category::fullRig;
        else
            e->category = e->kind == AmpLibraryEntry::Kind::cab ? AmpLibraryEntry::Category::cab
                                                                : AmpLibraryEntry::Category::head;

        if (auto* tagArr = parsed.getProperty ("tags", juce::var()).getArray())
            for (const auto& t : *tagArr)
                e->tags.add (t.toString());

        const auto namName = parsed.getProperty ("namFile", "").toString();
        const auto irName  = parsed.getProperty ("irFile", "").toString();
        const auto picName = parsed.getProperty ("picture", "").toString();
        if (namName.isNotEmpty()) e->namFile     = dir.getChildFile (namName);
        if (irName.isNotEmpty())  e->irFile      = dir.getChildFile (irName);
        if (picName.isNotEmpty()) e->pictureFile = dir.getChildFile (picName);

        // Version, migrating entries imported before it was recorded.
        e->namVersion = parsed.getProperty ("namVersion", "").toString();
        if (e->namVersion.isEmpty() && e->namFile.existsAsFile())
        {
            auto modelJson = juce::JSON::parse (e->namFile.loadFileAsString());
            e->namVersion = modelJson.getProperty ("version", "").toString();
            if (e->namVersion.isNotEmpty())
                writeSidecar (*e);
        }

        entries.add (e.release());
    }

    // Stable, friendly ordering: newest first within each kind.
    std::sort (entries.begin(), entries.end(), [] (const AmpLibraryEntry* a, const AmpLibraryEntry* b)
    {
        if (a->kind != b->kind)
            return a->kind == AmpLibraryEntry::Kind::rig;
        return a->dateAddedMs > b->dateAddedMs;
    });

    notifyChanged();
}

const AmpLibraryEntry* AmpLibrary::findById (const juce::String& id) const
{
    for (auto* e : entries)
        if (e->id == id)
            return e;
    return nullptr;
}

void AmpLibrary::writeSidecar (const AmpLibraryEntry& e)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("id",          e.id);
    obj->setProperty ("kind",        kindToString (e.kind));
    obj->setProperty ("category",    AmpLibraryEntry::categoryToString (e.category));
    obj->setProperty ("namVersion",  e.namVersion);
    obj->setProperty ("name",        e.name);
    obj->setProperty ("creator",     e.creator);
    obj->setProperty ("url",         e.url);
    obj->setProperty ("notes",       e.notes);
    obj->setProperty ("pairedCabId", e.pairedCabId);
    obj->setProperty ("dateAddedMs", e.dateAddedMs);

    juce::Array<juce::var> tagVars;
    for (const auto& t : e.tags)
        tagVars.add (t);
    obj->setProperty ("tags", tagVars);

    obj->setProperty ("namFile", e.namFile  == juce::File() ? "" : e.namFile.getFileName());
    obj->setProperty ("irFile",  e.irFile   == juce::File() ? "" : e.irFile.getFileName());
    obj->setProperty ("picture", e.pictureFile == juce::File() ? "" : e.pictureFile.getFileName());

    e.folder.getChildFile ("entry.json")
        .replaceWithText (juce::JSON::toString (juce::var (obj)));
}

bool AmpLibrary::updateEntry (const AmpLibraryEntry& updated)
{
    for (auto* e : entries)
    {
        if (e->id == updated.id)
        {
            *e = updated;
            writeSidecar (*e);
            notifyChanged();
            return true;
        }
    }
    return false;
}

bool AmpLibrary::deleteEntry (const juce::String& id)
{
    for (int i = 0; i < entries.size(); ++i)
    {
        if (entries[i]->id == id)
        {
            entries[i]->folder.deleteRecursively();
            entries.remove (i);
            notifyChanged();
            return true;
        }
    }
    return false;
}

bool AmpLibrary::isSupportedNamFile (const juce::File& f, juce::String& versionOut,
                                     juce::String& errorOut)
{
    if (! f.existsAsFile())
    {
        errorOut = "File not found: " + f.getFullPathName();
        return false;
    }

    auto parsed = juce::JSON::parse (f.loadFileAsString());
    if (! parsed.isObject())
    {
        errorOut = "This file isn't a readable NAM model.";
        return false;
    }

    const auto versionStr = parsed.getProperty ("version", "").toString();
    auto parts = juce::StringArray::fromTokens (versionStr, ".", "");
    if (parts.size() < 2)
    {
        errorOut = "This file isn't a readable NAM model (no version field).";
        return false;
    }

    versionOut = versionStr;
    const int major = parts[0].getIntValue();
    const int minor = parts[1].getIntValue();

    // The embedded NAM engine supports file formats 0.5.0 (A1 era) through
    // 0.7.x (A2). Anything older needs re-training on tone3000.com.
    if (major > 0 || minor >= 5)
        return true;

    errorOut = "This NAM model uses file format " + versionStr
             + ", which is too old for the embedded engine (0.5.0+ required). "
               "Re-download or re-train it on tone3000.com.";
    return false;
}

juce::String AmpLibrary::importInternal (const juce::File& src, const AmpImportInfo& info,
                                         AmpLibraryEntry::Kind storageKind, const juce::String& namVersion,
                                         const juce::String& pairedCabId, juce::String& errorOut)
{
    const auto id = juce::Uuid().toString();
    auto folder = getRootFolder().getChildFile (id);

    if (! folder.createDirectory())
    {
        errorOut = "Couldn't create library folder: " + folder.getFullPathName();
        return {};
    }

    auto fail = [&] (const juce::String& msg) -> juce::String
    {
        folder.deleteRecursively();
        errorOut = msg;
        return {};
    };

    auto dest = folder.getChildFile (src.getFileName());
    if (! src.copyFileTo (dest))
        return fail ("Couldn't copy " + src.getFileName() + " into the library.");

    auto e = std::make_unique<AmpLibraryEntry>();
    e->id          = id;
    e->category    = info.category;
    e->kind        = storageKind;
    e->namVersion  = namVersion;
    e->name        = info.name.isNotEmpty() ? info.name : src.getFileNameWithoutExtension();
    e->creator     = info.creator;
    e->url         = info.url;
    e->tags        = info.tags;
    e->folder      = folder;
    e->pairedCabId = pairedCabId;
    e->dateAddedMs = juce::Time::getCurrentTime().toMilliseconds();

    if (e->kind == AmpLibraryEntry::Kind::rig)
        e->namFile = dest;
    else
        e->irFile = dest;

    if (info.picture.existsAsFile())
    {
        auto picDest = folder.getChildFile ("picture" + info.picture.getFileExtension());
        if (info.picture.copyFileTo (picDest))
            e->pictureFile = picDest;
    }

    writeSidecar (*e);
    auto* raw = e.release();
    entries.insert (0, raw);
    notifyChanged();
    return raw->id;
}

juce::String AmpLibrary::importNamFile (const juce::File& src, const AmpImportInfo& info,
                                        juce::String& errorOut)
{
    juce::String version;
    if (! isSupportedNamFile (src, version, errorOut))
        return {};

    // A .nam capture can be a head, full rig, pedal, or cab (NAM cab capture);
    // spaces are IR wavs only.
    AmpImportInfo fixed = info;
    if (fixed.category == AmpLibraryEntry::Category::space)
        fixed.category = AmpLibraryEntry::Category::cab;

    juce::String pairedCabId;
    if (info.pairedIrToImport.existsAsFile())
    {
        AmpImportInfo irInfo;
        irInfo.name     = info.pairedIrToImport.getFileNameWithoutExtension();
        irInfo.creator  = info.creator;
        irInfo.tags     = info.tags;
        irInfo.category = AmpLibraryEntry::Category::cab;
        pairedCabId = importIrFile (info.pairedIrToImport, irInfo, errorOut);
        if (pairedCabId.isEmpty())
            return {};
    }

    return importInternal (src, fixed, AmpLibraryEntry::Kind::rig, version, pairedCabId, errorOut);
}

juce::String AmpLibrary::importIrFile (const juce::File& src, const AmpImportInfo& info,
                                       juce::String& errorOut)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (src));
    if (reader == nullptr)
    {
        errorOut = src.getFileName() + " isn't a readable audio file - cabinet/space IRs must be WAV.";
        return {};
    }

    AmpImportInfo fixed = info;
    if (fixed.category != AmpLibraryEntry::Category::cab
        && fixed.category != AmpLibraryEntry::Category::space)
        fixed.category = AmpLibraryEntry::Category::cab;

    return importInternal (src, fixed, AmpLibraryEntry::Kind::cab, {}, {}, errorOut);
}

void AmpLibrary::notifyChanged()
{
    if (onLibraryChanged)
        onLibraryChanged();
}
