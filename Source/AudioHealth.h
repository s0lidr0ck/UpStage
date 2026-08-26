#pragma once

#include <JuceHeader.h>

/**
 * AudioHealth
 *
 * Counters for things that make audio wrong without the driver ever noticing.
 *
 * The obvious measurement - AudioIODevice::getXRunCount() - is the driver's own
 * tally of dropped buffers, but not every driver reports it (the Focusrite USB
 * ASIO driver returns -1). These two counters need no driver support and
 * distinguish the two failure modes that sound identical from the outside:
 *
 *   blockOverruns    the audio callback took longer than the block budget.
 *                    We were too slow: genuine CPU load, and a dropout is
 *                    certain whether or not the driver admits it.
 *
 *   chainLockMisses  a strip could not take its chain lock, so it passed audio
 *                    through its inserts UNPROCESSED for that block. The
 *                    callback returned on time and the driver is happy, but
 *                    the signal jumped from wet to dry and back - a pop with
 *                    no underrun anywhere. Caused by the message thread
 *                    holding the lock across something slow, e.g. autosave
 *                    calling getStateInformation on every plugin.
 *
 * Both are written from the audio thread and read by the UI, so they are
 * relaxed atomics: exact ordering does not matter for a diagnostic count.
 */
struct AudioHealth
{
    /** Blocks whose processing exceeded the device's time budget. */
    static inline std::atomic<int> blockOverruns { 0 };

    /** Blocks that passed through a strip unprocessed due to lock contention. */
    static inline std::atomic<int> chainLockMisses { 0 };

    /** Worst whole-callback time, in microseconds, since the last read. */
    static inline std::atomic<int> maxBlockUs { 0 };

    /** Worst time spent inside plugin processBlock calls, same window. */
    static inline std::atomic<int> maxPluginUs { 0 };

    /** The block budget in microseconds, so the UI can show worst/budget. */
    static inline std::atomic<int> budgetUs { 0 };

    static void noteBlockTime (int totalUs, int pluginUs, int budget) noexcept
    {
        budgetUs.store (budget, std::memory_order_relaxed);
        storeMax (maxBlockUs,  totalUs);
        storeMax (maxPluginUs, pluginUs);
    }

    /** Read the peaks and reset them, so each UI update shows the worst case
        since the previous one rather than an all-time figure that never falls. */
    static void takePeaks (int& blockUs, int& pluginUs, int& budget) noexcept
    {
        blockUs  = maxBlockUs .exchange (0, std::memory_order_relaxed);
        pluginUs = maxPluginUs.exchange (0, std::memory_order_relaxed);
        budget   = budgetUs.load (std::memory_order_relaxed);
    }

    //==========================================================================
    // Worst single plugin, by processBlock time.
    //
    // Average CPU can look fine while one plugin periodically stalls for
    // milliseconds - a licensing check, a lazy resource load, or garbage
    // collection in an embedded scripting runtime. That reads as an occasional
    // pop and is invisible in any averaged figure. This names the culprit.

    static constexpr int kNameLen = 32;
    static inline char worstName[kNameLen] { 0 };
    static inline std::atomic<int> worstPluginUs { 0 };

    /** Audio thread. Records `name` if this is the slowest plugin so far in
        the current window. The name is only read by the UI after the window
        closes, and a torn name would at worst mislabel one display update. */
    static void notePlugin (const char* name, int us) noexcept
    {
        if (us <= worstPluginUs.load (std::memory_order_relaxed))
            return;

        worstPluginUs.store (us, std::memory_order_relaxed);

        int i = 0;
        for (; name != nullptr && name[i] != 0 && i < kNameLen - 1; ++i)
            worstName[i] = name[i];
        worstName[i] = 0;
    }

    static void takeWorstPlugin (juce::String& name, int& us) noexcept
    {
        us   = worstPluginUs.exchange (0, std::memory_order_relaxed);
        name = juce::String (worstName);
    }

    //==========================================================================
    // Callback delivery.
    //
    // Every other counter here measures how long WE take. None of them notice
    // if the driver simply fails to call us on time - which is a dropout we
    // cause none of and can see none of. If these move while BLK and OVR stay
    // at zero, the problem is below us: the interface, its driver, or system
    // DPC latency. No amount of work in this codebase fixes that.

    /** Callbacks that arrived far later than the block period implies. */
    static inline std::atomic<int> lateCallbacks { 0 };

    /** Worst gap between callback entries, microseconds, since last read. */
    static inline std::atomic<int> maxGapUs { 0 };

    static void noteCallbackGap (int gapUs, int expectedUs) noexcept
    {
        storeMax (maxGapUs, gapUs);

        // Half a block late is well beyond normal scheduling jitter.
        if (gapUs > expectedUs + expectedUs / 2)
            lateCallbacks.fetch_add (1, std::memory_order_relaxed);
    }

    static void takeGap (int& gapUs, int& late) noexcept
    {
        gapUs = maxGapUs.exchange (0, std::memory_order_relaxed);
        late  = lateCallbacks.load (std::memory_order_relaxed);
    }

    static void noteOverrun()   noexcept { blockOverruns  .fetch_add (1, std::memory_order_relaxed); }
    static void noteLockMiss()  noexcept { chainLockMisses.fetch_add (1, std::memory_order_relaxed); }

    static int  getOverruns()   noexcept { return blockOverruns  .load (std::memory_order_relaxed); }
    static int  getLockMisses() noexcept { return chainLockMisses.load (std::memory_order_relaxed); }

private:
    static void storeMax (std::atomic<int>& target, int value) noexcept
    {
        int prev = target.load (std::memory_order_relaxed);
        while (value > prev
               && ! target.compare_exchange_weak (prev, value, std::memory_order_relaxed))
        {} // prev is refreshed by the failed exchange
    }
};
