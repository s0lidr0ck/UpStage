#pragma once

#include <JuceHeader.h>
#include "AmpLibrary.h"

namespace nam { class DSP; }

/**
 * NamAmpProcessor - the internal NAM A2 amp sim.
 *
 * Lives in a channel's plugin chain as a juce::AudioPluginInstance so it rides
 * the existing bypass / reorder / editor-window / serialization machinery.
 *
 * Signal flow: stereo in -> mono sum -> input gain
 *   -> side A [trim -> NAM model -> cab IR]  (and side B in dual mode)
 *   -> blend + per-side equal-power pan -> stereo
 *   -> tone stack (bass/mid/treble) -> output gain -> stereo out.
 *
 * Threading: model loads run on a single-thread pool; the finished nam::DSP is
 * swapped in on the message thread under a SpinLock that processBlock only
 * try-locks (mid-swap blocks pass audio through untouched). Old models are
 * destroyed on the message thread, never under the lock, never on the audio
 * thread.
 */
class NamAmpProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr const char* kIdentifier = "UPSTAGE_INTERNAL:NAM_AMP";

    /** amp = full head row (tone stack, cab slot, dual mode). pedal = compact
        capture row (model + in/out gain only) meant to sit before the amp with
        plugins free to slot in between. Same engine, same identifier - the
        role is part of the saved state. */
    enum class Role { amp, pedal };

    explicit NamAmpProcessor (Role role = Role::amp);
    ~NamAmpProcessor() override;

    Role getRole() const { return role; }

    //==========================================================================
    // Rig / cab management. Sides: 0 = A, 1 = B. Message thread only.
    void loadRig (int side, const juce::String& rigId);
    void setCab (int side, const juce::String& cabId);   // empty id = rig's paired IR (or none)
    void setCabEnabled (int side, bool enabled);
    void setUseLite (bool lite);

    juce::String getRigId (int side) const   { return sides[juce::jlimit (0, 1, side)].rigId; }
    juce::String getRigName (int side) const { return sides[juce::jlimit (0, 1, side)].rigName; }
    juce::String getCabId (int side) const   { return sides[juce::jlimit (0, 1, side)].cabId; }
    juce::String getCabName (int side) const { return sides[juce::jlimit (0, 1, side)].cabName; }
    bool isCabEnabled (int side) const       { return sides[juce::jlimit (0, 1, side)].cabEnabled.load(); }
    bool hasModel (int side) const           { return sides[juce::jlimit (0, 1, side)].modelLive.load(); }
    bool didModelFail (int side) const       { return sides[juce::jlimit (0, 1, side)].failed.load(); }
    bool hasSampleRateMismatch (int side) const { return sides[juce::jlimit (0, 1, side)].srMismatch.load(); }
    bool isModelSlimmable (int side) const   { return sides[juce::jlimit (0, 1, side)].slimmable.load(); }
    juce::String getLastLoadError (int side) const { return sides[juce::jlimit (0, 1, side)].lastLoadError; }

    //==========================================================================
    // Parameters. Audio thread reads; message thread (editor) writes.
    std::atomic<float> inputGainDb  { 0.0f };
    std::atomic<float> bassKnob     { 5.0f };   // 0..10, 5 = flat
    std::atomic<float> midKnob      { 5.0f };
    std::atomic<float> trebleKnob   { 5.0f };
    std::atomic<float> outputGainDb { 0.0f };
    std::atomic<float> blend        { 0.5f };   // dual: 0 = all A, 1 = all B
    std::atomic<float> panA         { 0.0f };   // -1..1
    std::atomic<float> panB         { 0.0f };
    std::atomic<float> sideTrimDbA  { 0.0f };
    std::atomic<float> sideTrimDbB  { 0.0f };
    std::atomic<bool>  dualMode     { false };
    std::atomic<bool>  polarityFlipB { false };
    std::atomic<bool>  useLite      { false };

    /** Editor calls this after writing a tone knob so coefficients refresh. */
    void toneChanged() { toneDirty = true; }

    /** Fired on the message thread when a model arrives, fails, or a cab changes. */
    std::function<void()> onEngineStateChanged;

    //==========================================================================
    // juce::AudioProcessor
    // Stable per role: the appearance map keys on name.
    const juce::String getName() const override { return role == Role::pedal ? "NAM Pedal" : "NAM Amp"; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // juce::AudioPluginInstance
    void fillInPluginDescription (juce::PluginDescription& desc) const override;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        inline float p (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
        void makeLowShelf  (double sr, double f0, double gainDb) noexcept;
        void makePeaking   (double sr, double f0, double q, double gainDb) noexcept;
        void makeHighShelf (double sr, double f0, double gainDb) noexcept;
    };

    struct Side
    {
        juce::String rigId, cabId;          // library entry ids ("" = none / rig's paired IR)
        juce::String rigName, cabName;      // message-thread copies for the UI
        std::atomic<bool> cabEnabled { true };

        std::unique_ptr<nam::DSP> model;    // swapped under modelLock
        std::atomic<bool> modelLive  { false };
        std::atomic<bool> failed     { false };
        std::atomic<bool> srMismatch { false };
        std::atomic<bool> slimmable  { false };
        juce::String lastLoadError;         // message thread only

        juce::dsp::Convolution cab;         // has its own RT-safe background loader
        std::atomic<bool> cabLoaded { false };
    };

    void renderSide (Side& s, juce::AudioBuffer<float>& out, float trimDb, int n) noexcept;
    void updateToneCoeffs() noexcept;
    void applySlimSize (nam::DSP* m) const;

    Role role = Role::amp;
    Side sides[2];
    juce::SpinLock modelLock;
    juce::ThreadPool loaderPool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<int> loadGeneration[2] { { 0 }, { 0 } };

    double sampleRate = 0.0;
    int maxBlock = 0;
    juce::AudioBuffer<float> monoIn, sideBuf[2];

    Biquad toneLow[2], toneMid[2], toneHigh[2];
    std::atomic<bool> toneDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamAmpProcessor)
};
