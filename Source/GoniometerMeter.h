#pragma once
#include <JuceHeader.h>

class GoniometerMeter : public juce::Component,
                        private juce::Timer
{
public:
    GoniometerMeter()
    {
        sampleBuffer.resize (BUFFER_SIZE * 2, 0.0f);
        polarBins.resize (NUM_BINS, 0.0f);
        peakBins.resize (NUM_BINS, 0.0f);
        startTimerHz (30);
    }

    ~GoniometerMeter() override { stopTimer(); }

    void pushSamples (const float* leftData, const float* rightData, int numSamples)
    {
        juce::ScopedLock sl (bufferLock);
        for (int i = 0; i < numSamples; ++i)
        {
            sampleBuffer[writeIndex * 2]     = leftData[i];
            sampleBuffer[writeIndex * 2 + 1] = rightData[i];
            writeIndex = (writeIndex + 1) % BUFFER_SIZE;
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Housing
        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (bounds, 3.0f);

        // Label
        g.setColour (juce::Colour (0xff999999));
        g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f).withStyle ("Bold")));
        g.drawText ("STEREO FIELD", bounds.removeFromBottom (13.0f), juce::Justification::centred, false);

        auto display = bounds.reduced (4.0f, 3.0f);
        display.removeFromBottom (1.0f);

        // Dark background
        g.setColour (juce::Colour (0xff0a0a14));
        g.fillRoundedRectangle (display.expanded (1.0f), 2.0f);
        g.setColour (juce::Colour (0xff0e0e1e));
        g.fillRoundedRectangle (display, 1.5f);

        g.saveState();
        g.reduceClipRegion (display.toNearestInt());

        float cx = display.getCentreX();
        float maxR = display.getWidth() * 0.46f;
        float cy = display.getY() + maxR + 2.0f;

        // Concentric dB half-circle arcs
        float dbLevels[] = { -6.0f, -12.0f, -24.0f, -48.0f };
        g.setColour (juce::Colour (0xff1a1a3a));
        for (float db : dbLevels)
        {
            float r = maxR * dbToRadius (db);
            juce::Path arc;
            arc.addCentredArc (cx, cy, r, r, 0.0f,
                -juce::MathConstants<float>::pi, 0.0f, true);
            g.strokePath (arc, juce::PathStrokeType (0.5f));
        }

        // Axis lines: center (M), left 45, right 45
        g.setColour (juce::Colour (0xff2a2a4a));
        g.drawLine (cx, cy, cx, cy - maxR, 0.5f);
        float angle45 = juce::MathConstants<float>::pi * 0.25f;
        g.drawLine (cx, cy, cx - std::sin (angle45) * maxR, cy - std::cos (angle45) * maxR, 0.5f);
        g.drawLine (cx, cy, cx + std::sin (angle45) * maxR, cy - std::cos (angle45) * maxR, 0.5f);

        // Anti-phase lines (horizontal)
        g.setColour (juce::Colour (0xff3a1a1a));
        g.drawLine (cx - maxR, cy, cx + maxR, cy, 0.5f);

        // Labels
        g.setFont (juce::Font (juce::FontOptions().withHeight (8.0f)));
        g.setColour (juce::Colour (0xff6688cc));
        g.drawText ("L", (int)(display.getX() + 2), (int)(display.getY()), 12, 10, juce::Justification::centredLeft, false);
        g.drawText ("R", (int)(display.getRight() - 14), (int)(display.getY()), 12, 10, juce::Justification::centredRight, false);

        // Draw filled energy shape
        {
            juce::Path fillPath;
            bool started = false;

            for (int i = 0; i < NUM_BINS; ++i)
            {
                float angle = binToAngle (i);
                float r = juce::jmax (1.0f, displayBins[i] * maxR);

                float px = cx + std::sin (angle) * r;
                float py = cy - std::cos (angle) * r;

                if (!started) { fillPath.startNewSubPath (px, py); started = true; }
                else fillPath.lineTo (px, py);
            }
            fillPath.lineTo (cx, cy);
            fillPath.closeSubPath();

            g.setColour (juce::Colour (0x25667799));
            g.fillPath (fillPath);
        }

        // Draw peak outline trace
        {
            juce::Path tracePath;
            bool started = false;

            for (int i = 0; i < NUM_BINS; ++i)
            {
                float angle = binToAngle (i);
                float r = juce::jmax (1.0f, displayPeakBins[i] * maxR);

                float px = cx + std::sin (angle) * r;
                float py = cy - std::cos (angle) * r;

                if (!started) { tracePath.startNewSubPath (px, py); started = true; }
                else tracePath.lineTo (px, py);
            }

            g.setColour (juce::Colour (0xffee8833));
            g.strokePath (tracePath, juce::PathStrokeType (1.2f));
        }

        g.restoreState();

        // Glass
        {
            juce::ColourGradient glass (
                juce::Colour (0x10ffffff), cx, display.getY(),
                juce::Colour (0x00ffffff), cx, display.getY() + display.getHeight() * 0.35f, false);
            g.setGradientFill (glass);
            g.fillRoundedRectangle (display, 1.5f);
        }

        g.setColour (juce::Colour (0xff333333));
        g.drawRoundedRectangle (display, 1.5f, 1.0f);
    }

    void resized() override { repaint(); }
    static int preferredHeight() { return 72; }

private:
    static constexpr int BUFFER_SIZE = 4096;
    static constexpr int NUM_BINS = 91;
    static constexpr float PEAK_DECAY = 0.97f;

    juce::CriticalSection bufferLock;
    std::vector<float> sampleBuffer;
    int writeIndex = 0;

    std::vector<float> polarBins;
    std::vector<float> peakBins;
    std::vector<float> displayBins { std::vector<float> (NUM_BINS, 0.0f) };
    std::vector<float> displayPeakBins { std::vector<float> (NUM_BINS, 0.0f) };

    void timerCallback() override
    {
        computePolarDistribution();

        for (int i = 0; i < NUM_BINS; ++i)
        {
            displayBins[i] += (polarBins[i] - displayBins[i]) * 0.3f;

            if (polarBins[i] > peakBins[i])
                peakBins[i] = polarBins[i];
            else
                peakBins[i] *= PEAK_DECAY;

            displayPeakBins[i] += (peakBins[i] - displayPeakBins[i]) * 0.2f;
        }

        repaint();
    }

    void computePolarDistribution()
    {
        std::fill (polarBins.begin(), polarBins.end(), 0.0f);

        juce::ScopedLock sl (bufferLock);

        int analysisSize = juce::jmin (512, BUFFER_SIZE);
        int startIdx = (writeIndex - analysisSize + BUFFER_SIZE) % BUFFER_SIZE;

        for (int i = 0; i < analysisSize; ++i)
        {
            int idx = (startIdx + i) % BUFFER_SIZE;
            float l = sampleBuffer[idx * 2];
            float r = sampleBuffer[idx * 2 + 1];

            float mid  = (l + r) * 0.5f;
            float side = (l - r) * 0.5f;

            float magnitude = std::sqrt (mid * mid + side * side);
            if (magnitude < 1e-6f) continue;

            float angle = std::atan2 (side, mid);
            float normAngle = (angle + juce::MathConstants<float>::halfPi) / juce::MathConstants<float>::pi;
            normAngle = juce::jlimit (0.0f, 1.0f, normAngle);

            int bin = juce::jlimit (0, NUM_BINS - 1, (int)(normAngle * (NUM_BINS - 1)));

            float dbMag = dbToRadius (juce::Decibels::gainToDecibels (magnitude, -60.0f));
            polarBins[bin] = juce::jmax (polarBins[bin], dbMag);
        }
    }

    static float dbToRadius (float db)
    {
        db = juce::jlimit (-60.0f, 0.0f, db);
        return (db + 60.0f) / 60.0f;
    }

    static float binToAngle (int bin)
    {
        float norm = (float) bin / (float)(NUM_BINS - 1);
        return (norm - 0.5f) * juce::MathConstants<float>::pi;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoniometerMeter)
};
