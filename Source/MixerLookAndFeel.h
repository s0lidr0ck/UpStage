#pragma once
#include <JuceHeader.h>

class MixerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MixerLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff252525));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3a3a));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5a9e5a));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffaaaaaa));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        setColour (juce::Slider::backgroundColourId, juce::Colour (0xff1a1a1a));
        setColour (juce::Slider::trackColourId, juce::Colour (0xff4a90e2));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xffe0e0e0));

        setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    }

    ~MixerLookAndFeel() override = default;

    //==========================================================================
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                          bool, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (2.0f, 1.0f);
        auto toggleState = button.getToggleState();

        g.setColour (toggleState ? juce::Colour (0xff3a6040) : juce::Colour (0xff2a2a2a));
        g.fillRoundedRectangle (bounds, 3.0f);

        g.setColour (toggleState ? juce::Colour (0xff4a8050) : juce::Colour (0xff1a1a1a));
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        auto checkArea = bounds.removeFromLeft (bounds.getHeight()).reduced (4.0f);
        if (toggleState)
        {
            g.setColour (juce::Colour (0xff88ff88));
            g.fillEllipse (checkArea);
        }
        else
        {
            g.setColour (juce::Colour (0xff555555));
            g.drawEllipse (checkArea, 1.5f);
        }
    }

    //==========================================================================
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            float cx = x + width * 0.5f;
            float margin = 12.0f;
            float trackTop = (float)y + margin;
            float trackBottom = (float)(y + height) - margin;
            float trackH = trackBottom - trackTop;

            // ---- Recessed slot ----
            float slotWidth = 6.0f;
            float slotX = cx - slotWidth * 0.5f;

            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (slotX - 1.0f, trackTop - 2.0f, slotWidth + 2.0f, trackH + 4.0f, 2.0f);

            g.setColour (juce::Colour (0xff0a0a0a));
            g.fillRoundedRectangle (slotX, trackTop, slotWidth, trackH, 1.5f);

            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.drawVerticalLine ((int)(slotX + slotWidth - 1), trackTop, trackBottom);

            // ---- dB scale with labels ----
            auto& range = slider.getNormalisableRange();

            struct TickMark { float db; const char* label; };
            TickMark ticks[] = {
                { 10.0f, "+10" }, { 5.0f, "+5" }, { 0.0f, "0" },
                { -5.0f, "-5" }, { -10.0f, "-10" }, { -15.0f, "-15" },
                { -20.0f, "-20" }, { -30.0f, "-30" }, { -40.0f, "-40" },
                { -50.0f, "-50" }
            };

            g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));

            for (auto& tick : ticks)
            {
                if (tick.db < range.start || tick.db > range.end) continue;

                double proportion = range.convertTo0to1 (tick.db);
                float tickY = trackTop + trackH * (1.0f - (float)proportion);

                // Tick lines on both sides
                g.setColour (juce::Colour (0xff555555));
                g.drawHorizontalLine ((int)tickY, slotX - 7.0f, slotX - 2.0f);
                g.drawHorizontalLine ((int)tickY, slotX + slotWidth + 2.0f, slotX + slotWidth + 7.0f);

                // dB label on right side
                g.setColour (juce::Colour (0xff777777));
                g.drawText (tick.label,
                    (int)(slotX + slotWidth + 8.0f), (int)(tickY - 5.0f),
                    (int)((float)(x + width) - slotX - slotWidth - 8.0f), 11,
                    juce::Justification::centredLeft, false);
            }

            // ---- Fader thumb / cap ----
            float thumbWidth = juce::jmin ((float)width * 0.6f, 42.0f);
            float thumbHeight = 72.0f;
            float thumbX = cx - thumbWidth * 0.5f;
            // Clamp the thumb's centre line to the visible scale extremes (+10
            // and -50) so it can't ride above/below the printed scale, even
            // though the slider's value range extends further (-60..+12).
            float topLimitY    = trackTop + trackH * (1.0f - (float) range.convertTo0to1 ( 10.0));
            float bottomLimitY = trackTop + trackH * (1.0f - (float) range.convertTo0to1 (-50.0));
            float thumbCentreY = juce::jlimit (topLimitY, bottomLimitY, sliderPos);
            float thumbY = thumbCentreY - thumbHeight * 0.5f;

            juce::Colour faderCol = getFaderColour (slider);

            // ---- Multi-layer drop shadow ----
            for (int s = 6; s >= 0; --s)
            {
                float offX = (float)s * 0.5f;
                float offY = (float)s * 0.8f;
                float expand = (float)s * 0.3f;
                float alpha = 0.10f - (float)s * 0.012f;
                g.setColour (juce::Colours::black.withAlpha (alpha));
                g.fillRoundedRectangle (thumbX + offX - expand * 0.5f,
                                        thumbY + offY,
                                        thumbWidth + expand,
                                        thumbHeight + expand * 0.5f, 5.0f);
            }

            // ---- Main body: top-to-bottom metallic gradient ----
            {
                juce::ColourGradient bodyGrad (
                    faderCol.brighter (0.6f), thumbX, thumbY,
                    faderCol.darker (0.35f),  thumbX, thumbY + thumbHeight, false);
                bodyGrad.addColour (0.15, faderCol.brighter (0.4f));
                bodyGrad.addColour (0.48, faderCol.brighter (0.1f));
                bodyGrad.addColour (0.52, faderCol.darker (0.15f));
                bodyGrad.addColour (0.85, faderCol.darker (0.25f));
                g.setGradientFill (bodyGrad);
                g.fillRoundedRectangle (thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);
            }

            // ---- Left-to-right curvature (cylindrical highlight) ----
            {
                juce::ColourGradient sideGrad (
                    juce::Colours::white.withAlpha (0.18f), thumbX, thumbY,
                    juce::Colours::transparentWhite, thumbX + thumbWidth * 0.35f, thumbY, false);
                g.setGradientFill (sideGrad);
                g.fillRoundedRectangle (thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);

                juce::ColourGradient rightShadow (
                    juce::Colours::transparentBlack, thumbX + thumbWidth * 0.7f, thumbY,
                    juce::Colours::black.withAlpha (0.15f), thumbX + thumbWidth, thumbY, false);
                g.setGradientFill (rightShadow);
                g.fillRoundedRectangle (thumbX, thumbY, thumbWidth, thumbHeight, 4.0f);
            }

            // ---- Top edge highlight (sharp specular reflection) ----
            {
                juce::ColourGradient topGlow (
                    juce::Colours::white.withAlpha (0.4f), thumbX + thumbWidth * 0.5f, thumbY + 1.0f,
                    juce::Colours::transparentWhite, thumbX + thumbWidth * 0.5f, thumbY + thumbHeight * 0.15f, false);
                g.setGradientFill (topGlow);
                g.fillRoundedRectangle (thumbX + 3.0f, thumbY + 1.0f,
                                       thumbWidth - 6.0f, thumbHeight * 0.15f, 3.0f);
            }

            // ---- Bottom edge shadow (undercut) ----
            {
                juce::ColourGradient bottomShadow (
                    juce::Colours::transparentBlack, thumbX, thumbY + thumbHeight * 0.88f,
                    juce::Colours::black.withAlpha (0.2f), thumbX, thumbY + thumbHeight, false);
                g.setGradientFill (bottomShadow);
                g.fillRoundedRectangle (thumbX, thumbY + thumbHeight * 0.85f,
                                       thumbWidth, thumbHeight * 0.15f, 3.0f);
            }

            // ---- Bevel edge: light top/left, dark bottom/right ----
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int)(thumbY + 1), thumbX + 4.0f, thumbX + thumbWidth - 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.drawHorizontalLine ((int)(thumbY + thumbHeight - 2), thumbX + 4.0f, thumbX + thumbWidth - 4.0f);

            // Center line (black indicator)
            float centerY = thumbY + thumbHeight * 0.5f;
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRect (thumbX + 4.0f, centerY - 1.0f, thumbWidth - 8.0f, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRect (thumbX + 4.0f, centerY + 1.0f, thumbWidth - 8.0f, 1.0f);

            // Grip grooves (above and below center line)
            for (int side = 0; side < 2; ++side)
            {
                float baseY = (side == 0) ? centerY - 8.0f : centerY + 6.0f;
                float dir = (side == 0) ? -1.0f : 1.0f;
                for (int gi = 0; gi < 3; ++gi)
                {
                    float gy = baseY + dir * gi * 5.0f;
                    g.setColour (juce::Colours::black.withAlpha (0.18f));
                    g.drawHorizontalLine ((int)gy, thumbX + 8.0f, thumbX + thumbWidth - 8.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.06f));
                    g.drawHorizontalLine ((int)gy + 1, thumbX + 8.0f, thumbX + thumbWidth - 8.0f);
                }
            }

            // Border
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (thumbX, thumbY, thumbWidth, thumbHeight, 4.0f, 1.0f);
        }
        else
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                    minSliderPos, maxSliderPos, style, slider);
        }
    }

    //==========================================================================
    // Skeuomorphic rotary knob - metallic with bevel, shadow, and pointer
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> (x, y, width, height);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.40f;
        auto centreX = bounds.getCentreX();
        auto centreY = bounds.getCentreY();
        auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // ---- Drop shadow ----
        for (int i = 4; i >= 0; --i)
        {
            float off = (float)i * 0.6f;
            float alpha = 0.12f - (float)i * 0.02f;
            g.setColour (juce::Colours::black.withAlpha (alpha));
            g.fillEllipse (centreX - radius - 2.0f + off,
                           centreY - radius - 2.0f + off * 1.5f,
                           (radius + 2.0f) * 2.0f,
                           (radius + 2.0f) * 2.0f);
        }

        // ---- Outer ring / bezel ----
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillEllipse (centreX - radius - 2.0f, centreY - radius - 2.0f,
                       (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);

        // ---- Arc track (background) ----
        {
            juce::Path arcBg;
            arcBg.addCentredArc (centreX, centreY, radius + 1.0f, radius + 1.0f,
                                 0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (juce::Colour (0xff333333));
            g.strokePath (arcBg, juce::PathStrokeType (3.0f));
        }

        // ---- Arc track (value fill) ----
        {
            auto accentColour = getKnobAccentColour (slider);
            bool bipolar = slider.getComponentID().contains ("pan");

            if (bipolar)
            {
                float midAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
                float startArc = juce::jmin (midAngle, angle);
                float endArc   = juce::jmax (midAngle, angle);
                if (endArc - startArc > 0.01f)
                {
                    juce::Path arcFill;
                    arcFill.addCentredArc (centreX, centreY, radius + 1.0f, radius + 1.0f,
                                           0.0f, startArc, endArc, true);
                    g.setColour (accentColour);
                    g.strokePath (arcFill, juce::PathStrokeType (3.0f));
                }
            }
            else if (sliderPosProportional > 0.01f)
            {
                juce::Path arcFill;
                arcFill.addCentredArc (centreX, centreY, radius + 1.0f, radius + 1.0f,
                                       0.0f, rotaryStartAngle, angle, true);
                g.setColour (accentColour);
                g.strokePath (arcFill, juce::PathStrokeType (3.0f));
            }

            // Center-detent tick at 12 o'clock marks a bipolar (pan) control, so
            // it reads as a pan knob even when centred (no arc drawn).
            if (bipolar)
            {
                float midAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
                juce::Point<float> outer (centreX + (radius + 3.0f) * std::sin (midAngle),
                                          centreY - (radius + 3.0f) * std::cos (midAngle));
                juce::Point<float> inner (centreX + (radius - 1.0f) * std::sin (midAngle),
                                          centreY - (radius - 1.0f) * std::cos (midAngle));
                g.setColour (accentColour.brighter (0.3f));
                g.drawLine ({ inner, outer }, 2.0f);
            }
        }

        // ---- Knob body - metallic gradient tinted with accent colour ----
        {
            auto accent = getKnobAccentColour (slider);
            auto bodyLight = juce::Colour (0xff707070).interpolatedWith (accent, 0.2f);
            auto bodyMid   = juce::Colour (0xff5a5a5a).interpolatedWith (accent, 0.15f);
            auto bodyDark  = juce::Colour (0xff3a3a3a).interpolatedWith (accent, 0.1f);

            juce::ColourGradient metalGrad (
                bodyLight, centreX, centreY - radius,
                bodyDark,  centreX, centreY + radius, false);
            metalGrad.addColour (0.4, bodyMid);
            g.setGradientFill (metalGrad);
            g.fillEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
        }

        // ---- Top highlight (convex illusion) ----
        {
            auto hlRadius = radius * 0.75f;
            juce::ColourGradient hlGrad (
                juce::Colours::white.withAlpha (0.22f), centreX - radius * 0.15f, centreY - radius * 0.4f,
                juce::Colours::transparentWhite, centreX, centreY + radius * 0.2f, true);
            g.setGradientFill (hlGrad);
            g.fillEllipse (centreX - hlRadius, centreY - hlRadius, hlRadius * 2.0f, hlRadius * 2.0f);
        }

        // ---- Subtle inner ring ----
        g.setColour (juce::Colour (0xff2a2a2a));
        g.drawEllipse (centreX - radius + 1.0f, centreY - radius + 1.0f,
                       (radius - 1.0f) * 2.0f, (radius - 1.0f) * 2.0f, 0.8f);

        // ---- Pointer / indicator line ----
        {
            auto pointerThickness = 2.5f;

            juce::Path pointer;
            pointer.addRoundedRectangle (-pointerThickness * 0.5f, -radius + 2.0f,
                                         pointerThickness, radius - 2.0f, 1.0f);

            auto transform = juce::AffineTransform::rotation (angle)
                                .translated (centreX, centreY);

            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillPath (pointer, transform);
        }
    }

    //==========================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const float r = 2.5f;
        bool isDown = shouldDrawButtonAsDown || button.getToggleState();

        auto baseColour = backgroundColour;
        if (isDown)
            baseColour = button.findColour (juce::TextButton::buttonOnColourId);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter (0.06f);

        // ---- Recessed cavity behind button ----
        {
            auto cavity = bounds.expanded (0.5f);
            // Dark pit
            g.setColour (juce::Colour (0xff0a0a0a));
            g.fillRoundedRectangle (cavity, r + 1.0f);
            // Inner shadow at top of cavity (light comes from above)
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawHorizontalLine ((int) cavity.getY(), cavity.getX() + 2.0f, cavity.getRight() - 2.0f);
            g.drawHorizontalLine ((int) cavity.getY() + 1, cavity.getX() + 2.0f, cavity.getRight() - 2.0f);
            // Faint light edge at bottom of cavity
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.drawHorizontalLine ((int) cavity.getBottom() - 1, cavity.getX() + 2.0f, cavity.getRight() - 2.0f);
        }

        // ---- Button face: raised cap sitting in the cavity ----
        auto faceRect = isDown ? bounds.reduced (1.5f).translated (0.0f, 1.0f)
                               : bounds.reduced (1.5f);

        // Multi-stop gradient for plastic/rubber surface
        {
            juce::ColourGradient face (
                baseColour.brighter (0.35f), faceRect.getCentreX(), faceRect.getY(),
                baseColour.darker (0.4f),    faceRect.getCentreX(), faceRect.getBottom(), false);
            face.addColour (0.15, baseColour.brighter (0.15f));
            face.addColour (0.5,  baseColour);
            face.addColour (0.85, baseColour.darker (0.25f));
            g.setGradientFill (face);
            g.fillRoundedRectangle (faceRect, r);
        }

        // ---- Surface texture: fine horizontal grain ----
        {
            juce::Random rng ((juce::int64)(bounds.getX() * 100 + bounds.getY() * 37));
            for (int ty = (int) faceRect.getY() + 1; ty < (int) faceRect.getBottom() - 1; ty += 2)
            {
                float alpha = rng.nextFloat() * 0.04f;
                g.setColour (((ty - (int) faceRect.getY()) % 4 < 2)
                    ? juce::Colours::white.withAlpha (alpha)
                    : juce::Colours::black.withAlpha (alpha));
                g.drawHorizontalLine (ty, faceRect.getX() + 2.0f, faceRect.getRight() - 2.0f);
            }
        }

        // ---- Top bevel highlight (bright edge) ----
        if (! isDown)
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawHorizontalLine ((int)(faceRect.getY() + 1.0f),
                                  faceRect.getX() + 3.0f, faceRect.getRight() - 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawHorizontalLine ((int)(faceRect.getY() + 2.0f),
                                  faceRect.getX() + 3.0f, faceRect.getRight() - 3.0f);
        }

        // ---- Left edge highlight ----
        if (! isDown)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawVerticalLine ((int)(faceRect.getX() + 1.0f),
                                faceRect.getY() + 3.0f, faceRect.getBottom() - 3.0f);
        }

        // ---- Bottom bevel shadow ----
        g.setColour (juce::Colours::black.withAlpha (isDown ? 0.15f : 0.35f));
        g.drawHorizontalLine ((int)(faceRect.getBottom() - 1.5f),
                              faceRect.getX() + 3.0f, faceRect.getRight() - 3.0f);

        // ---- Right edge shadow ----
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.drawVerticalLine ((int)(faceRect.getRight() - 1.5f),
                            faceRect.getY() + 3.0f, faceRect.getBottom() - 3.0f);

        // ---- Outer border of face ----
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (faceRect, r, 0.8f);

        // ---- Subtle colour glow when active ----
        if (isDown)
        {
            auto glowColour = button.findColour (juce::TextButton::buttonOnColourId);
            if (glowColour.getBrightness() > 0.15f)
            {
                g.setColour (glowColour.withAlpha (0.08f));
                g.fillRoundedRectangle (faceRect.reduced (2.0f), r);
                g.setColour (glowColour.brighter (0.4f).withAlpha (0.12f));
                g.fillRoundedRectangle (faceRect.reduced (4.0f).removeFromTop (faceRect.getHeight() * 0.3f), r);
            }
        }
    }

    //==========================================================================
    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool /*highlighted*/, bool /*down*/) override
    {
        auto id = button.getComponentID();
        if (id.startsWith ("icon_") && ! showButtonLabels)
        {
            auto textColour = button.findColour (button.getToggleState()
                ? juce::TextButton::textColourOnId
                : juce::TextButton::textColourOffId);
            g.setColour (textColour);

            auto area = button.getLocalBounds().toFloat().reduced (3.0f);
            float cx = area.getCentreX(), cy = area.getCentreY();
            float sz = juce::jmin (area.getWidth(), area.getHeight()) * 0.35f;

            if (id == "icon_tuner")
            {
                // Tuning fork shape
                g.drawLine (cx, cy - sz, cx, cy + sz, 1.5f);
                g.drawLine (cx - sz * 0.4f, cy - sz, cx - sz * 0.4f, cy - sz * 0.1f, 1.5f);
                g.drawLine (cx + sz * 0.4f, cy - sz, cx + sz * 0.4f, cy - sz * 0.1f, 1.5f);
                g.drawLine (cx - sz * 0.4f, cy - sz * 0.1f, cx + sz * 0.4f, cy - sz * 0.1f, 1.5f);
            }
            else if (id == "icon_panic")
            {
                // Exclamation mark in triangle
                juce::Path tri;
                tri.addTriangle (cx, cy - sz * 1.0f, cx - sz * 0.9f, cy + sz * 0.8f, cx + sz * 0.9f, cy + sz * 0.8f);
                g.strokePath (tri, juce::PathStrokeType (1.3f));
                g.drawLine (cx, cy - sz * 0.35f, cx, cy + sz * 0.2f, 1.5f);
                g.fillEllipse (cx - 1.2f, cy + sz * 0.4f, 2.4f, 2.4f);
            }
            else if (id == "icon_rec")
            {
                // Filled circle
                g.fillEllipse (cx - sz * 0.55f, cy - sz * 0.55f, sz * 1.1f, sz * 1.1f);
            }
            else if (id == "icon_play")
            {
                // Right-pointing triangle
                juce::Path tri;
                tri.addTriangle (cx - sz * 0.5f, cy - sz * 0.65f,
                                 cx - sz * 0.5f, cy + sz * 0.65f,
                                 cx + sz * 0.65f, cy);
                g.fillPath (tri);
            }
            else if (id == "icon_stop")
            {
                // Square
                g.fillRect (cx - sz * 0.45f, cy - sz * 0.45f, sz * 0.9f, sz * 0.9f);
            }
            else if (id == "icon_metro")
            {
                // Metronome: triangle body + pendulum line
                juce::Path body;
                body.addTriangle (cx, cy - sz * 0.9f, cx - sz * 0.6f, cy + sz * 0.8f, cx + sz * 0.6f, cy + sz * 0.8f);
                g.strokePath (body, juce::PathStrokeType (1.3f));
                g.drawLine (cx, cy + sz * 0.5f, cx + sz * 0.4f, cy - sz * 0.6f, 1.5f);
            }
            else if (id == "icon_loop")
            {
                // Circular arrows (loop symbol)
                juce::Path arc;
                arc.addArc (cx - sz * 0.6f, cy - sz * 0.5f, sz * 1.2f, sz * 1.0f,
                            -0.3f, juce::MathConstants<float>::pi + 0.3f, true);
                g.strokePath (arc, juce::PathStrokeType (1.5f));
                juce::Path arc2;
                arc2.addArc (cx - sz * 0.6f, cy - sz * 0.5f, sz * 1.2f, sz * 1.0f,
                             juce::MathConstants<float>::pi - 0.3f,
                             juce::MathConstants<float>::twoPi + 0.3f, true);
                g.strokePath (arc2, juce::PathStrokeType (1.5f));
                // Arrow tips
                float ax1 = cx + sz * 0.55f, ay1 = cy - sz * 0.35f;
                g.drawLine (ax1, ay1, ax1 - 3.0f, ay1 - 3.0f, 1.3f);
                g.drawLine (ax1, ay1, ax1 + 3.0f, ay1 - 3.0f, 1.3f);
                float ax2 = cx - sz * 0.55f, ay2 = cy + sz * 0.35f;
                g.drawLine (ax2, ay2, ax2 - 3.0f, ay2 + 3.0f, 1.3f);
                g.drawLine (ax2, ay2, ax2 + 3.0f, ay2 + 3.0f, 1.3f);
            }
            else if (id == "icon_tap")
            {
                // Hand/finger tap: downward pointing finger
                g.drawLine (cx, cy - sz * 0.7f, cx, cy + sz * 0.3f, 2.0f);
                g.fillEllipse (cx - 2.5f, cy + sz * 0.3f, 5.0f, 5.0f);
                // Ripple arc below
                juce::Path ripple;
                ripple.addArc (cx - sz * 0.5f, cy + sz * 0.1f, sz, sz * 0.7f,
                               0.5f, juce::MathConstants<float>::pi - 0.5f, true);
                g.strokePath (ripple, juce::PathStrokeType (1.0f));
            }
            else if (id == "icon_expand")
            {
                // Three horizontal bars (hamburger)
                float bw = sz * 0.7f;
                for (int i = -1; i <= 1; ++i)
                    g.fillRect (cx - bw, cy + i * sz * 0.5f - 0.75f, bw * 2.0f, 1.5f);
            }
            else
            {
                // Fallback: draw text
                g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
                g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (2),
                                  juce::Justification::centred, 1);
            }
        }
        else
        {
            auto textColour = button.findColour (button.getToggleState()
                ? juce::TextButton::textColourOnId
                : juce::TextButton::textColourOffId);
            g.setColour (textColour);
            g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (2),
                              juce::Justification::centred, 1);
        }
    }

    //==========================================================================
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        auto bounds = label.getLocalBounds().toFloat();
        auto bgColour = label.findColour (juce::Label::backgroundColourId);

        if (bgColour.getAlpha() > 0)
        {
            const float r = 2.0f;

            // Recessed cavity
            g.setColour (juce::Colour (0xff0c0c0c));
            g.fillRoundedRectangle (bounds, r + 0.5f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawHorizontalLine ((int) bounds.getY(), bounds.getX() + 2.0f, bounds.getRight() - 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.drawHorizontalLine ((int) bounds.getBottom() - 1, bounds.getX() + 2.0f, bounds.getRight() - 2.0f);

            // Raised face
            auto face = bounds.reduced (1.0f);
            juce::ColourGradient grad (
                bgColour.brighter (0.2f), face.getCentreX(), face.getY(),
                bgColour.darker (0.2f),   face.getCentreX(), face.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (face, r);

            // Top highlight
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawHorizontalLine ((int)(face.getY() + 1.0f), face.getX() + 2.0f, face.getRight() - 2.0f);

            // Bottom shadow
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.drawHorizontalLine ((int)(face.getBottom() - 1.0f), face.getX() + 2.0f, face.getRight() - 2.0f);

            // Outline
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (face, r, 0.6f);
        }

        // Draw text
        auto textArea = label.getLocalBounds().reduced (4, 0);
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont (label.getFont());
        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          juce::jmax (1, (int)(textArea.getHeight() / label.getFont().getHeight())),
                          label.getMinimumHorizontalScale());
    }

    //==========================================================================
    void setKnobColorMap (const std::map<juce::String, juce::String>* map)
    {
        knobColorMapPtr = map;
    }

    bool showButtonLabels = false;

private:
    const std::map<juce::String, juce::String>* knobColorMapPtr = nullptr;

    juce::Colour getFaderColour (juce::Slider& slider)
    {
        auto compID = slider.getComponentID();
        if (knobColorMapPtr != nullptr && ! compID.isEmpty())
        {
            auto it = knobColorMapPtr->find (compID);
            if (it != knobColorMapPtr->end())
                return faderColourForName (it->second);
        }
        return juce::Colour (0xffc8c8c8); // default silver
    }

    static juce::Colour faderColourForName (const juce::String& color)
    {
        if (color == "red")        return juce::Colour (0xffcc2222);
        if (color == "blue")       return juce::Colour (0xff3366bb);
        if (color == "green")      return juce::Colour (0xff33aa44);
        if (color == "purple")     return juce::Colour (0xff8844bb);
        if (color == "teal")       return juce::Colour (0xff33aaaa);
        if (color == "royal_blue") return juce::Colour (0xff2244cc);
        return juce::Colour (0xffc8c8c8); // grey/silver default
    }

    juce::Colour getKnobAccentColour (juce::Slider& slider)
    {
        // Check custom color map first
        auto compID = slider.getComponentID();
        if (knobColorMapPtr != nullptr && !compID.isEmpty())
        {
            auto it = knobColorMapPtr->find (compID);
            if (it != knobColorMapPtr->end())
                return colourForName (it->second);
        }

        // Pan knobs get a distinct amber accent so they read differently from
        // the blue IN-trim knobs at a glance (they share the "ch" name prefix).
        if (compID.contains ("pan") || slider.getName().contains ("pan"))
            return juce::Colour (0xffe2a04a); // amber

        // Default by name
        auto name = slider.getName();
        if (name.contains ("input") || name.contains ("Input"))
            return juce::Colour (0xff33ccaa); // teal
        if (name.contains ("send") || name.contains ("Send") || name.contains ("master"))
            return juce::Colour (0xffaa55cc); // purple
        if (name.contains ("ch"))
            return juce::Colour (0xff4a90e2); // blue

        return juce::Colour (0xff4a90e2); // default blue
    }

    static juce::Colour colourForName (const juce::String& color)
    {
        if (color == "blue")       return juce::Colour (0xff4a90e2);
        if (color == "green")      return juce::Colour (0xff44bb44);
        if (color == "teal")       return juce::Colour (0xff33ccaa);
        if (color == "purple")     return juce::Colour (0xffaa55cc);
        if (color == "red")        return juce::Colour (0xffcc4444);
        if (color == "royal_blue") return juce::Colour (0xff4466dd);
        return juce::Colour (0xff888888); // grey
    }
};
