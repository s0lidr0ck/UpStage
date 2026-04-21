#include "LevelMeter.h"

LevelMeter::LevelMeter (Orientation o, ColourMode cm) : orientation (o), colourMode (cm)
{
    startTimerHz (30);
}

LevelMeter::~LevelMeter()
{
    stopTimer();
}

//==============================================================================
void LevelMeter::pushLevel (float left, float right)
{
    leftLevel .store (left,  std::memory_order_relaxed);
    rightLevel.store (right < 0.0f ? left : right, std::memory_order_relaxed);
}

void LevelMeter::timerCallback()
{
    float newLeft  = leftLevel .load (std::memory_order_relaxed);
    float newRight = rightLevel.load (std::memory_order_relaxed);

    displayLeft  = juce::jmax (newLeft,  displayLeft  - DECAY_RATE);
    displayRight = juce::jmax (newRight, displayRight - DECAY_RATE);

    if (newLeft > peakLeft || newRight > peakRight)
    {
        peakLeft       = juce::jmax (newLeft, peakLeft);
        peakRight      = juce::jmax (newRight, peakRight);
        peakHoldFrames = PEAK_HOLD_FRAMES;
    }
    else if (peakHoldFrames > 0)
    {
        --peakHoldFrames;
    }
    else
    {
        peakLeft  = juce::jmax (0.0f, peakLeft  - DECAY_RATE * 0.5f);
        peakRight = juce::jmax (0.0f, peakRight - DECAY_RATE * 0.5f);
    }

    repaint();
}

//==============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Beveled bezel around the meter
    g.setColour (juce::Colour (0xff0a0a0a));
    g.fillRoundedRectangle (bounds, 2.0f);

    // Inner bevel highlight
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, 0.5f);

    auto area = bounds.reduced (2.0f);

    bool stereo = (rightLevel.load() >= 0.0f);

    if (stereo)
    {
        auto left  = area.removeFromLeft  (area.getWidth() * 0.5f - 1.0f);
        area.removeFromLeft (2.0f);
        auto right = area;
        paintBar (g, left,  displayLeft,  peakLeft);
        paintBar (g, right, displayRight, peakRight);
    }
    else
    {
        paintBar (g, area, displayLeft, peakLeft);
    }
}

void LevelMeter::paintBar (juce::Graphics& g, juce::Rectangle<float> area,
                            float level, float peak) const
{
    // Deep recessed background
    g.setColour (juce::Colour (0xff080810));
    g.fillRect (area);

    // Inset shadow (top and left darker)
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine (area.getX(), area.getY(), area.getRight(), area.getY(), 1.0f);
    g.drawLine (area.getX(), area.getY(), area.getX(), area.getBottom(), 1.0f);

    float h = area.getHeight();

    float redStart    = 1.0f - (3.0f  / 60.0f);
    float yellowStart = 1.0f - (12.0f / 60.0f);

    float filledH = level * h;

    // Main zone with horizontal gradient for 3D tube effect
    float mainH = juce::jmin (filledH, yellowStart * h);
    if (mainH > 0.0f)
    {
        auto mainRect = juce::Rectangle<float> (area.getX(), area.getBottom() - mainH,
                                                 area.getWidth(), mainH);

        juce::Colour darkCol, brightCol;
        if (colourMode == ColourMode::Amber)
        {
            darkCol   = juce::Colour (0xff6a4a0a);
            brightCol = juce::Colour (0xffeeaa20);
        }
        else
        {
            darkCol   = juce::Colour (0xff0a6a1a);
            brightCol = juce::Colour (0xff30ff50);
        }

        juce::ColourGradient mainGrad (
            darkCol, mainRect.getX(), mainRect.getCentreY(),
            brightCol, mainRect.getCentreX(), mainRect.getCentreY(), false);
        mainGrad.addColour (1.0, darkCol);
        mainGrad.point2 = { mainRect.getRight(), mainRect.getCentreY() };
        g.setGradientFill (mainGrad);
        g.fillRect (mainRect);
    }

    // Yellow zone
    float yellowH = juce::jmax (0.0f, juce::jmin (filledH, redStart * h) - yellowStart * h);
    if (yellowH > 0.0f)
    {
        auto yellowRect = juce::Rectangle<float> (area.getX(),
            area.getBottom() - yellowStart * h - yellowH, area.getWidth(), yellowH);
        juce::ColourGradient yellowGrad (
            juce::Colour (0xffaa8800), yellowRect.getX(), yellowRect.getCentreY(),
            juce::Colour (0xffffee22), yellowRect.getCentreX(), yellowRect.getCentreY(), false);
        yellowGrad.addColour (1.0, juce::Colour (0xffaa8800));
        yellowGrad.point2 = { yellowRect.getRight(), yellowRect.getCentreY() };
        g.setGradientFill (yellowGrad);
        g.fillRect (yellowRect);
    }

    // Red zone
    float redH = juce::jmax (0.0f, filledH - redStart * h);
    if (redH > 0.0f)
    {
        auto redRect = juce::Rectangle<float> (area.getX(),
            area.getBottom() - redStart * h - redH, area.getWidth(), redH);
        juce::ColourGradient redGrad (
            juce::Colour (0xffaa1111), redRect.getX(), redRect.getCentreY(),
            juce::Colour (0xffff4444), redRect.getCentreX(), redRect.getCentreY(), false);
        redGrad.addColour (1.0, juce::Colour (0xffaa1111));
        redGrad.point2 = { redRect.getRight(), redRect.getCentreY() };
        g.setGradientFill (redGrad);
        g.fillRect (redRect);
    }

    // Segment lines for VU look
    g.setColour (juce::Colour (0xff080810).withAlpha (0.4f));
    int segSpacing = juce::jmax (3, (int)(h / 30.0f));
    for (float sy = area.getY(); sy < area.getBottom(); sy += (float)segSpacing)
        g.drawHorizontalLine ((int)sy, area.getX(), area.getRight());

    // Peak hold line with glow
    if (peak > 0.01f)
    {
        float peakY = area.getBottom() - peak * h;
        juce::Colour peakCol = (peak > redStart) ? juce::Colour (0xffff5555)
                             : (peak > yellowStart) ? juce::Colour (0xffffffaa)
                             : juce::Colour (0xff88ff88);

        // Glow
        g.setColour (peakCol.withAlpha (0.15f));
        g.fillRect (area.getX(), peakY - 2.0f, area.getWidth(), 5.0f);

        // Line
        g.setColour (peakCol.withAlpha (0.9f));
        g.drawHorizontalLine (juce::roundToInt (peakY), area.getX(), area.getRight());
    }

    // Highlight on left edge
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawVerticalLine ((int)area.getX() + 1, area.getY(), area.getBottom());
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    peakLeft  = 0.0f;
    peakRight = 0.0f;
    peakHoldFrames = 0;
}

juce::String LevelMeter::getTooltip()
{
    float peakDb = juce::Decibels::gainToDecibels (juce::jmax (peakLeft, peakRight), -80.0f);
    float rmsDb  = juce::Decibels::gainToDecibels (juce::jmax (displayLeft, displayRight), -80.0f);

    if (peakDb <= -80.0f)
        return "Peak: -INF\nRMS: -INF";

    return "Peak: " + juce::String (peakDb, 1) + " dB\nRMS: " + juce::String (rmsDb, 1) + " dB";
}
