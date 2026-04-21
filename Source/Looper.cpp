#include "Looper.h"

Looper::Looper() {}

void Looper::prepare (double sr, int)
{
    sampleRate = sr;
    int maxSamples = (int)(sr * MAX_LOOP_SECONDS);
    loopBuffer.setSize (2, maxSamples);
    loopBuffer.clear();
    loopLength = 0;
    writePos   = 0;
    readPos    = 0;
    state.store (State::Idle);
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

    if (s == State::Recording)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (writePos < maxLen)
            {
                for (int ch = 0; ch < numCh; ++ch)
                    loopBuffer.getWritePointer (ch)[writePos] = buffer.getReadPointer (ch)[i];
                writePos++;
            }
        }
    }
    else if (s == State::Playing)
    {
        if (loopLength <= 0) return;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] += loopBuffer.getReadPointer (ch)[readPos] * vol;

            readPos++;
            if (readPos >= loopLength)
                readPos = 0;
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

    if (s == State::Idle || s == State::Playing)
    {
        if (s == State::Idle)
        {
            loopBuffer.clear();
            writePos = 0;
            readPos  = 0;
            loopLength = 0;
        }

        if (s == State::Playing)
        {
            state.store (State::Overdubbing);
            return;
        }

        state.store (State::Recording);
    }
    else if (s == State::Recording)
    {
        loopLength = writePos;
        readPos = 0;
        state.store (State::Playing);
    }
    else if (s == State::Overdubbing)
    {
        state.store (State::Playing);
    }
}

void Looper::togglePlay()
{
    auto s = state.load();

    if (s == State::Playing || s == State::Overdubbing)
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

void Looper::stop()
{
    auto s = state.load();

    if (s == State::Recording)
    {
        loopLength = writePos;
    }

    readPos = 0;
    state.store (State::Idle);
}

void Looper::clear()
{
    state.store (State::Idle);
    loopBuffer.clear();
    loopLength = 0;
    writePos   = 0;
    readPos    = 0;
}

double Looper::getPositionNormalised() const
{
    if (loopLength <= 0) return 0.0;
    return (double) readPos / (double) loopLength;
}
