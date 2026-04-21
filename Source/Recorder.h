#pragma once
#include <JuceHeader.h>

/**
 * Recorder
 *
 * Captures audio to WAV files from the audio thread.
 * Supports three modes:
 *   - Input only  (dry guitar)
 *   - Output only (processed/wet signal)
 *   - Both simultaneously (two files: input_<ts>.wav + output_<ts>.wav)
 *
 * Files are written to a configurable output folder.
 * Filenames are auto-timestamped: "UpStage_Input_20260415_172300.wav"
 *
 * Call startRecording() / stopRecording() from the UI thread.
 * Call writeInputBlock() / writeOutputBlock() from the audio thread.
 */
class Recorder
{
public:
    enum class Mode { InputOnly, OutputOnly, Both };

    Recorder();
    ~Recorder();

    void prepare (double sampleRate, int numChannels);

    /** Start a new recording session. Creates file(s) immediately. */
    bool startRecording (const juce::File& outputFolder, Mode mode);

    /** Flush and close file(s). */
    void stopRecording();

    bool isRecording() const { return recording.load(); }
    Mode getMode()     const { return currentMode; }
    juce::File getLastOutputFolder() const { return lastOutputFolder; }

    /** Call from audio thread with the raw (pre-strip) input buffer. */
    void writeInputBlock  (const juce::AudioBuffer<float>& buffer);

    /** Call from audio thread with the processed output buffer. */
    void writeOutputBlock (const juce::AudioBuffer<float>& buffer);

private:
    std::atomic<bool> recording { false };
    Mode              currentMode = Mode::OutputOnly;
    double            currentSampleRate = 44100.0;
    int               currentNumChannels = 2;

    juce::AudioFormatManager       formatManager;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> inputWriter;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> outputWriter;

    juce::File            lastOutputFolder;
    juce::TimeSliceThread backgroundThread { "RecorderThread" };

    static juce::File makeOutputFile (const juce::File& folder,
                                      const juce::String& label);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};
