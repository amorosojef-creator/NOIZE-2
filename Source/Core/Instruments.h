#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Project.h"

namespace noize
{
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
};

class Instrument
{
public:
    virtual ~Instrument() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void setParams (const ParamMap& p) = 0;
    virtual void setSample (std::shared_ptr<const SampleData>) {}
    virtual void noteOn (int note, float velocity) = 0;
    virtual void noteOff (int note) = 0;
    virtual void allNotesOff() = 0;
    /** Adds `num` samples of audio into `out` starting at `start`. */
    virtual void render (juce::AudioBuffer<float>& out, int start, int num) = 0;
};

std::unique_ptr<Instrument> createInstrument (InstrumentType type);
}
