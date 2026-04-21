#pragma once
#include <JuceHeader.h>

/**
 * NoiseGate
 *
 * Simple envelope-following noise gate for the global input signal.
 * Placed on the input signal before it reaches any ChannelStrip.
 *
 * Parameters:
 *   thresholdDb   — gate opens above this level  (default: -60 dBFS)
 *   attackMs      — time to open  (default: 5 ms)
 *   holdMs        — time to stay open after signal drops below threshold (default: 50 ms)
 *   releaseMs     — time to close (default: 100 ms)
 *
 * The gate is applied per-block, sample-accurate envelope following.
 * Enabled flag lets you bypass it entirely.
 */
class NoiseGate
{
public:
    NoiseGate();

    void prepare (double sampleRate, int blockSize);
    void processBlock (juce::AudioBuffer<float>& buffer);

    //==========================================================================
    bool  enabled       = true;
    float thresholdDb   = -60.0f;
    float attackMs      = 5.0f;
    float holdMs        = 50.0f;
    float releaseMs     = 100.0f;

    /** 0.0 = fully closed, 1.0 = fully open — use for UI metering */
    float getCurrentGain() const { return currentGain; }

private:
    double sampleRate = 44100.0;

    // State
    enum class State { Closed, Opening, Open, Holding, Closing };
    State state      = State::Closed;
    float currentGain = 0.0f;
    float envLevel    = 0.0f;
    int   holdSamples = 0;
    int   holdCounter = 0;

    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;

    void updateCoefficients();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseGate)
};
