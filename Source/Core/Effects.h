#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Project.h"

namespace noize
{
class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() {}
    virtual void setParams (const ParamMap& p) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;   // in-place, 1-2 channels
};

std::unique_ptr<Effect> createEffect (EffectType type);

/** Ordered, bypassable chain of effects, built from a vector<EffectSlot>.
    Rebuilding only happens when the slot types/order change; parameter updates are cheap. */
class EffectChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void update (const std::vector<EffectSlot>& slots);   // call from audio thread before processing a block
    void process (juce::AudioBuffer<float>& buffer);
    void reset();
private:
    struct Entry { EffectType type; bool bypass; std::unique_ptr<Effect> fx; };
    std::vector<Entry> entries;
    double sr = 44100.0; int maxBlock = 512;
};
}
