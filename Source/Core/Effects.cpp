#include "Effects.h"
#include <cmath>

namespace noize
{
namespace
{
inline float getP (const ParamMap& p, const char* k, float def)
{
    auto it = p.find (k);
    return it == p.end() ? def : it->second;
}
inline float dbToGain (float db) { return juce::Decibels::decibelsToGain (db); }

// ------------------------------------------------------------------ Gain
class GainFx : public Effect
{
public:
    void prepare (double, int) override {}
    void setParams (const ParamMap& p) override { target = dbToGain (getP (p, "gainDb", 0.f)); }
    void process (juce::AudioBuffer<float>& b) override
    {
        b.applyGainRamp (0, b.getNumSamples(), current, target);
        current = target;
    }
private:
    float current = 1.f, target = 1.f;
};

// ------------------------------------------------------------------ EQ (low shelf / peak / high shelf)
class EqFx : public Effect
{
public:
    void prepare (double sampleRate, int) override { sr = sampleRate; reset(); dirty = true; }
    void reset() override { for (auto& f : filters) f.reset(); }
    void setParams (const ParamMap& p) override
    {
        const float lf = getP (p, "lowFreq", 120), lg = getP (p, "lowGain", 0);
        const float mf = getP (p, "midFreq", 1000), mg = getP (p, "midGain", 0), mq = getP (p, "midQ", 1);
        const float hf = getP (p, "highFreq", 8000), hg = getP (p, "highGain", 0);
        if (dirty || lf != cLf || lg != cLg || mf != cMf || mg != cMg || mq != cMq || hf != cHf || hg != cHg)
        {
            cLf = lf; cLg = lg; cMf = mf; cMg = mg; cMq = mq; cHf = hf; cHg = hg; dirty = false;
            const auto nyq = (float) (sr * 0.45);
            auto low  = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, juce::jlimit (20.f, nyq, lf), 0.707f, dbToGain (lg));
            auto mid  = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, juce::jlimit (20.f, nyq, mf), juce::jmax (0.1f, mq), dbToGain (mg));
            auto high = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, juce::jlimit (20.f, nyq, hf), 0.707f, dbToGain (hg));
            for (int ch = 0; ch < 2; ++ch)
            {
                *filters[ch * 3 + 0].coefficients = *low;
                *filters[ch * 3 + 1].coefficients = *mid;
                *filters[ch * 3 + 2].coefficients = *high;
            }
        }
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = d[i];
                x = filters[ch * 3 + 0].processSample (x);
                x = filters[ch * 3 + 1].processSample (x);
                x = filters[ch * 3 + 2].processSample (x);
                d[i] = x;
            }
        }
    }
private:
    double sr = 44100.0; bool dirty = true;
    float cLf = 0, cLg = 0, cMf = 0, cMg = 0, cMq = 0, cHf = 0, cHg = 0;
    std::array<juce::dsp::IIR::Filter<float>, 6> filters {
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)),
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)),
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)),
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)),
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)),
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)) };
};

// ------------------------------------------------------------------ Compressor (feed-forward, linked stereo)
class CompressorFx : public Effect
{
public:
    void prepare (double sampleRate, int) override { sr = sampleRate; env = 0.f; }
    void reset() override { env = 0.f; }
    void setParams (const ParamMap& p) override
    {
        thresholdDb = getP (p, "threshold", -18);
        ratio = juce::jmax (1.f, getP (p, "ratio", 3));
        attackCoef = std::exp (-1.f / (float) (sr * juce::jmax (0.1f, getP (p, "attack", 10)) * 0.001));
        releaseCoef = std::exp (-1.f / (float) (sr * juce::jmax (1.f, getP (p, "release", 120)) * 0.001));
        makeup = dbToGain (getP (p, "makeup", 0));
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nch = b.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.f;
            for (int ch = 0; ch < nch; ++ch) peak = juce::jmax (peak, std::abs (b.getReadPointer (ch)[i]));
            const float coef = peak > env ? attackCoef : releaseCoef;
            env = peak + coef * (env - peak);
            const float envDb = juce::Decibels::gainToDecibels (env, -120.f);
            float gainDb = 0.f;
            if (envDb > thresholdDb) gainDb = (thresholdDb - envDb) * (1.f - 1.f / ratio);
            const float g = dbToGain (gainDb) * makeup;
            for (int ch = 0; ch < nch; ++ch) b.getWritePointer (ch)[i] *= g;
        }
    }
private:
    double sr = 44100.0;
    float thresholdDb = -18, ratio = 3, attackCoef = 0.f, releaseCoef = 0.f, makeup = 1.f, env = 0.f;
};

// ------------------------------------------------------------------ Limiter (instant attack gain reduction + safety clip)
class LimiterFx : public Effect
{
public:
    void prepare (double sampleRate, int) override { sr = sampleRate; gain = 1.f; }
    void reset() override { gain = 1.f; }
    void setParams (const ParamMap& p) override
    {
        ceiling = dbToGain (getP (p, "ceiling", -0.3f));
        releaseCoef = std::exp (-1.f / (float) (sr * juce::jmax (1.f, getP (p, "release", 80)) * 0.001));
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nch = b.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.f;
            for (int ch = 0; ch < nch; ++ch) peak = juce::jmax (peak, std::abs (b.getReadPointer (ch)[i]));
            const float needed = peak > ceiling ? ceiling / peak : 1.f;
            if (needed < gain) gain = needed;                       // instant attack
            else gain = needed + releaseCoef * (gain - needed);     // smooth release
            for (int ch = 0; ch < nch; ++ch)
            {
                auto& s = b.getWritePointer (ch)[i];
                s = juce::jlimit (-ceiling, ceiling, s * gain);     // guarantees the ceiling is never exceeded
            }
        }
    }
private:
    double sr = 44100.0; float ceiling = 0.97f, releaseCoef = 0.999f, gain = 1.f;
};

// ------------------------------------------------------------------ Reverb
class ReverbFx : public Effect
{
public:
    void prepare (double sampleRate, int) override { rev.setSampleRate (sampleRate); rev.reset(); }
    void reset() override { rev.reset(); }
    void setParams (const ParamMap& p) override
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = getP (p, "room", 0.5f);
        rp.damping = getP (p, "damp", 0.5f);
        rp.wetLevel = getP (p, "wet", 0.25f);
        rp.dryLevel = 1.f - rp.wetLevel * 0.5f;
        rp.width = 1.f;
        rev.setParameters (rp);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        if (b.getNumChannels() >= 2) rev.processStereo (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
        else rev.processMono (b.getWritePointer (0), b.getNumSamples());
    }
private:
    juce::Reverb rev;
};

// ------------------------------------------------------------------ Delay (feedback, stereo)
class DelayFx : public Effect
{
public:
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        size = (int) (sr * 2.5) + 8;
        for (auto& l : lines) l.assign ((size_t) size, 0.f);
        pos = 0;
    }
    void reset() override { for (auto& l : lines) std::fill (l.begin(), l.end(), 0.f); }
    void setParams (const ParamMap& p) override
    {
        delaySamples = juce::jlimit (1, size - 2, (int) (getP (p, "timeMs", 375) * 0.001 * sr));
        feedback = juce::jlimit (0.f, 0.95f, getP (p, "feedback", 0.35f));
        wet = getP (p, "wet", 0.3f);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int nch = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int rd = (pos - delaySamples + size) % size;
            for (int ch = 0; ch < nch; ++ch)
            {
                float* d = b.getWritePointer (ch);
                const float delayed = lines[(size_t) ch][(size_t) rd];
                lines[(size_t) ch][(size_t) pos] = d[i] + delayed * feedback;
                d[i] = d[i] + delayed * wet;
            }
            pos = (pos + 1) % size;
        }
    }
private:
    double sr = 44100.0; int size = 1, pos = 0, delaySamples = 1; float feedback = 0.35f, wet = 0.3f;
    std::array<std::vector<float>, 2> lines;
};

// ------------------------------------------------------------------ Chorus
class ChorusFx : public Effect
{
public:
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate; size = (int) (sr * 0.06) + 8;
        for (auto& l : lines) l.assign ((size_t) size, 0.f);
        pos = 0; phase = 0.f;
    }
    void reset() override { for (auto& l : lines) std::fill (l.begin(), l.end(), 0.f); }
    void setParams (const ParamMap& p) override
    {
        rate = getP (p, "rate", 0.8f); depth = getP (p, "depth", 0.35f); mix = getP (p, "mix", 0.4f);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int nch = juce::jmin (2, b.getNumChannels());
        const float inc = (float) (rate / sr);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < nch; ++ch)
            {
                float ph = phase + (ch == 1 ? 0.25f : 0.f);
                ph -= std::floor (ph);
                const float lfo = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * ph);
                const float delaySamp = (float) (sr * (0.008 + 0.012 * depth * lfo));
                float rp = (float) pos - delaySamp;
                while (rp < 0) rp += (float) size;
                const int i0 = (int) rp, i1 = (i0 + 1) % size;
                const float fr = rp - (float) i0;
                auto& line = lines[(size_t) ch];
                const float del = line[(size_t) i0] * (1.f - fr) + line[(size_t) i1] * fr;
                float* d = b.getWritePointer (ch);
                line[(size_t) pos] = d[i];
                d[i] = d[i] * (1.f - mix * 0.5f) + del * mix;
            }
            pos = (pos + 1) % size;
            phase += inc; if (phase >= 1.f) phase -= 1.f;
        }
    }
private:
    double sr = 44100.0; int size = 1, pos = 0; float phase = 0.f, rate = 0.8f, depth = 0.35f, mix = 0.4f;
    std::array<std::vector<float>, 2> lines;
};

// ------------------------------------------------------------------ Distortion (tanh waveshaper + tone)
class DistortionFx : public Effect
{
public:
    void prepare (double sampleRate, int) override { sr = sampleRate; z[0] = z[1] = 0.f; }
    void reset() override { z[0] = z[1] = 0.f; }
    void setParams (const ParamMap& p) override
    {
        driveGain = dbToGain (getP (p, "drive", 12));
        mix = getP (p, "mix", 1.f);
        const float tone = getP (p, "tone", 0.6f);
        const float fc = 500.f + tone * 14000.f;
        a = 1.f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sr);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const float comp = 1.f / std::tanh (juce::jmax (1.f, driveGain * 0.5f));
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float dry = d[i];
                float wet = std::tanh (dry * driveGain) * comp * 0.7f;
                z[ch] += a * (wet - z[ch]);
                d[i] = dry * (1.f - mix) + z[ch] * mix;
            }
        }
    }
private:
    double sr = 44100.0; float driveGain = 4.f, mix = 1.f, a = 0.5f, z[2] { 0.f, 0.f };
};

// ------------------------------------------------------------------ Low-pass / High-pass
class FilterFx : public Effect
{
public:
    explicit FilterFx (bool highPass) : hp (highPass) {}
    void prepare (double sampleRate, int) override { sr = sampleRate; reset(); }
    void reset() override { for (auto& f : filt) f.reset(); }
    void setParams (const ParamMap& p) override
    {
        const float fc = juce::jlimit (20.f, (float) (sr * 0.45), getP (p, "cutoff", hp ? 80.f : 8000.f));
        const float q = juce::jmax (0.1f, getP (p, "resonance", 0.7f));
        auto c = hp ? juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, fc, q)
                    : juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, fc, q);
        for (auto& f : filt) *f.coefficients = *c;
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i) d[i] = filt[(size_t) ch].processSample (d[i]);
        }
    }
private:
    bool hp; double sr = 44100.0;
    std::array<juce::dsp::IIR::Filter<float>, 2> filt {
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)),
        juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1.f, 0.f, 0.f, 1.f, 0.f, 0.f)) };
};

// ------------------------------------------------------------------ Stereo width (mid/side)
class WidthFx : public Effect
{
public:
    void prepare (double, int) override {}
    void setParams (const ParamMap& p) override { width = juce::jlimit (0.f, 2.f, getP (p, "width", 1.f)); }
    void process (juce::AudioBuffer<float>& b) override
    {
        if (b.getNumChannels() < 2) return;
        auto* l = b.getWritePointer (0); auto* r = b.getWritePointer (1);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * width;
            l[i] = m + s; r[i] = m - s;
        }
    }
private:
    float width = 1.f;
};
} // namespace

std::unique_ptr<Effect> createEffect (EffectType t)
{
    switch (t)
    {
        case EffectType::EQ:          return std::make_unique<EqFx>();
        case EffectType::Compressor:  return std::make_unique<CompressorFx>();
        case EffectType::Limiter:     return std::make_unique<LimiterFx>();
        case EffectType::Reverb:      return std::make_unique<ReverbFx>();
        case EffectType::Delay:       return std::make_unique<DelayFx>();
        case EffectType::Chorus:      return std::make_unique<ChorusFx>();
        case EffectType::Distortion:  return std::make_unique<DistortionFx>();
        case EffectType::LowPass:     return std::make_unique<FilterFx> (false);
        case EffectType::HighPass:    return std::make_unique<FilterFx> (true);
        case EffectType::StereoWidth: return std::make_unique<WidthFx>();
        case EffectType::Gain:        return std::make_unique<GainFx>();
    }
    return nullptr;
}

// ------------------------------------------------------------------ chain
void EffectChain::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate; maxBlock = maxBlockSize;
    for (auto& e : entries) e.fx->prepare (sr, maxBlock);
}

void EffectChain::reset() { for (auto& e : entries) e.fx->reset(); }

void EffectChain::update (const std::vector<EffectSlot>& slots)
{
    // Rebuild only when the structure (order / types) changed. Instantiation happens on the audio thread here;
    // the engine calls update() from its prepare/snapshot-apply stage, which allocates outside the hot path
    // whenever a new snapshot is published (see Engine::applySnapshot).
    bool same = slots.size() == entries.size();
    for (size_t i = 0; same && i < slots.size(); ++i) same = slots[i].type == entries[i].type;
    if (! same)
    {
        std::vector<Entry> fresh;
        for (auto& s : slots)
        {
            Entry e { s.type, s.bypass, createEffect (s.type) };
            e.fx->prepare (sr, maxBlock);
            fresh.push_back (std::move (e));
        }
        entries = std::move (fresh);
    }
    for (size_t i = 0; i < slots.size(); ++i)
    {
        entries[i].bypass = slots[i].bypass;
        entries[i].fx->setParams (slots[i].params);
    }
}

void EffectChain::process (juce::AudioBuffer<float>& buffer)
{
    for (auto& e : entries)
        if (! e.bypass) e.fx->process (buffer);
}
}
