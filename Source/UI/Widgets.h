#pragma once
#include "AppContext.h"

namespace noize
{
struct ParamRange { float lo = 0, hi = 1, skewMid = -1; bool integer = false; juce::String suffix; };

inline ParamRange rangeFor (bool fx, const juce::String& n, float def)
{
    static const std::map<juce::String, ParamRange> common = {
        { "cutoff", { 20, 20000, 1000, false, " Hz" } }, { "resonance", { 0, 1, -1 } }, { "gain", { 0, 1, -1 } }, { "detune", { 0, 1, -1 } }, { "mix2", { 0, 1, -1 } },
        { "wave1", { 0, 3, -1, true } }, { "wave2", { 0, 3, -1, true } }, { "filterEnv", { 0, 8000, 1500, false, " Hz" } },
        { "sustain", { 0, 1, -1 } }, { "decay", { 0.01f, 4, 0.5f, false, " s" } }, { "release", { 0.01f, 4, 0.4f, false, " s" } },
        { "kickTune", { 30, 100, -1, false, " Hz" } }, { "kickDecay", { 0.1f, 1.2f, -1, false, " s" } }, { "snareTone", { 0, 1, -1 } }, { "hatDecay", { 0.01f, 0.4f, -1, false, " s" } },
        { "rootNote", { 0, 127, -1, true } }, { "brightness", { 0, 1, -1 } },
        { "lowFreq", { 20, 500, -1, false, " Hz" } }, { "midFreq", { 100, 8000, 1000, false, " Hz" } }, { "highFreq", { 1000, 20000, 6000, false, " Hz" } },
        { "lowGain", { -18, 18, -1, false, " dB" } }, { "midGain", { -18, 18, -1, false, " dB" } }, { "highGain", { -18, 18, -1, false, " dB" } }, { "midQ", { 0.2f, 10, 1, false } },
        { "threshold", { -60, 0, -1, false, " dB" } }, { "ratio", { 1, 20, 4, false, ":1" } }, { "makeup", { 0, 24, -1, false, " dB" } },
        { "ceiling", { -12, 0, -1, false, " dB" } }, { "room", { 0, 1, -1 } }, { "damp", { 0, 1, -1 } }, { "wet", { 0, 1, -1 } },
        { "timeMs", { 10, 1500, 300, false, " ms" } }, { "feedback", { 0, 0.95f, -1 } }, { "rate", { 0.1f, 8, -1, false, " Hz" } }, { "depth", { 0, 1, -1 } },
        { "mix", { 0, 1, -1 } }, { "drive", { 0, 36, -1, false, " dB" } }, { "tone", { 0, 1, -1 } }, { "width", { 0, 2, -1 } }, { "gainDb", { -36, 18, -1, false, " dB" } } };
    if (fx && n == "attack")  return { 0.1f, 100, 10, false, " ms" };
    if (fx && n == "release") return { 10, 1000, 150, false, " ms" };
    if (! fx && n == "attack") return { 0.001f, 3, 0.1f, false, " s" };
    if (fx && n == "resonance") return { 0.3f, 4, 1, false };
    auto it = common.find (n);
    if (it != common.end()) return it->second;
    return { 0, juce::jmax (1.f, def * 2.f) };
}

/** Labelled rotary knob bound to a ParamMap entry via callbacks. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, ParamRange r, float value, std::function<void (float)> onBegin, std::function<void (float)> onChange)
        : label (name)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        juce::NormalisableRange<double> nr (r.lo, r.hi);
        if (r.skewMid > r.lo && r.skewMid < r.hi) nr.setSkewForCentre (r.skewMid);
        if (r.integer) nr.interval = 1.0;
        slider.setNormalisableRange (nr);
        slider.setValue (value, juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, value);
        if (r.suffix.isNotEmpty()) slider.setTextValueSuffix (r.suffix);
        slider.setNumDecimalPlacesToDisplay (r.integer ? 0 : (r.hi - r.lo > 100 ? 0 : 2));
        slider.onDragStart = [onBegin, this] { if (onBegin) onBegin ((float) slider.getValue()); };
        slider.onValueChange = [onChange, this] { if (onChange) onChange ((float) slider.getValue()); };
        addAndMakeVisible (slider);
        setTooltip (name);
    }
    void setTooltip (const juce::String& t) { slider.setTooltip (t); }
    void paint (juce::Graphics& g) override { g.setColour (Theme::dim); g.setFont (Theme::font (11.f, true)); g.drawText (label.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centred); }
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (12)); }
    juce::Slider slider;
private:
    juce::String label;
};

/** Grid of knobs editing one ParamMap. */
class ParamPanel : public juce::Component
{
public:
    std::function<void()> beginEdit;       // called once when a drag starts
    std::function<void()> changed;         // called on every value change
    void setParams (std::function<ParamMap*()> getter, bool isFx, std::vector<juce::String> only = {})
    {
        knobs.clear();
        getMap = std::move (getter);
        auto* map = getMap ? getMap() : nullptr;
        if (map == nullptr) { repaint(); return; }
        for (auto& kv : *map)
        {
            const juce::String key = kv.first;
            if (! only.empty() && std::find (only.begin(), only.end(), key) == only.end()) continue;
            auto k = std::make_unique<Knob> (key, rangeFor (isFx, key, kv.second), kv.second,
                [this] (float) { if (beginEdit) beginEdit(); },
                [this, key] (float v) { if (auto* m = getMap ? getMap() : nullptr) { (*m)[key] = v; if (changed) changed(); } });
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
        resized();
    }
    void resized() override
    {
        const int cw = 84, ch = 92;
        const int cols = juce::jmax (1, getWidth() / cw);
        int i = 0;
        for (auto& k : knobs) { k->setBounds ((i % cols) * cw, (i / cols) * ch, cw, ch); ++i; }
    }
    int heightFor (int width) const { const int cols = juce::jmax (1, width / 84); return (((int) knobs.size() + cols - 1) / cols) * 92; }
private:
    std::function<ParamMap*()> getMap;
    std::vector<std::unique_ptr<Knob>> knobs;
};

class MeterBar : public juce::Component
{
public:
    void setLevel (float l, float r)
    {
        const float dec = 0.86f;
        lvl[0] = juce::jmax (l, lvl[0] * dec); lvl[1] = juce::jmax (r, lvl[1] * dec);
        if (l > hold[0]) { hold[0] = l; holdT[0] = 30; } else if (--holdT[0] < 0) hold[0] *= 0.95f;
        if (r > hold[1]) { hold[1] = r; holdT[1] = 30; } else if (--holdT[1] < 0) hold[1] *= 0.95f;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r, 2.f);
        const float w = r.getWidth() / 2.f - 1.f;
        for (int c = 0; c < 2; ++c)
        {
            auto bar = juce::Rectangle<float> (r.getX() + c * (w + 2.f), r.getY() + 1.f, w, r.getHeight() - 2.f);
            const float db = juce::Decibels::gainToDecibels (lvl[c], -60.f);
            const float f = juce::jlimit (0.f, 1.f, (db + 60.f) / 63.f);
            auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * f);
            g.setGradientFill (juce::ColourGradient (lvl[c] > 1.0f ? juce::Colours::red : Theme::accent, 0, bar.getY(), Theme::accent.darker (0.7f), 0, bar.getBottom(), false));
            g.fillRect (fill);
            const float hf = juce::jlimit (0.f, 1.f, (juce::Decibels::gainToDecibels (hold[c], -60.f) + 60.f) / 63.f);
            g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * hf, bar.getWidth(), 1.5f);
        }
    }
private:
    float lvl[2] { 0, 0 }, hold[2] { 0, 0 }; int holdT[2] { 0, 0 };
};

//------------------------------------------------------------------------------ presets (original to NOIZE)
struct Preset { const char* name; std::map<juce::String, float> over; };
inline std::vector<Preset> presetsFor (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::PolySynth: return {
            { "Neon Pad", { {"wave1",0}, {"wave2",0}, {"detune",0.3f}, {"attack",0.6f}, {"release",1.2f}, {"cutoff",3500}, {"sustain",0.8f} } },
            { "Chrome Lead", { {"wave1",1}, {"wave2",0}, {"detune",0.15f}, {"attack",0.005f}, {"decay",0.15f}, {"sustain",0.6f}, {"cutoff",8000} } },
            { "Glass Pluck", { {"wave1",2}, {"wave2",3}, {"attack",0.002f}, {"decay",0.25f}, {"sustain",0.f}, {"release",0.3f}, {"cutoff",6000} } },
            { "Warehouse Stab", { {"wave1",0}, {"wave2",1}, {"detune",0.4f}, {"attack",0.003f}, {"decay",0.18f}, {"sustain",0.2f}, {"cutoff",2500}, {"resonance",0.5f} } } };
        case InstrumentType::SubSynth: return {
            { "Deep Sub", { {"wave1",3}, {"wave2",3}, {"mix2",0.2f}, {"cutoff",600}, {"filterEnv",300}, {"resonance",0.1f} } },
            { "Acid Squelch", { {"wave1",0}, {"wave2",1}, {"cutoff",700}, {"resonance",0.8f}, {"filterEnv",5000}, {"decay",0.2f}, {"sustain",0.1f} } },
            { "Dark Drone", { {"wave1",0}, {"wave2",0}, {"detune",0.5f}, {"attack",1.f}, {"release",2.f}, {"cutoff",1200}, {"sustain",1.f} } } };
        case InstrumentType::BassSynth: return {
            { "Concrete Bass", { {"wave1",0}, {"wave2",2}, {"cutoff",700}, {"filterEnv",1500}, {"resonance",0.4f} } },
            { "Rumble Sine", { {"wave1",3}, {"wave2",3}, {"cutoff",400}, {"filterEnv",0}, {"sustain",0.9f}, {"release",0.2f} } },
            { "Reese Wall", { {"wave1",0}, {"wave2",0}, {"detune",0.45f}, {"cutoff",1100}, {"filterEnv",600}, {"sustain",0.8f} } } };
        case InstrumentType::DrumMachine: return {
            { "Club Kit", { {"kickTune",50}, {"kickDecay",0.4f}, {"snareTone",0.5f}, {"hatDecay",0.05f} } },
            { "Hard Techno", { {"kickTune",42}, {"kickDecay",0.6f}, {"snareTone",0.8f}, {"hatDecay",0.08f} } },
            { "Tight House", { {"kickTune",58}, {"kickDecay",0.25f}, {"snareTone",0.3f}, {"hatDecay",0.03f} } } };
        case InstrumentType::Sampler: return { { "Default", {} }, { "Soft Attack", { {"attack",0.3f}, {"release",0.8f} } } };
        case InstrumentType::Piano: return {
            { "Studio Grand", { {"brightness",0.5f}, {"decay",2.f} } },
            { "Felt Piano", { {"brightness",0.2f}, {"decay",1.2f} } },
            { "Bright Upright", { {"brightness",0.85f}, {"decay",1.f} } } };
    }
    return {};
}

inline juce::StringArray instrumentNames() { return { "Polysynth", "Subtractive Synth", "Bass Synth", "Drum Machine", "Sampler", "Piano" }; }
inline juce::StringArray effectNames() { return { "Parametric EQ", "Compressor", "Limiter", "Reverb", "Delay", "Chorus", "Distortion", "Low-pass Filter", "High-pass Filter", "Stereo Width", "Gain" }; }

/** Popup window with a parameter panel for one effect slot. */
class FxEditorWindow : public juce::DocumentWindow
{
public:
    FxEditorWindow (AppContext& c, const juce::String& title, std::function<ParamMap*()> map, bool fx, std::function<void()> onClose)
        : DocumentWindow (title, Theme::panel, DocumentWindow::closeButton), ctx (c), closeCb (std::move (onClose))
    {
        setUsingNativeTitleBar (false);
        panel.beginEdit = [this] { ctx.beginEdit(); };
        panel.changed = [this] { ctx.touch(); };
        panel.setParams (std::move (map), fx);
        const int w = 84 * 4, h = juce::jmax (100, panel.heightFor (w)) + 10;
        panel.setSize (w, h);
        setContentNonOwned (&panel, true);
        centreWithSize (w + 4, h + 30);
        setVisible (true);
        setAlwaysOnTop (true);
    }
    void closeButtonPressed() override { setVisible (false); if (closeCb) closeCb(); }
private:
    AppContext& ctx; ParamPanel panel; std::function<void()> closeCb;
};
}
