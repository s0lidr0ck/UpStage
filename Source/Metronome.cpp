#include "Metronome.h"

Metronome::Metronome() {}

void Metronome::prepare (double sr, int)
{
    sampleRate = sr;
    recalcTiming();
}

void Metronome::setBPM (double bpm)
{
    currentBPM.store (juce::jlimit (30.0, 300.0, bpm));
    recalcTiming();
}

void Metronome::setTimeSignature (int numerator, int denominator)
{
    timeSigNum = juce::jlimit (1, 12, numerator);
    timeSigDen = juce::jlimit (2, 16, denominator);
    recalcTiming();
}

void Metronome::setSubdivision (int sub)
{
    subdivision = juce::jlimit (1, 4, sub);
    recalcTiming();
}

void Metronome::recalcTiming()
{
    double bpm = currentBPM.load();
    int samplesPerBeat = juce::roundToInt (sampleRate * 60.0 / bpm);
    samplesPerTick = samplesPerBeat / juce::jmax (1, subdivision);
}

float Metronome::generateClickSample()
{
    float sample = 0.0f;
    auto sound = clickSound.load();

    switch (sound)
    {
        case ClickSound::Sine:
            sample = std::sin ((float) clickPhase);
            break;

        case ClickSound::Tick:
        {
            sample = std::sin ((float) clickPhase);
            float noise = noiseRng.nextFloat() * 2.0f - 1.0f;
            sample = sample * 0.6f + noise * 0.4f;
            break;
        }

        case ClickSound::Woodblock:
        {
            sample = std::sin ((float) clickPhase);
            float harmonic = std::sin ((float)(clickPhase * 2.7));
            sample = sample * 0.7f + harmonic * 0.3f;
            break;
        }
    }

    clickPhase += clickPhaseInc;
    return sample;
}

void Metronome::processBlock (juce::AudioBuffer<float>& buffer)
{
    if (! enabled.load())
        return;

    const float vol = volume.load();
    const int numSamples = buffer.getNumSamples();
    const int ticksPerBeat = juce::jmax (1, subdivision);
    const bool useAccent = accentOn.load();
    const double aFreq = accentFreq.load();
    const double nFreq = normalFreq.load();

    for (int i = 0; i < numSamples; ++i)
    {
        if (sampleCounter == 0)
        {
            int beatIndex = tickCounter / ticksPerBeat;
            int subIndex  = tickCounter % ticksPerBeat;

            isAccent_ = useAccent && (subIndex == 0) && (beatIndex % timeSigNum == 0);
            isSubTick = (subIndex != 0);

            clickSamplesLeft = isSubTick ? CLICK_DURATION / 2 : CLICK_DURATION;
            clickPhase = 0.0;

            double freq = isAccent_ ? aFreq : (isSubTick ? nFreq * 0.8 : nFreq);
            clickPhaseInc = freq * juce::MathConstants<double>::twoPi / sampleRate;

            if (subIndex == 0)
                beatFlash.store (true);

            tickCounter++;
        }

        float sample = 0.0f;
        if (clickSamplesLeft > 0)
        {
            int totalDur = isSubTick ? CLICK_DURATION / 2 : CLICK_DURATION;
            float envelope = (float) clickSamplesLeft / (float) totalDur;
            envelope *= envelope;
            float clickVol = isSubTick ? vol * 0.5f : vol;
            sample = generateClickSample() * envelope * clickVol;
            clickSamplesLeft--;
        }

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (ch)[i] += sample;

        sampleCounter++;
        if (sampleCounter >= samplesPerTick)
            sampleCounter = 0;
    }
}
