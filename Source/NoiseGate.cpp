#include "NoiseGate.h"
#include <cmath>

NoiseGate::NoiseGate() {}

void NoiseGate::prepare (double sr, int /*blockSize*/)
{
    sampleRate = sr;
    updateCoefficients();
    currentGain  = 0.0f;
    envLevel     = 0.0f;
    state        = State::Closed;
    holdCounter  = 0;
}

void NoiseGate::updateCoefficients()
{
    attackCoeff  = (attackMs  > 0.0f) ? std::exp (-1.0f / (float)(sampleRate * attackMs  * 0.001)) : 0.0f;
    releaseCoeff = (releaseMs > 0.0f) ? std::exp (-1.0f / (float)(sampleRate * releaseMs * 0.001)) : 0.0f;
    holdSamples  = (int)(sampleRate * holdMs * 0.001);
}

//==============================================================================
void NoiseGate::processBlock (juce::AudioBuffer<float>& buffer)
{
    if (! enabled) return;

    updateCoefficients();

    float threshold = juce::Decibels::decibelsToGain (thresholdDb);
    int   numCh     = buffer.getNumChannels();
    int   numSamp   = buffer.getNumSamples();

    for (int s = 0; s < numSamp; ++s)
    {
        // Compute peak envelope across all channels
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, s)));

        // Envelope follower
        if (peak > envLevel)
            envLevel = peak; // instant attack tracking
        else
            envLevel = envLevel * releaseCoeff + peak * (1.0f - releaseCoeff);

        // State machine
        switch (state)
        {
            case State::Closed:
                if (envLevel >= threshold)
                    state = State::Opening;
                break;

            case State::Opening:
                currentGain += (1.0f - currentGain) * (1.0f - attackCoeff);
                if (currentGain >= 0.999f) { currentGain = 1.0f; state = State::Open; }
                break;

            case State::Open:
                if (envLevel < threshold)
                {
                    state        = State::Holding;
                    holdCounter  = holdSamples;
                }
                break;

            case State::Holding:
                if (envLevel >= threshold)
                    state = State::Open;
                else if (--holdCounter <= 0)
                    state = State::Closing;
                break;

            case State::Closing:
                currentGain *= releaseCoeff;
                if (currentGain < 0.001f) { currentGain = 0.0f; state = State::Closed; }
                break;
        }

        // Apply gain to all channels
        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample (ch, s, buffer.getSample (ch, s) * currentGain);
    }
}
