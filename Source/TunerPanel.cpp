#include "TunerPanel.h"
#include <cmath>

const char* TunerPanel::noteNames[12] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

//==============================================================================
TunerPanel::TunerPanel()
{
    startTimerHz (20); // update display at 20 fps
}

TunerPanel::~TunerPanel()
{
    stopTimer();
}

//==============================================================================
void TunerPanel::pushAudioData (const float* samples, int numSamples)
{
    int pos = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer[pos] = samples[i];
        pos = (pos + 1) % BUFFER_SIZE;
    }
    writePos.store (pos, std::memory_order_release);
}

//==============================================================================
void TunerPanel::timerCallback()
{
    if (tunerActive)
        analyzeBuffer();
    repaint();
}

void TunerPanel::analyzeBuffer()
{
    // Copy ring buffer into a contiguous array starting from oldest sample
    float buf[BUFFER_SIZE];
    int pos = writePos.load (std::memory_order_acquire);
    for (int i = 0; i < BUFFER_SIZE; ++i)
        buf[i] = ringBuffer[(pos + i) % BUFFER_SIZE];

    float pitch = detectPitch (sampleRate);

    if (pitch > MIN_FREQ && pitch < MAX_FREQ)
    {
        currentPitch = pitch;

        // Convert Hz to note + cents
        // A4 = 440 Hz, MIDI note 69
        float midiNote = 12.0f * std::log2f (pitch / 440.0f) + 69.0f;
        int   nearestNote = juce::roundToInt (midiNote);
        currentCents      = (midiNote - (float)nearestNote) * 100.0f;
        currentNoteIndex  = ((nearestNote % 12) + 12) % 12;
        currentOctave     = (nearestNote / 12) - 1;
    }
    else
    {
        currentPitch = 0.0f;
    }
}

float TunerPanel::detectPitch (float sr)
{
    // YIN-simplified autocorrelation pitch detection
    int pos = writePos.load (std::memory_order_acquire);
    float buf[BUFFER_SIZE];
    for (int i = 0; i < BUFFER_SIZE; ++i)
        buf[i] = ringBuffer[(pos + i) % BUFFER_SIZE];

    int minPeriod = (int)(sr / MAX_FREQ);
    int maxPeriod = (int)(sr / MIN_FREQ);
    maxPeriod = juce::jmin (maxPeriod, BUFFER_SIZE / 2);

    // Compute difference function
    float bestCorr = 0.0f;
    int   bestTau  = 0;

    for (int tau = minPeriod; tau < maxPeriod; ++tau)
    {
        float corr = 0.0f;
        float norm = 0.0f;
        for (int i = 0; i < maxPeriod; ++i)
        {
            float diff = buf[i] - buf[i + tau];
            corr += diff * diff;
            norm += buf[i] * buf[i] + buf[i + tau] * buf[i + tau];
        }
        float clarity = (norm > 0.0f) ? (1.0f - corr / norm) : 0.0f;
        if (clarity > bestCorr)
        {
            bestCorr = clarity;
            bestTau  = tau;
        }
    }

    if (bestCorr < CLARITY_THRESH || bestTau == 0) return 0.0f;

    // Parabolic interpolation for sub-sample accuracy
    if (bestTau > 0 && bestTau < maxPeriod - 1)
    {
        float y0 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        auto corr = [&] (int tau) -> float {
            float c = 0.0f, n = 0.0f;
            for (int i = 0; i < maxPeriod; ++i) {
                float d = buf[i] - buf[i+tau];
                c += d*d;
                n += buf[i]*buf[i] + buf[i+tau]*buf[i+tau];
            }
            return (n > 0.0f) ? (1.0f - c/n) : 0.0f;
        };
        y0 = corr (bestTau - 1);
        y1 = corr (bestTau);
        y2 = corr (bestTau + 1);
        float denom = 2.0f * (2.0f * y1 - y0 - y2);
        float refinedTau = (float)bestTau + (denom != 0.0f ? (y0 - y2) / denom : 0.0f);
        return sr / refinedTau;
    }

    return sr / (float)bestTau;
}

//==============================================================================
void TunerPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff111122));
    g.setColour (juce::Colour (0xff444466));
    g.drawRect (bounds, 1.0f);

    if (! tunerActive || currentPitch == 0.0f)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::Font(juce::FontOptions().withHeight(18.0f)));
        g.drawText ("TUNER — play a note", bounds, juce::Justification::centred);
        return;
    }

    // Note name + octave
    juce::String noteStr = juce::String (noteNames[currentNoteIndex]) + juce::String (currentOctave);
    g.setFont (juce::Font (juce::FontOptions()
                               .withName  (juce::Font::getDefaultMonospacedFontName())
                               .withHeight (48.0f)
                               .withStyle  ("Bold")));

    // Colour: green within ±5 cents, yellow within ±15, red outside
    auto absC = std::abs (currentCents);
    if      (absC <= 5.0f)  g.setColour (juce::Colours::limegreen);
    else if (absC <= 15.0f) g.setColour (juce::Colours::yellow);
    else                    g.setColour (juce::Colours::orangered);

    g.drawText (noteStr, bounds.removeFromTop (bounds.getHeight() * 0.55f),
                juce::Justification::centred);

    // Needle / cents display
    auto needleArea = getLocalBounds().removeFromBottom (getHeight() / 2).toFloat();
    needleArea.reduce (20.0f, 10.0f);

    // Background track
    g.setColour (juce::Colour (0xff333355));
    float trackH = 8.0f;
    float trackY = needleArea.getCentreY() - trackH / 2.0f;
    g.fillRoundedRectangle (needleArea.getX(), trackY,
                            needleArea.getWidth(), trackH, 4.0f);

    // Centre line
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    float centreX = needleArea.getCentreX();
    g.drawLine (centreX, trackY - 6.0f, centreX, trackY + trackH + 6.0f, 1.5f);

    // Needle (maps -50..+50 cents to left..right)
    float frac    = juce::jlimit (-1.0f, 1.0f, currentCents / 50.0f);
    float needleX = centreX + frac * (needleArea.getWidth() * 0.5f);
    if      (absC <= 5.0f)  g.setColour (juce::Colours::limegreen);
    else if (absC <= 15.0f) g.setColour (juce::Colours::yellow);
    else                    g.setColour (juce::Colours::orangered);

    g.fillEllipse (needleX - 7.0f, trackY - 5.0f, 14.0f, trackH + 10.0f);

    // Cents label
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::Font(juce::FontOptions().withHeight(14.0f)));
    juce::String centsStr = (currentCents >= 0.0f ? "+" : "") +
                             juce::String ((int)currentCents) + " cents";
    g.drawText (centsStr, needleArea.translated (0, 20), juce::Justification::centred);
}

void TunerPanel::resized() {}
