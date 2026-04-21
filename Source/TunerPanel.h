#pragma once
#include <JuceHeader.h>

/**
 * TunerPanel
 *
 * Chromatic pitch detector. Reads audio input and shows:
 *   - Detected note name (A–G + accidentals)
 *   - Octave number
 *   - Cents deviation (-50 to +50 cents, +/- 0.5 semitone)
 *   - Visual needle/bar
 *
 * While the tuner is visible/active, output audio is muted
 * (silent tuning on stage).
 *
 * Pitch detection algorithm: autocorrelation (YIN-simplified)
 * Works well for monophonic signals (guitar, bass, single-note).
 *
 * Usage:
 *   1. Call pushAudioData() from the audio thread with your input buffer
 *   2. Component polls results in paint() via timerCallback
 *   3. Set tunerActive = true to mute main output + start detection
 */
class TunerPanel : public juce::Component,
                   private juce::Timer
{
public:
    TunerPanel();
    ~TunerPanel() override;

    //==========================================================================
    // Called from the audio thread — feed input samples in
    void pushAudioData (const float* samples, int numSamples);

    //==========================================================================
    bool   tunerActive = false;  // set true to mute output + show detection

    //==========================================================================
    // Component
    void paint  (juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int   BUFFER_SIZE     = 4096;
    static constexpr float MIN_FREQ        = 60.0f;   // B1
    static constexpr float MAX_FREQ        = 1400.0f; // ~F6
    static constexpr float CLARITY_THRESH  = 0.93f;

    //==========================================================================
    // Pitch detection
    float  detectPitch (float sampleRate);
    float  currentPitch      = 0.0f;   // Hz, 0 = no signal
    float  currentCents      = 0.0f;
    int    currentNoteIndex  = -1;     // 0=C, 1=C#, 2=D, ...
    int    currentOctave     = 4;

    float  sampleRate = 44100.0f;

    //==========================================================================
    // Ring buffer (audio thread writes, timer reads)
    float         ringBuffer[BUFFER_SIZE] {};
    std::atomic<int> writePos { 0 };

    void           timerCallback() override;
    void           analyzeBuffer();

    //==========================================================================
    // Note names
    static const char* noteNames[12];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerPanel)
};
