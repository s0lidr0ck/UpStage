#include "Looper.h"

Looper::Looper() {}

void Looper::prepare (double sr, int)
{
    sampleRate = sr;
    int maxSamples = (int)(sr * MAX_LOOP_SECONDS);
    loopBuffer.setSize (2, maxSamples);
    loopBuffer.clear();
    inputCapture.setSize (2, maxSamples);
    inputCapture.clear();
    loopLength = 0;
    writePos   = 0;
    readPos    = 0;
    state.store (State::Idle);
}

int Looper::calcSamplesPerBeat() const
{
    if (currentBPM <= 0.0) return 0;
    return (int)(sampleRate * 60.0 / currentBPM);
}

int Looper::calcFixedLoopLength() const
{
    if (loopBars <= 0 || currentBPM <= 0.0) return 0;
    int spb = (int)(sampleRate * 60.0 / currentBPM);
    return spb * meterNum * loopBars;
}

void Looper::startCountIn()
{
    samplesPerBeat = calcSamplesPerBeat();
    if (samplesPerBeat <= 0) return;

    countInSamplesLeft = samplesPerBeat * countInBeats;
    countInBeatPos = 0;
    fixedLoopLength = calcFixedLoopLength();

    loopBuffer.clear();
    inputCapture.clear();
    writePos = 0;
    readPos  = 0;
    loopLength = 0;

    triggerClick (true);
    state.store (State::CountIn);
}

void Looper::triggerClick (bool accent)
{
    isAccentClick = accent;
    clickPhase = 0.0;
    clickPhaseInc = (accent ? ACCENT_FREQ : NORMAL_FREQ) * 2.0 * juce::MathConstants<double>::pi / sampleRate;
    clickSamplesLeft = CLICK_DURATION;
}

float Looper::generateClickSample()
{
    if (clickSamplesLeft <= 0) return 0.0f;

    float env = (float) clickSamplesLeft / (float) CLICK_DURATION;
    float sample = std::sin ((float) clickPhase) * env * CLICK_VOLUME;
    clickPhase += clickPhaseInc;
    clickSamplesLeft--;
    return sample;
}

void Looper::beginRecording()
{
    fixedLoopLength = calcFixedLoopLength();
    state.store (State::Recording);
    if (onRecordingStarted)
        onRecordingStarted();
}

void Looper::finishRecording()
{
    loopLength = writePos;
    readPos = 0;
    state.store (State::Playing);
    if (onRecordingStopped)
        onRecordingStopped();
}

void Looper::feedInput (const juce::AudioBuffer<float>& buf, int numSamples)
{
    auto s = state.load();
    if (s != State::Recording && s != State::CountIn)
        return;
    if (capturePoint.load() != CapturePoint::Input)
        return;
    if (s != State::Recording)
        return;

    const int numCh = juce::jmin (buf.getNumChannels(), inputCapture.getNumChannels());
    const int maxLen = inputCapture.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        if (writePos + i < maxLen)
        {
            for (int ch = 0; ch < numCh; ++ch)
                inputCapture.getWritePointer (ch)[writePos + i] = buf.getReadPointer (ch)[i];
        }
    }
}

void Looper::processBlock (juce::AudioBuffer<float>& buffer)
{
    const auto s = state.load();
    if (s == State::Idle)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), loopBuffer.getNumChannels());
    const float vol = volume.load();
    const int maxLen = loopBuffer.getNumSamples();
    const bool captureInput = (capturePoint.load() == CapturePoint::Input);

    if (s == State::CountIn)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float click = generateClickSample();
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] += click;

            countInSamplesLeft--;
            countInBeatPos++;

            if (countInSamplesLeft <= 0)
            {
                beginRecording();
                break;
            }

            if (countInBeatPos >= samplesPerBeat)
            {
                countInBeatPos = 0;
                int beatsRemaining = countInSamplesLeft / samplesPerBeat;
                triggerClick (beatsRemaining % meterNum == (countInBeats % meterNum));
            }
        }
        return;
    }

    if (s == State::Recording)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (writePos < maxLen)
            {
                if (captureInput)
                {
                    for (int ch = 0; ch < numCh; ++ch)
                        loopBuffer.getWritePointer (ch)[writePos] = inputCapture.getReadPointer (ch)[writePos];
                }
                else
                {
                    for (int ch = 0; ch < numCh; ++ch)
                        loopBuffer.getWritePointer (ch)[writePos] = buffer.getReadPointer (ch)[i];
                }
                writePos++;
            }

            if (fixedLoopLength > 0 && writePos >= fixedLoopLength)
            {
                finishRecording();
                break;
            }
        }
    }
    else if (s == State::Playing || s == State::OverdubPending)
    {
        if (loopLength <= 0) return;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] += loopBuffer.getReadPointer (ch)[readPos] * vol;

            readPos++;
            if (readPos >= loopLength)
            {
                readPos = 0;
                if (s == State::OverdubPending)
                {
                    state.store (State::Overdubbing);
                    return;
                }
            }
        }
    }
    else if (s == State::Overdubbing)
    {
        if (loopLength <= 0) return;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                float loopSample = loopBuffer.getReadPointer (ch)[readPos];
                float inputSample = buffer.getReadPointer (ch)[i];
                buffer.getWritePointer (ch)[i] += loopSample * vol;
                loopBuffer.getWritePointer (ch)[readPos] = loopSample + inputSample;
            }

            readPos++;
            if (readPos >= loopLength)
                readPos = 0;
        }
    }
}

void Looper::toggleRecord()
{
    auto s = state.load();

    if (s == State::Idle)
    {
        if (countInBeats > 0 && currentBPM > 0.0)
        {
            startCountIn();
            return;
        }

        loopBuffer.clear();
        inputCapture.clear();
        writePos = 0;
        readPos  = 0;
        loopLength = 0;
        beginRecording();
    }
    else if (s == State::CountIn)
    {
        state.store (State::Idle);
        clickSamplesLeft = 0;
        if (onRecordingStopped)
            onRecordingStopped();
    }
    else if (s == State::Recording)
    {
        finishRecording();
    }
    else if (s == State::Overdubbing)
    {
        state.store (State::Playing);
    }
}

void Looper::togglePlayStop()
{
    auto s = state.load();

    if (s == State::Playing || s == State::Overdubbing || s == State::OverdubPending)
    {
        readPos = 0;
        state.store (State::Idle);
    }
    else if (s == State::Idle && loopLength > 0)
    {
        readPos = 0;
        state.store (State::Playing);
    }
}

void Looper::startOverdub()
{
    auto s = state.load();

    if (s == State::Playing)
        state.store (State::OverdubPending);
    else if (s == State::OverdubPending)
        state.store (State::Playing);
    else if (s == State::Overdubbing)
        state.store (State::Playing);
}

void Looper::stop()
{
    auto s = state.load();

    if (s == State::Recording)
        loopLength = writePos;

    readPos = 0;
    clickSamplesLeft = 0;

    bool wasActive = (s != State::Idle);
    state.store (State::Idle);

    if (wasActive && onRecordingStopped)
        onRecordingStopped();
}

void Looper::clear()
{
    bool wasActive = (state.load() != State::Idle);
    state.store (State::Idle);
    loopBuffer.clear();
    inputCapture.clear();
    loopLength = 0;
    writePos   = 0;
    readPos    = 0;
    clickSamplesLeft = 0;

    if (wasActive && onRecordingStopped)
        onRecordingStopped();
}

double Looper::getPositionNormalised() const
{
    if (loopLength <= 0) return 0.0;
    return (double) readPos / (double) loopLength;
}

double Looper::getElapsedSeconds() const
{
    auto s = state.load();
    if (s == State::Recording)
        return (double) writePos / sampleRate;
    if (s == State::CountIn)
        return -(double) countInSamplesLeft / sampleRate;
    if (loopLength > 0)
        return (double) readPos / sampleRate;
    return 0.0;
}

bool Looper::exportToFile (const juce::File& file) const
{
    if (loopLength <= 0)
        return false;

    file.getParentDirectory().createDirectory();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> fos (file.createOutputStream());
    if (fos == nullptr)
        return false;

    auto* writer = wavFormat.createWriterFor (fos.get(), sampleRate, 2, 16, {}, 0);
    if (writer == nullptr)
        return false;

    fos.release();

    juce::AudioBuffer<float> exportBuf (2, loopLength);
    for (int ch = 0; ch < 2 && ch < loopBuffer.getNumChannels(); ++ch)
        exportBuf.copyFrom (ch, 0, loopBuffer, ch, 0, loopLength);

    writer->writeFromAudioSampleBuffer (exportBuf, 0, loopLength);
    delete writer;
    return true;
}
