#include "AmpLibrary.h"

AmpLibrary& AmpLibrary::instance()
{
    static AmpLibrary lib;
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
        e->notes       = parsed.getProperty ("notes", "").toString();
        e->pairedCabId = parsed.getProperty ("pairedCabId", "").toString();
        e->cabBakedIn  = (bool) parsed.getProperty ("cabBakedIn", false);
        e->dateAddedMs = (juce::int64) parsed.getProperty ("dateAddedMs", 0);
        e->folder      = dir;

        if (auto* tagArr = parsed.getProperty ("tags", juce::var()).getArray())
            for (const auto& t : *tagArr)
                e->tags.add (t.toString());

        const auto namName = parsed.getProperty ("namFile", "").toString();
        const auto irName  = parsed.getProperty ("irFile", "").toString();
        const auto picName = parsed.getProperty ("picture", "").toString();
        if (namName.isNotEmpty()) e->namFile     = dir.getChildFile (namName);
        if (irName.isNotEmpty())  e->irFile      = dir.getChildFile (irName);
        if (picName.isNotEmpty()) e->pictureFile = dir.getChildFile (picName);

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
    obj->setProperty ("name",        e.name);
    obj->setProperty ("creator",     e.creator);
    obj->setProperty ("notes",       e.notes);
    obj->setProperty ("pairedCabId", e.pairedCabId);
    obj->setProperty ("cabBakedIn",  e.cabBakedIn);
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

bool AmpLibrary::isA2NamFile (const juce::File& f, juce::String& errorOut)
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

    const int major = parts[0].getIntValue();
    const int minor = parts[1].getIntValue();

    // A2-generation captures are NAM file format 0.7.0+; 0.5.x / 0.6.x are A1-era.
    if (major > 0 || minor >= 7)
        return true;

    errorOut = "This is an A1-era NAM model (file version " + versionStr
             + "). UpStage supports A2 models only - look for the A2 badge on tone3000.com.";
    return false;
}

juce::String AmpLibrary::importInternal (AmpLibraryEntry::Kind kind, const juce::File& src,
                                         const juce::String& name, const juce::File& picture,
                                         const juce::StringArray& tags, bool cabBakedIn,
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
    e->kind        = kind;
    e->name        = name.isNotEmpty() ? name : src.getFileNameWithoutExtension();
    e->tags        = tags;
    e->folder      = folder;
    e->cabBakedIn  = cabBakedIn;
    e->pairedCabId = pairedCabId;
    e->dateAddedMs = juce::Time::getCurrentTime().toMilliseconds();

    if (kind == AmpLibraryEntry::Kind::rig)
        e->namFile = dest;
    else
        e->irFile = dest;

    if (picture.existsAsFile())
    {
        auto picDest = folder.getChildFile ("picture" + picture.getFileExtension());
        if (picture.copyFileTo (picDest))
            e->pictureFile = picDest;
    }

    writeSidecar (*e);
    auto* raw = e.release();
    entries.insert (0, raw);
    notifyChanged();
    return raw->id;
}

juce::String AmpLibrary::importNamFile (const juce::File& src, const juce::String& name,
                                        const juce::File& picture, const juce::StringArray& tags,
                                        bool cabBakedIn, const juce::File& pairedIrToImport,
                                        juce::String& errorOut)
{
    if (! isA2NamFile (src, errorOut))
        return {};

    juce::String pairedCabId;
    if (pairedIrToImport.existsAsFile())
    {
        pairedCabId = importIrFile (pairedIrToImport, pairedIrToImport.getFileNameWithoutExtension(),
                                    juce::File(), tags, errorOut);
        if (pairedCabId.isEmpty())
            return {};
    }

    return importInternal (AmpLibraryEntry::Kind::rig, src, name, picture, tags,
                           cabBakedIn, pairedCabId, errorOut);
}

juce::String AmpLibrary::importIrFile (const juce::File& src, const juce::String& name,
                                       const juce::File& picture, const juce::StringArray& tags,
                                       juce::String& errorOut)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (src));
    if (reader == nullptr)
    {
        errorOut = src.getFileName() + " isn't a readable audio file - cabinet IRs must be WAV.";
        return {};
    }

    return importInternal (AmpLibraryEntry::Kind::cab, src, name, picture, tags,
                           false, {}, errorOut);
}

void AmpLibrary::notifyChanged()
{
    if (onLibraryChanged)
        onLibraryChanged();
}
