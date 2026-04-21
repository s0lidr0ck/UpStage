#pragma once
#include <JuceHeader.h>

/**
 * TapTempo
 *
 * Detects BPM from tap events and broadcasts MIDI clock (24 PPQN) to
 * the active MIDI output device so delay/modulation plugins lock to the tempo.
 *
 * Usage:
 *   1. User taps a button → call tap()
 *   2. After 2+ taps, getBPM() returns the detected tempo
 *   3. Call startClock() to begin broadcasting MIDI clock to plugs/outDevice
 *   4. Call stopClock() when no longer needed
 *
 * BPM range: 40 – 300
 * Tap window: taps more than 3 seconds apart reset the sequence
 */
class TapTempo : private juce::Timer
{
public:
    TapTempo();
    ~TapTempo() override;

    //==========================================================================
    /** Record a tap event. Returns the new BPM (0 if not yet calculable). */
    double tap();

    /** Manually set BPM (also updates clock rate if running). */
    void   setBPM (double bpm);
    double getBPM() const { return currentBPM; }

    //==========================================================================
    /** Start / stop the MIDI clock output. */
    void startClock (juce::MidiOutput* output);
    void stopClock();
    bool isClockRunning() const { return clockRunning; }

    //==========================================================================
    /** Callback fired on message thread whenever BPM changes. */
    std::function<void(double bpm)> onBPMChanged;

private:
    static constexpr int    MAX_TAPS      = 4;
    static constexpr double TAP_TIMEOUT_S = 3.0;
    static constexpr double MIN_BPM       = 40.0;
    static constexpr double MAX_BPM       = 300.0;
    static constexpr int    PPQN          = 24;

    juce::int64  tapTimes[MAX_TAPS] {};
    int          tapCount   = 0;
    double       currentBPM = 120.0;
    bool         clockRunning = false;

    juce::MidiOutput* midiOut = nullptr;

    void timerCallback() override;  // fires at 24 PPQN interval
    void updateTimerInterval();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapTempo)
};
