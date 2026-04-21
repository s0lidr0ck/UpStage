#pragma once
#include <JuceHeader.h>

/**
 * SignalChainView  v0.4
 *
 * A thin horizontal strip that visualises the UpStage signal path:
 *
 *   [Guitar/Loop] ──► [Trim] ──► [Gate] ──► [ChN] ──► [FX Bus] ──► [Out]
 *
 * Each block is clickable and fires onBlockClicked with a BlockID so that
 * MainComponent can open the relevant panel/settings.
 *
 * The active channel block is highlighted with the channel accent colour.
 * Blocks that are bypassed or disabled are drawn dim with a diagonal slash.
 */
class SignalChainView : public juce::Component
{
public:
    enum class BlockID
    {
        Input = 0,
        Trim,
        Gate,
        Channel,
        FxBus,
        Output
    };

    std::function<void (BlockID)> onBlockClicked;

    SignalChainView()
    {
        setInterceptsMouseClicks (true, false);
    }

    //==========================================================================
    void setChannelNumber   (int ch)                  { channelNumber = ch;         repaint(); }
    void setChannelColour   (juce::Colour c)          { channelColour = c;          repaint(); }
    void setGateEnabled     (bool enabled)            { gateEnabled = enabled;      repaint(); }
    void setFxBusBypassed   (bool bypassed)           { fxBusBypassed = bypassed;   repaint(); }
    void setInputMode       (const juce::String& mode){ inputMode = mode;           repaint(); }
    void setTrimDb          (float db)                { trimDb = db;                repaint(); }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (4.0f, 4.0f);

        // Background
        g.setColour (juce::Colour (0xff16162a));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (juce::Colour (0xff383860));
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

        // Build block list
        struct Block { juce::String label; juce::String sub; juce::Colour col; bool dim; BlockID id; };
        juce::Array<Block> blocks;

        blocks.add ({ "Input",  inputMode,  juce::Colour (0xff2a2a4a), false, BlockID::Input   });
        blocks.add ({ "Trim",   juce::String ((int) trimDb) + " dB",
                                             juce::Colour (0xff2a2a4a),
                                             trimDb == 0.0f, BlockID::Trim    });
        blocks.add ({ "Gate",   gateEnabled ? "ON" : "OFF",
                                             juce::Colour (0xff2a2a4a),
                                             ! gateEnabled, BlockID::Gate    });
        blocks.add ({ "Ch " + juce::String (channelNumber + 1), "Active",
                                             channelColour.withAlpha (0.6f),
                                             false, BlockID::Channel });
        blocks.add ({ "FX Bus", fxBusBypassed ? "BYP" : "ON",
                                             juce::Colour (0xff2a4a2a),
                                             fxBusBypassed, BlockID::FxBus  });
        blocks.add ({ "Output", "",          juce::Colour (0xff2a2a4a), false, BlockID::Output  });

        const float connW  = 18.0f;
        const float totalConn = connW * (blocks.size() - 1);
        const float blockW = (area.getWidth() - totalConn) / blocks.size();
        const float blockH = area.getHeight() - 8.0f;
        float cx = area.getX();

        for (int i = 0; i < blocks.size(); ++i)
        {
            const auto& blk = blocks[i];
            auto bRect = juce::Rectangle<float> (cx, area.getCentreY() - blockH * 0.5f, blockW, blockH);
            blockRects[i] = bRect;

            // Block fill
            g.setColour (blk.col.withAlpha (blk.dim ? 0.35f : 1.0f));
            g.fillRoundedRectangle (bRect, 4.0f);

            // Edge
            g.setColour (juce::Colour (0xff555577).withAlpha (blk.dim ? 0.3f : 0.8f));
            g.drawRoundedRectangle (bRect.reduced (0.5f), 4.0f, 1.0f);

            // Dim slash
            if (blk.dim)
            {
                g.setColour (juce::Colour (0x33ff4444));
                g.drawLine (bRect.getX() + 4.0f, bRect.getBottom() - 4.0f,
                            bRect.getRight() - 4.0f, bRect.getY() + 4.0f, 1.5f);
            }

            // Label
            g.setColour (blk.dim ? juce::Colour (0xff666677) : juce::Colours::white);
            g.setFont (juce::Font(juce::FontOptions().withHeight(11.0f).withStyle("Bold")));
            g.drawText (blk.label, bRect.reduced (2.0f, 2.0f).removeFromTop (blockH * 0.55f),
                        juce::Justification::centred, true);

            if (blk.sub.isNotEmpty())
            {
                g.setColour (blk.dim ? juce::Colour (0xff444455) : juce::Colour (0xffaaaacc));
                g.setFont (juce::Font(juce::FontOptions().withHeight(9.5f)));
                g.drawText (blk.sub, bRect.reduced (2.0f, 2.0f).removeFromBottom (blockH * 0.4f),
                            juce::Justification::centred, true);
            }

            cx += blockW;

            // Arrow connector
            if (i < blocks.size() - 1)
            {
                const float arrowY = area.getCentreY();
                const float ax1    = cx + 2.0f;
                const float ax2    = cx + connW - 2.0f;
                g.setColour (juce::Colour (0xff4444aa));
                g.drawLine (ax1, arrowY, ax2 - 5.0f, arrowY, 1.5f);

                juce::Path arrow;
                arrow.addTriangle (ax2, arrowY, ax2 - 5.0f, arrowY - 3.5f,
                                               ax2 - 5.0f, arrowY + 3.5f);
                g.fillPath (arrow);

                cx += connW;
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 6; ++i)
        {
            if (blockRects[i].contains (e.position))
            {
                if (onBlockClicked)
                    onBlockClicked (static_cast<BlockID> (i));
                return;
            }
        }
    }

    void resized() override { repaint(); }

    static int preferredHeight() { return 48; }

private:
    int          channelNumber  = 0;
    juce::Colour channelColour  { 0xff4040a0 };
    bool         gateEnabled    = false;
    bool         fxBusBypassed  = false;
    juce::String inputMode      = "Live";
    float        trimDb         = 0.0f;

    juce::Rectangle<float> blockRects[6];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalChainView)
};
