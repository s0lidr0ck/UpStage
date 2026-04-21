#include "Recorder.h"

Recorder::Recorder()
{
    formatManager.registerBasicFormats();
    backgroundThread.startThread();
}

Recorder::~Recorder()
{
    stopRecording();
    backgroundThread.stopThread (2000);
}

void Recorder::prepare (double sr, int numChannels)
{
    currentSampleRate   = sr;
    currentNumChannels  = numChannels;
}

//==============================================================================
bool Recorder::startRecording (const juce::File& outputFolder, Mode mode)
{
    if (recording.load()) stopRecording();

    currentMode = mode;
    outputFolder.createDirectory();

    auto makeWriter = [&] (const juce::String& label)
        -> std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>
    {
        auto file = makeOutputFile (outputFolder, label);
        auto wavFormat = std::make_unique<juce::WavAudioFormat>();
        std::unique_ptr<juce::OutputStream> fileStream = file.createOutputStream();
        if (fileStream == nullptr) return nullptr;

        auto writer = wavFormat->createWriterFor (
            fileStream,
            juce::AudioFormatWriterOptions{}
                .withSampleRate    (currentSampleRate)
                .withNumChannels   (currentNumChannels)
                .withBitsPerSample (24));

        if (writer == nullptr) return nullptr;
        // fileStream ownership was transferred into writer by createWriterFor

        return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
            writer.release(), backgroundThread, 32768);
    };

    if (mode == Mode::InputOnly || mode == Mode::Both)
        inputWriter  = makeWriter ("Input");

    if (mode == Mode::OutputOnly || mode == Mode::Both)
        outputWriter = makeWriter ("Output");

    recording.store (true);
    return true;
}

void Recorder::stopRecording()
{
    recording.store (false);

    // Flush writers (blocks until queued samples are written)
    inputWriter.reset();
    outputWriter.reset();
}

//==============================================================================
void Recorder::writeInputBlock (const juce::AudioBuffer<float>& buffer)
{
    if (! recording.load() || inputWriter == nullptr) return;
    inputWriter->write (buffer.getArrayOfReadPointers(),
                        buffer.getNumSamples());
}

void Recorder::writeOutputBlock (const juce::AudioBuffer<float>& buffer)
{
    if (! recording.load() || outputWriter == nullptr) return;
    outputWriter->write (buffer.getArrayOfReadPointers(),
                         buffer.getNumSamples());
}

//==============================================================================
juce::File Recorder::makeOutputFile (const juce::File& folder,
                                     const juce::String& label)
{
    auto now = juce::Time::getCurrentTime();
    auto ts  = now.formatted ("%Y%m%d_%H%M%S");
    auto filename = "UpStage_" + label + "_" + ts + ".wav";
    return folder.getChildFile (filename);
}
