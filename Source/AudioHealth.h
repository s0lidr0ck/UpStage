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

    static void noteOverrun()   noexcept { blockOverruns  .fetch_add (1, std::memory_order_relaxed); }
    static void noteLockMiss()  noexcept { chainLockMisses.fetch_add (1, std::memory_order_relaxed); }

    static int  getOverruns()   noexcept { return blockOverruns  .load (std::memory_order_relaxed); }
    static int  getLockMisses() noexcept { return chainLockMisses.load (std::memory_order_relaxed); }
};
