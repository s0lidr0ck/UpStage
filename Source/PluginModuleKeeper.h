#pragma once

#include <JuceHeader.h>

/**
 * PluginModuleKeeper
 *
 * Keeps one instance of each plugin alive for the lifetime of the process, so
 * the plugin's DLL is never unloaded.
 *
 * ---- Why ----
 *
 * Several logged crashes named the faulting module as e.g.
 * "LANDR Composer.vst3_unloaded". Windows adds that suffix when the faulting
 * address is found in the *unloaded* module list - so the DLL had already been
 * unmapped when it faulted. The sequence is:
 *
 *   1. a plugin is removed, or the project is switched
 *   2. the last AudioPluginInstance for it is destroyed
 *   3. the host drops the last module reference and the DLL is unmapped
 *   4. a thread or timer the plugin left running fires, jumps into unmapped
 *      memory, and takes the whole app down
 *
 * That happens exactly when you switch songs, which is the worst possible time.
 *
 * A host must keep a plugin's DLL mapped while any instance of it exists -
 * otherwise every call through the instance's vtable would fault. So holding a
 * single instance is enough to pin the module, and step 3 never happens.
 *
 * ---- The trade ----
 *
 * Memory is bounded by the number of DISTINCT plugins loaded in a session, not
 * by how many times they are used: parking the last instance of a plugin costs
 * nothing extra when it is loaded again elsewhere. Kept instances have had
 * releaseResources() called, so they hold no processing buffers and burn no
 * CPU - they exist purely to hold the module open.
 *
 * ---- Honest limits ----
 *
 * This turns "callback into unmapped memory" (a guaranteed crash) into
 * "callback into code that is still mapped". That is strictly better, but it
 * is a mitigation, not a proof: a plugin whose leaked thread touches state
 * belonging to an instance we did destroy can still misbehave. It removes the
 * failure mode the crash log actually shows.
 *
 * Internal processors (UPSTAGE_INTERNAL:*) are not DLLs and are never kept.
 */
struct PluginModuleKeeper
{
    /** Park `instance` so its module stays loaded, or let it be destroyed if
        this plugin is already covered. Takes ownership either way.
        Message thread only. */
    static void keep (const juce::String& identifier,
                      std::unique_ptr<juce::AudioPluginInstance> instance)
    {
        if (instance == nullptr)
            return;

        // Internal rows live in our own binary - nothing to keep loaded.
        if (identifier.startsWith ("UPSTAGE_INTERNAL:"))
            return;   // unique_ptr destroys it here, as before

        auto& kept = getKept();

        if (identifier.isEmpty() || kept.count (identifier) > 0)
            return;   // already holding this module open; destroy normally

        // Quiet it down before parking: no buffers, no playhead back into the
        // app, no editor. It will never be processed again.
        instance->setPlayHead (nullptr);
        instance->releaseResources();

        juce::Logger::writeToLog ("PluginModuleKeeper: holding module open for "
                                  + instance->getName());

        kept.emplace (identifier, std::move (instance));
    }

    /** How many modules are being held open. Diagnostics only. */
    static int getNumKept() { return (int) getKept().size(); }

private:
    using Map = std::map<juce::String, std::unique_ptr<juce::AudioPluginInstance>>;

    /** Deliberately allocated and never freed.
        Destroying these at shutdown would reintroduce exactly the unload race
        this exists to avoid, only during teardown where it is harder to
        diagnose - and two of the logged crashes look like shutdown. Letting the
        OS reclaim the process is both safer and simpler. Function-local so
        there is no static initialisation order to reason about either. */
    static Map& getKept()
    {
        static Map* kept = new Map();
        return *kept;
    }
};
