#include "Instruments.h"
#include <cmath>
#include <array>

namespace noize
{
namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

inline float getP (const ParamMap& p, const char* k, float def)
{
    auto it = p.find (k);
    return it == p.end() ? def : it->second;
}
inline float midiToHz (float note) { return 440.f * std::pow (2.f, (note - 69.f) / 12.f); }

// Simple xorshift noise (deterministic, allocation-free)
struct Noise
{
    uint32_t s = 22222u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s / 2147483648.f; }
};

// ------------------------------------------------------------ Subtractive synth
inline float polyBlep (float t, float dt)
{
    if (t < dt)        { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt)  { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

// wave: 0 saw, 1 square, 2 triangle, 3 sine
inline float oscSample (int wave, float phase, float dt)
{
    switch (wave)
    {
        case 0: return 2.f * phase - 1.f - polyBlep (phase, dt);
        case 1:
        {
            float v = phase < 0.5f ? 1.f : -1.f;
            v += polyBlep (phase, dt);
            v -= polyBlep (std::fmod (phase + 0.5f, 1.f), dt);
            return v;
        }
        case 2: return 4.f * std::abs (phase - 0.5f) - 1.f;
        default: return std::sin (kTwoPi * phase);
    }
}

class SubtractiveSynth : public Instrument
{
public:
    explicit SubtractiveSynth (int maxVoices) : voiceLimit (maxVoices) {}

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& v : voices) { v.env.setSampleRate (sr); v.active = false; }
    }

    void setParams (const ParamMap& p) override
    {
        wave1 = (int) getP (p, "wave1", 0); wave2 = (int) getP (p, "wave2", 0);
        detune = getP (p, "detune", 0.1f); mix2 = getP (p, "mix2", 0.4f);
        envParams.attack = juce::jmax (0.001f, getP (p, "attack", 0.01f));
        envParams.decay = juce::jmax (0.001f, getP (p, "decay", 0.2f));
        envParams.sustain = juce::jlimit (0.f, 1.f, getP (p, "sustain", 0.7f));
        envParams.release = juce::jmax (0.002f, getP (p, "release", 0.3f));
        cutoff = getP (p, "cutoff", 9000); resonance = getP (p, "resonance", 0.2f);
        filterEnv = getP (p, "filterEnv", 0.f); gain = getP (p, "gain", 0.5f);
        for (auto& v : voices) v.env.setParameters (envParams);
    }

    void noteOn (int note, float vel) override
    {
        Voice* v = nullptr;
        if (voiceLimit == 1) { v = &voices[0]; }
        else
        {
            for (int i = 0; i < voiceLimit && ! v; ++i) if (! voices[(size_t) i].active) v = &voices[(size_t) i];
            if (! v)   // steal the oldest
            {
                v = &voices[0];
                for (int i = 1; i < voiceLimit; ++i) if (voices[(size_t) i].age > v->age) v = &voices[(size_t) i];
            }
        }
        v->active = true; v->note = note; v->vel = vel; v->age = 0;
        v->freq = midiToHz ((float) note);
        v->phase1 = 0.f; v->phase2 = 0.3f; v->z1 = v->z2 = 0.f;
        v->env.setParameters (envParams);
        v->env.noteOn();
    }

    void noteOff (int note) override
    {
        for (int i = 0; i < voiceLimit; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.active && v.note == note && v.env.isActive()) v.env.noteOff();
        }
    }
    void allNotesOff() override { for (auto& v : voices) { v.env.reset(); v.active = false; } }

    void render (juce::AudioBuffer<float>& out, int start, int num) override
    {
        auto* L = out.getWritePointer (0, start);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;
        const float det = std::pow (2.f, detune / 12.f);
        for (int vi = 0; vi < voiceLimit; ++vi)
        {
            auto& v = voices[(size_t) vi];
            if (! v.active) continue;
            const float dt1 = v.freq / (float) sr, dt2 = v.freq * det / (float) sr;
            for (int i = 0; i < num; ++i)
            {
                const float e = v.env.getNextSample();
                if (! v.env.isActive()) { v.active = false; break; }
                float s = oscSample (wave1, v.phase1, dt1) * (1.f - mix2 * 0.5f) + oscSample (wave2, v.phase2, dt2) * mix2;
                v.phase1 += dt1; if (v.phase1 >= 1.f) v.phase1 -= 1.f;
                v.phase2 += dt2; if (v.phase2 >= 1.f) v.phase2 -= 1.f;

                // TPT state-variable low-pass, cutoff modulated by the envelope
                const float fc = juce::jlimit (30.f, (float) (sr * 0.45), cutoff + filterEnv * e * v.vel);
                const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
                const float k = 2.f - 1.9f * juce::jlimit (0.f, 1.f, resonance);
                const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const float v3 = s - v.z2;
                const float v1 = a1 * v.z1 + a2 * v3;
                const float v2 = v.z2 + a2 * v.z1 + a3 * v3;
                v.z1 = 2.f * v1 - v.z1; v.z2 = 2.f * v2 - v.z2;
                const float y = v2 * e * (0.25f + 0.75f * v.vel) * gain;
                L[i] += y; if (R) R[i] += y;
            }
            ++v.age;
        }
    }

private:
    struct Voice
    {
        bool active = false; int note = 0, age = 0; float vel = 0.f, freq = 440.f, phase1 = 0.f, phase2 = 0.f, z1 = 0.f, z2 = 0.f;
        juce::ADSR env;
    };
    int voiceLimit; double sr = 44100.0;
    std::array<Voice, 16> voices;
    juce::ADSR::Parameters envParams;
    int wave1 = 0, wave2 = 0; float detune = 0.1f, mix2 = 0.4f, cutoff = 9000.f, resonance = 0.2f, filterEnv = 0.f, gain = 0.5f;
};

// ------------------------------------------------------------ Drum machine (fully synthesized)
class DrumMachine : public Instrument
{
public:
    void prepare (double sampleRate, int) override { sr = sampleRate; allNotesOff(); }
    void setParams (const ParamMap& p) override
    {
        kickTune = getP (p, "kickTune", 50); kickDecay = getP (p, "kickDecay", 0.35f);
        snareTone = getP (p, "snareTone", 0.5f); hatDecay = getP (p, "hatDecay", 0.05f); gain = getP (p, "gain", 0.8f);
    }
    void noteOn (int note, float vel) override
    {
        Kind k; float pitch = 0.f;
        switch (note)
        {
            case 35: case 36: k = Kind::Kick; break;
            case 37: case 38: case 40: k = Kind::Snare; break;
            case 39: k = Kind::Clap; break;
            case 42: case 44: k = Kind::HatClosed; break;
            case 46: k = Kind::HatOpen; break;
            case 49: case 51: case 57: k = Kind::Crash; break;
            default: k = Kind::Tom; pitch = (float) (note - 45); break;
        }
        if (k == Kind::HatClosed) for (auto& v : voices) if (v.kind == Kind::HatOpen) v.active = false;   // choke
        Hit* h = nullptr;
        for (auto& v : voices) if (! v.active) { h = &v; break; }
        if (! h) h = &voices[0];
        *h = Hit();
        h->active = true; h->kind = k; h->vel = vel; h->pitch = pitch;
    }
    void noteOff (int) override {}   // drums are one-shots
    void allNotesOff() override { for (auto& v : voices) v.active = false; }

    void render (juce::AudioBuffer<float>& out, int start, int num) override
    {
        auto* L = out.getWritePointer (0, start);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;
        const float dt = 1.f / (float) sr;
        for (auto& h : voices)
        {
            if (! h.active) continue;
            for (int i = 0; i < num; ++i)
            {
                float y = 0.f, t = h.t;
                switch (h.kind)
                {
                    case Kind::Kick:
                    {
                        const float f = kickTune + 110.f * std::exp (-t * 38.f);
                        h.phase += f * dt; h.phase -= std::floor (h.phase);
                        y = std::sin (kTwoPi * h.phase) * std::exp (-t / juce::jmax (0.05f, kickDecay) * 4.f)
                          + 0.4f * std::exp (-t * 400.f) * h.noise.next();
                        if (t > kickDecay * 1.5f) h.active = false;
                        break;
                    }
                    case Kind::Snare:
                    {
                        h.phase += 185.f * dt; h.phase -= std::floor (h.phase);
                        const float tone = std::sin (kTwoPi * h.phase) * std::exp (-t * 28.f) * (0.3f + snareTone);
                        const float n = h.noise.next();
                        h.hp += 0.25f * (n - h.hp);
                        y = tone * 0.7f + (n - h.hp) * std::exp (-t * 16.f) * 0.9f;
                        if (t > 0.35f) h.active = false;
                        break;
                    }
                    case Kind::Clap:
                    {
                        const float n = h.noise.next();
                        const float bursts = (t < 0.01f || (t > 0.02f && t < 0.03f) || (t > 0.04f && t < 0.05f)) ? 1.f : 0.f;
                        y = n * (bursts * std::exp (-t * 30.f) * 0.9f + std::exp (-(t - 0.05f) * 25.f) * (t > 0.05f ? 0.6f : 0.f));
                        if (t > 0.3f) h.active = false;
                        break;
                    }
                    case Kind::HatClosed: case Kind::HatOpen:
                    {
                        const float n = h.noise.next();
                        h.hp += 0.6f * (n - h.hp);
                        const float dec = h.kind == Kind::HatOpen ? 0.3f : hatDecay;
                        y = (n - h.hp) * std::exp (-t / juce::jmax (0.01f, dec) * 5.f) * 0.7f;
                        if (t > dec * 2.f) h.active = false;
                        break;
                    }
                    case Kind::Crash:
                    {
                        const float n = h.noise.next();
                        h.hp += 0.5f * (n - h.hp);
                        y = (n - h.hp) * std::exp (-t * 2.5f) * 0.6f;
                        if (t > 1.8f) h.active = false;
                        break;
                    }
                    case Kind::Tom:
                    {
                        const float base = 110.f * std::pow (2.f, h.pitch / 12.f);
                        const float f = base + 60.f * std::exp (-t * 25.f);
                        h.phase += f * dt; h.phase -= std::floor (h.phase);
                        y = std::sin (kTwoPi * h.phase) * std::exp (-t * 9.f);
                        if (t > 0.6f) h.active = false;
                        break;
                    }
                }
                h.t += dt;
                y *= h.vel * gain;
                L[i] += y; if (R) R[i] += y;
                if (! h.active) break;
            }
        }
    }
private:
    enum class Kind { Kick, Snare, Clap, HatClosed, HatOpen, Crash, Tom };
    struct Hit { bool active = false; Kind kind = Kind::Kick; float vel = 1.f, t = 0.f, phase = 0.f, hp = 0.f, pitch = 0.f; Noise noise; };
    std::array<Hit, 24> voices;
    double sr = 44100.0; float kickTune = 50, kickDecay = 0.35f, snareTone = 0.5f, hatDecay = 0.05f, gain = 0.8f;
};

// ------------------------------------------------------------ Sampler
class Sampler : public Instrument
{
public:
    void prepare (double sampleRate, int) override { sr = sampleRate; allNotesOff(); }
    void setParams (const ParamMap& p) override
    {
        root = getP (p, "rootNote", 60); gain = getP (p, "gain", 0.8f);
        ep.attack = juce::jmax (0.001f, getP (p, "attack", 0.002f)); ep.decay = 0.1f; ep.sustain = 1.f;
        ep.release = juce::jmax (0.005f, getP (p, "release", 0.15f));
    }
    void setSample (std::shared_ptr<const SampleData> s) override { allNotesOff(); data = std::move (s); }
    void noteOn (int note, float vel) override
    {
        if (! data) return;
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (! v) v = &voices[0];
        v->active = true; v->note = note; v->vel = vel; v->pos = 0.0;
        v->ratio = std::pow (2.0, (note - (double) root) / 12.0) * data->sampleRate / sr;
        v->env.setSampleRate (sr); v->env.setParameters (ep); v->env.noteOn();
    }
    void noteOff (int note) override { for (auto& v : voices) if (v.active && v.note == note) v.env.noteOff(); }
    void allNotesOff() override { for (auto& v : voices) { v.active = false; v.env.reset(); } }
    void render (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! data) return;
        const auto& buf = data->buffer; const int len = buf.getNumSamples(), nch = buf.getNumChannels();
        if (len < 2) return;
        auto* L = out.getWritePointer (0, start);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            for (int i = 0; i < num; ++i)
            {
                const int i0 = (int) v.pos;
                if (i0 >= len - 1 || ! v.env.isActive()) { v.active = false; break; }
                const float fr = (float) (v.pos - i0), e = v.env.getNextSample() * v.vel * gain;
                const float l = buf.getSample (0, i0) * (1.f - fr) + buf.getSample (0, i0 + 1) * fr;
                const float r = nch > 1 ? buf.getSample (1, i0) * (1.f - fr) + buf.getSample (1, i0 + 1) * fr : l;
                L[i] += l * e; if (R) R[i] += r * e;
                v.pos += v.ratio;
            }
        }
    }
private:
    struct Voice { bool active = false; int note = 0; float vel = 0.f; double pos = 0.0, ratio = 1.0; juce::ADSR env; };
    std::array<Voice, 16> voices; std::shared_ptr<const SampleData> data; juce::ADSR::Parameters ep;
    double sr = 44100.0; float root = 60.f, gain = 0.8f;
};

// ------------------------------------------------------------ Piano (additive string model with hammer noise)
class PianoInstrument : public Instrument
{
public:
    void prepare (double sampleRate, int) override { sr = sampleRate; allNotesOff(); }
    void setParams (const ParamMap& p) override
    {
        brightness = getP (p, "brightness", 0.5f); decay = juce::jmax (0.2f, getP (p, "decay", 1.8f));
        release = juce::jmax (0.03f, getP (p, "release", 0.25f)); gain = getP (p, "gain", 0.6f);
    }
    void noteOn (int note, float vel) override
    {
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (! v) v = &voices[0];
        *v = Voice();
        v->active = true; v->note = note; v->vel = vel;
        const float f0 = midiToHz ((float) note);
        const float noteDecay = decay * std::pow (2.f, -(note - 60) / 24.f);   // higher notes die faster
        for (int k = 0; k < kPartials; ++k)
        {
            const float n = (float) (k + 1);
            const float f = f0 * n * std::sqrt (1.f + 0.0004f * n * n);
            if (f > sr * 0.45f) { v->amp[k] = 0.f; continue; }
            const float tilt = std::pow (n, -(1.6f - 0.9f * brightness - 0.5f * vel));
            v->amp[k] = tilt;
            v->inc[k] = f / (float) sr;
            v->rate[k] = std::exp (-1.f / (float) sr * (4.f / noteDecay) * (1.f + 0.35f * (n - 1.f)));
        }
    }
    void noteOff (int note) override { for (auto& v : voices) if (v.active && v.note == note) v.released = true; }
    void allNotesOff() override { for (auto& v : voices) v.active = false; }
    void render (juce::AudioBuffer<float>& out, int start, int num) override
    {
        auto* L = out.getWritePointer (0, start);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;
        const float relRate = std::exp (-1.f / ((float) sr * release) * 5.f);
        for (auto& v : voices)
        {
            if (! v.active) continue;
            for (int i = 0; i < num; ++i)
            {
                float s = 0.f;
                for (int k = 0; k < kPartials; ++k)
                {
                    if (v.amp[k] == 0.f) continue;
                    s += std::sin (kTwoPi * v.phase[k]) * v.amp[k] * v.env[k];
                    v.phase[k] += v.inc[k]; v.phase[k] -= std::floor (v.phase[k]);
                    v.env[k] *= v.rate[k];
                }
                // hammer thump
                const float h = v.noise.next(); v.hz += 0.15f * (h - v.hz);
                s += v.hz * std::exp (-v.t * 90.f) * 0.5f * v.vel;
                v.t += 1.f / (float) sr;
                if (v.released) v.rel *= relRate;
                const float y = s * v.rel * v.vel * gain * 0.35f;
                L[i] += y; if (R) R[i] += y;
                if (v.rel < 0.0005f || v.t > 12.f) { v.active = false; break; }
            }
        }
    }
private:
    static constexpr int kPartials = 8;
    struct Voice
    {
        bool active = false, released = false; int note = 0; float vel = 0.f, t = 0.f, rel = 1.f, hz = 0.f;
        std::array<float, kPartials> amp {}, inc {}, rate {}, phase {}, env = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
        Noise noise;
    };
    std::array<Voice, 16> voices;
    double sr = 44100.0; float brightness = 0.5f, decay = 1.8f, release = 0.25f, gain = 0.6f;
};
} // namespace

std::unique_ptr<Instrument> createInstrument (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::PolySynth:   return std::make_unique<SubtractiveSynth> (16);
        case InstrumentType::SubSynth:    return std::make_unique<SubtractiveSynth> (8);
        case InstrumentType::BassSynth:   return std::make_unique<SubtractiveSynth> (1);
        case InstrumentType::DrumMachine: return std::make_unique<DrumMachine>();
        case InstrumentType::Sampler:     return std::make_unique<Sampler>();
        case InstrumentType::Piano:       return std::make_unique<PianoInstrument>();
    }
    return nullptr;
}
}
