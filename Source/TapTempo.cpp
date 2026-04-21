#include "TapTempo.h"

TapTempo::TapTempo() {}

TapTempo::~TapTempo()
{
    stopClock();
}

//==============================================================================
double TapTempo::tap()
{
    auto now = juce::Time::getHighResolutionTicks();

    // Reset if gap is too large
    if (tapCount > 0)
    {
        double elapsed = juce::Time::highResolutionTicksToSeconds (now - tapTimes[tapCount - 1]);
        if (elapsed > TAP_TIMEOUT_S)
            tapCount = 0;
    }

    if (tapCount < MAX_TAPS)
        tapTimes[tapCount++] = now;
    else
    {
        // Shift left, add new tap
        for (int i = 0; i < MAX_TAPS - 1; ++i)
            tapTimes[i] = tapTimes[i + 1];
        tapTimes[MAX_TAPS - 1] = now;
    }

    if (tapCount < 2) return 0.0;

    // Average interval over all recorded taps
    double totalSec = juce::Time::highResolutionTicksToSeconds (tapTimes[tapCount - 1] - tapTimes[0]);
    double avgIntervalSec = totalSec / (double)(tapCount - 1);
    double bpm = 60.0 / avgIntervalSec;
    bpm = juce::jlimit (MIN_BPM, MAX_BPM, bpm);

    setBPM (bpm);
    return bpm;
}

void TapTempo::setBPM (double bpm)
{
    currentBPM = juce::jlimit (MIN_BPM, MAX_BPM, bpm);
    if (clockRunning)
        updateTimerInterval();

    if (onBPMChanged)
        onBPMChanged (currentBPM);
}

//==============================================================================
void TapTempo::startClock (juce::MidiOutput* output)
{
    midiOut      = output;
    clockRunning = true;

    // Send MIDI Start message
    if (midiOut)
        midiOut->sendMessageNow (juce::MidiMessage::midiStart());

    updateTimerInterval();
}

void TapTempo::stopClock()
{
    stopTimer();
    clockRunning = false;

    if (midiOut)
    {
        midiOut->sendMessageNow (juce::MidiMessage::midiStop());
        midiOut = nullptr;
    }
}

//==============================================================================
void TapTempo::timerCallback()
{
    // Send one MIDI clock pulse (24 PPQN)
    if (midiOut)
        midiOut->sendMessageNow (juce::MidiMessage::midiClock());
}

void TapTempo::updateTimerInterval()
{
    // Interval between clock pulses in milliseconds
    // 60 seconds / BPM / 24 PPQN
    double intervalMs = (60000.0 / currentBPM) / (double) PPQN;
    startTimer (juce::roundToInt (intervalMs));
}
