#pragma once
#include "ViewBase.h"

namespace noize
{
class SynthView : public ViewBase, private juce::MidiKeyboardState::Listener
{
public:
    explicit SynthView (AppContext& c) : ViewBase (c), keyboard (kbState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        kbState.addListener (this);
        keyboard.setKeyWidth (34.f); keyboard.setAvailableRange (24, 96); keyboard.setOctaveForMiddleC (4);
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent.withAlpha (0.8f));
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::accent.withAlpha (0.3f));
        addAndMakeVisible (keyboard);
        for (auto n : instrumentNames()) typeBox.addItem (n, typeBox.getNumItems() + 1);
        addAndMakeVisible (typeBox); addAndMakeVisible (presetBox); addAndMakeVisible (loadSample); loadSample.setButtonText ("Load sample...");
        typeBox.onChange = [this]
        {
            auto* t = ctx.selTrack(); if (! t || t->audioTrack || rebuilding) return;
            const auto ty = (InstrumentType) (typeBox.getSelectedId() - 1);
            if (ty != t->instrument) ctx.edit ([&] { t->instrument = ty; t->instrumentParams = defaultParams (ty); });
        };
        presetBox.onChange = [this]
        {
            auto* t = ctx.selTrack(); if (! t || t->audioTrack || rebuilding || presetBox.getSelectedId() < 1) return;
            auto list = presetsFor (t->instrument); const int i = presetBox.getSelectedId() - 1; if (i >= (int) list.size()) return;
            ctx.edit ([&] { t->instrumentParams = defaultParams (t->instrument); for (auto& kv : list[(size_t) i].over) t->instrumentParams[kv.first] = kv.second; });
        };
        loadSample.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            { auto f = fc.getResult(); if (auto* t = ctx.selTrack()) if (f.existsAsFile()) ctx.edit ([&] { t->samplerFile = f.getFullPathName(); }); });
        };
        startUiTimer (30);
        rebuild();
    }
    ~SynthView() override { kbState.removeListener (this); }

    void activated() override { if (auto* t = ctx.selTrack()) ctx.engine.setLiveTrack (t->id); rebuild(); }
    void projectChanged() override { rebuild(); repaint(); }
    void tick() override { phase += 0.03f; repaint (graphArea()); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (40).reduced (8, 6);
        typeBox.setBounds (top.removeFromLeft (170)); top.removeFromLeft (6); presetBox.setBounds (top.removeFromLeft (170)); top.removeFromLeft (6); loadSample.setBounds (top.removeFromLeft (120));
        keyboard.setBounds (r.removeFromBottom (92).reduced (8, 6));
        layoutPanels();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        auto* t = ctx.selTrack();
        if (t == nullptr || t->audioTrack) { g.setColour (Theme::dim); g.setFont (Theme::font (14.f)); g.drawText ("Select an instrument track (Arrangement / Channel Rack) to edit its sound.", getLocalBounds(), juce::Justification::centred); return; }
        const bool sub = isSubtractive (t->instrument);
        const auto p = t->instrumentParams;
        auto get = [&] (const char* k, float d) { auto it = p.find (k); return it == p.end() ? d : it->second; };
        for (auto& pr : panelRects()) paintPanel (g, pr.first.toFloat(), pr.second, 8.f);
        if (sub)
        {
            auto o1 = oscGraph (0), o2 = oscGraph (1);
            drawOsc (g, o1, (int) get ("wave1", 0), 1.f); drawOsc (g, o2, (int) get ("wave2", 0), get ("mix2", 0.5f) + 0.25f);
            drawFilter (g, filterGraph(), get ("cutoff", 5000), get ("resonance", 0.3f));
            drawAdsr (g, envGraph(), get ("attack", 0.01f), get ("decay", 0.2f), get ("sustain", 0.7f), get ("release", 0.3f));
        }
        else if (t->instrument == InstrumentType::Sampler)
        {
            auto gr = oscGraph (0).withWidth (getWidth() - 60);
            drawSample (g, gr, t->samplerFile);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* t = ctx.selTrack(); if (! t || ! isSubtractive (t->instrument)) return;
        for (int o = 0; o < 2; ++o)
        {
            auto gr = oscGraph (o);
            for (int w = 0; w < 4; ++w)
                if (waveBtn (gr, w).contains (e.getPosition())) { ctx.edit ([&] { t->instrumentParams[o == 0 ? "wave1" : "wave2"] = (float) w; }); return; }
        }
    }

private:
    juce::MidiKeyboardState kbState; juce::MidiKeyboardComponent keyboard;
    juce::ComboBox typeBox, presetBox; juce::TextButton loadSample; bool rebuilding = false; float phase = 0.f;
    ParamPanel oscPanel, filtPanel, envPanel, outPanel, genericPanel; std::unique_ptr<juce::FileChooser> chooser;
    int builtTrack = -1; InstrumentType builtType = InstrumentType::PolySynth;

    static bool isSubtractive (InstrumentType t) { return t == InstrumentType::PolySynth || t == InstrumentType::SubSynth || t == InstrumentType::BassSynth; }
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override { ctx.engine.injectMidi (juce::MidiMessage::noteOn (1, note, vel)); }
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override { ctx.engine.injectMidi (juce::MidiMessage::noteOff (1, note)); }

    juce::Rectangle<int> content() const { return getLocalBounds().withTrimmedTop (44).withTrimmedBottom (100).reduced (8, 4); }
    juce::Rectangle<int> graphArea() const { return content(); }
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> panelRects() const
    {
        auto* t = ctx.engine.project().findTrack (ctx.selectedTrack);
        auto c = content(); std::vector<std::pair<juce::Rectangle<int>, juce::String>> v;
        if (! t) return v;
        if (isSubtractive (t->instrument))
        {
            const int h = c.getHeight() / 2 - 4, w = c.getWidth() / 3 - 6;
            v.push_back ({ { c.getX(), c.getY(), w * 2 + 6, h }, "Oscillators" });
            v.push_back ({ { c.getX() + w * 2 + 12, c.getY(), w, h }, "Output" });
            v.push_back ({ { c.getX(), c.getY() + h + 8, w, h }, "Filter" });
            v.push_back ({ { c.getX() + w + 6, c.getY() + h + 8, w * 2 + 6, h }, "Envelope" });
        }
        else v.push_back ({ c, instrumentNames()[(int) t->instrument] + (t->instrument == InstrumentType::Sampler ? "  -  " + (t->samplerFile.isEmpty() ? juce::String ("no sample loaded") : juce::File (t->samplerFile).getFileName()) : juce::String()) });
        return v;
    }
    juce::Rectangle<int> oscGraph (int o) const { auto r = panelRects()[0].first; const int w = (r.getWidth() - 36) / 2 - 90; return { r.getX() + 12 + o * ((r.getWidth() - 24) / 2), r.getY() + 24, juce::jmax (60, w), r.getHeight() - 70 }; }
    juce::Rectangle<int> waveBtn (juce::Rectangle<int> g, int w) const { return { g.getX() + w * (g.getWidth() / 4), g.getBottom() + 6, g.getWidth() / 4 - 4, 22 }; }
    juce::Rectangle<int> filterGraph() const { auto r = panelRects()[2].first; return { r.getX() + 10, r.getY() + 24, r.getWidth() - 20, r.getHeight() - 24 - 110 }; }
    juce::Rectangle<int> envGraph() const { auto r = panelRects()[3].first; return { r.getX() + 10, r.getY() + 24, r.getWidth() - 20, r.getHeight() - 24 - 110 }; }

    void layoutPanels()
    {
        auto* t = ctx.selTrack(); for (auto* p : { &oscPanel, &filtPanel, &envPanel, &outPanel, &genericPanel }) p->setVisible (false);
        if (! t || t->audioTrack) return;
        auto rects = panelRects();
        if (isSubtractive (t->instrument))
        {
            auto o = rects[0].first; oscPanel.setBounds (o.getX() + o.getWidth() / 2 - 100, o.getBottom() - 100, 200, 94);
            // oscillator knobs placed to the right of each graph
            outPanel.setBounds (rects[1].first.reduced (6).withTrimmedTop (22));
            filtPanel.setBounds (rects[2].first.getX() + 4, rects[2].first.getBottom() - 100, rects[2].first.getWidth() - 8, 96);
            envPanel.setBounds (rects[3].first.getX() + 4, rects[3].first.getBottom() - 100, rects[3].first.getWidth() - 8, 96);
            for (auto* p : { &oscPanel, &filtPanel, &envPanel, &outPanel }) p->setVisible (true);
        }
        else { genericPanel.setBounds (rects[0].first.reduced (10).withTrimmedTop (22).withTrimmedBottom (t->instrument == InstrumentType::Sampler ? 0 : 0).withTrimmedTop (t->instrument == InstrumentType::Sampler ? 150 : 0)); genericPanel.setVisible (true); }
    }

    void rebuild()
    {
        auto* t = ctx.selTrack();
        const int id = t ? t->id : -1;
        if (t && ! t->audioTrack && (id != builtTrack || t->instrument != builtType || builtParamCount != (int) t->instrumentParams.size()))
        {
            builtTrack = id; builtType = t->instrument; builtParamCount = (int) t->instrumentParams.size();
            auto getter = [this, id] () -> ParamMap* { auto* tt = ctx.project().findTrack (id); return tt ? &tt->instrumentParams : nullptr; };
            for (auto* p : { &oscPanel, &filtPanel, &envPanel, &outPanel, &genericPanel })
            {
                if (p->getParentComponent() == nullptr) addAndMakeVisible (p);
                p->beginEdit = [this] { ctx.beginEdit(); }; p->changed = [this] { ctx.engine.commit(); ctx.engine.markDirty(); repaint(); };
            }
            if (isSubtractive (t->instrument))
            {
                oscPanel.setParams (getter, false, { "detune", "mix2" }); filtPanel.setParams (getter, false, { "cutoff", "resonance", "filterEnv" });
                envPanel.setParams (getter, false, { "attack", "decay", "sustain", "release" }); outPanel.setParams (getter, false, { "gain" });
            }
            else genericPanel.setParams (getter, false);
        }
        rebuilding = true;
        if (t && ! t->audioTrack)
        {
            typeBox.setSelectedId ((int) t->instrument + 1, juce::dontSendNotification);
            presetBox.clear (juce::dontSendNotification); int i = 1; for (auto& p : presetsFor (t->instrument)) presetBox.addItem (p.name, i++);
            presetBox.setTextWhenNothingSelected ("Presets");
        }
        typeBox.setEnabled (t && ! t->audioTrack); presetBox.setEnabled (t && ! t->audioTrack);
        loadSample.setVisible (t && t->instrument == InstrumentType::Sampler);
        rebuilding = false;
        layoutPanels();
    }
    int builtParamCount = -1;

    void drawOsc (juce::Graphics& g, juce::Rectangle<int> r, int wave, float level)
    {
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r.toFloat(), 4.f);
        g.setColour (Theme::edge); g.drawRoundedRectangle (r.toFloat(), 4.f, 1.f);
        const int layers = Theme::quality == 2 ? 5 : (Theme::quality == 1 ? 2 : 1);
        for (int l = layers - 1; l >= 0; --l)          // stacked layers give the waveform dimensional depth
        {
            juce::Path p; const float off = l * 3.f;
            for (int x = 0; x < r.getWidth(); ++x)
            {
                float ph = std::fmod ((float) x / (float) r.getWidth() * 2.f + (Theme::quality > 0 ? phase * 0.15f : 0.f) - l * 0.02f, 1.f); if (ph < 0) ph += 1.f;
                float s = 0; switch (wave) { case 0: s = 2.f * ph - 1.f; break; case 1: s = ph < 0.5f ? 1.f : -1.f; break; case 2: s = 4.f * std::abs (ph - 0.5f) - 1.f; break; default: s = std::sin (ph * juce::MathConstants<float>::twoPi); }
                const float y = (float) r.getCentreY() - s * level * (float) r.getHeight() * 0.38f + off, xx = (float) r.getX() + x + off * 0.6f;
                if (x == 0) p.startNewSubPath (xx, y); else p.lineTo (xx, y);
            }
            g.setColour (l == 0 ? Theme::accent : Theme::accent.withAlpha (0.12f + 0.04f * (float) (layers - l))); g.strokePath (p, juce::PathStrokeType (l == 0 ? 2.f : 1.f));
        }
        static const char* nm[] = { "SAW", "SQR", "TRI", "SIN" };
        for (int w = 0; w < 4; ++w)
        {
            auto b = waveBtn (r, w); g.setColour (w == wave ? Theme::accent : Theme::panel2); g.fillRoundedRectangle (b.toFloat(), 3.f);
            g.setColour (w == wave ? juce::Colours::black : Theme::text); g.setFont (Theme::font (11.f, true)); g.drawText (nm[w], b, juce::Justification::centred);
        }
    }
    void drawFilter (juce::Graphics& g, juce::Rectangle<int> r, float fc, float res)
    {
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r.toFloat(), 4.f);
        juce::Path p; const float q = 0.6f + res * 7.f;
        for (int x = 0; x < r.getWidth(); ++x)
        {
            const float f = 20.f * std::pow (1000.f, (float) x / (float) r.getWidth()); const float u = f / juce::jmax (20.f, fc);
            const float m = 1.f / std::sqrt ((1.f - u * u) * (1.f - u * u) + (u / q) * (u / q));
            const float db = juce::jlimit (-36.f, 18.f, 20.f * std::log10 (juce::jmax (1e-4f, m)));
            const float y = (float) r.getBottom() - (db + 36.f) / 54.f * (float) r.getHeight();
            if (x == 0) p.startNewSubPath ((float) r.getX(), y); else p.lineTo ((float) (r.getX() + x), y);
        }
        juce::Path fill = p; fill.lineTo ((float) r.getRight(), (float) r.getBottom()); fill.lineTo ((float) r.getX(), (float) r.getBottom()); fill.closeSubPath();
        g.setColour (Theme::accent.withAlpha (0.15f)); g.fillPath (fill); g.setColour (Theme::accent); g.strokePath (p, juce::PathStrokeType (2.f));
    }
    void drawAdsr (juce::Graphics& g, juce::Rectangle<int> r, float a, float d, float s, float rel)
    {
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r.toFloat(), 4.f);
        const float total = a + d + 0.6f + rel; const float W = (float) r.getWidth() - 8.f, H = (float) r.getHeight() - 8.f, x0 = (float) r.getX() + 4.f, yb = (float) r.getBottom() - 4.f;
        juce::Path p; p.startNewSubPath (x0, yb);
        float x = x0 + a / total * W; p.lineTo (x, yb - H);
        x += d / total * W; p.lineTo (x, yb - s * H);
        x += 0.6f / total * W; p.lineTo (x, yb - s * H);
        x += rel / total * W; p.lineTo (x, yb);
        juce::Path fill = p; fill.closeSubPath(); g.setColour (Theme::accent.withAlpha (0.15f)); g.fillPath (fill);
        g.setColour (Theme::accent); g.strokePath (p, juce::PathStrokeType (2.f, juce::PathStrokeType::curved));
    }
    void drawSample (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& file)
    {
        r = panelRects()[0].first.reduced (10).withTrimmedTop (24).withHeight (120);
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r.toFloat(), 4.f);
        auto sd = ctx.engine.getAssets().get (file);
        if (! sd) { g.setColour (Theme::dim); g.drawText (file.isEmpty() ? "Load a sample, then play notes - pitch follows the root note." : "Sample missing or unreadable", r, juce::Justification::centred); return; }
        const float* d = sd->buffer.getReadPointer (0); const int len = sd->buffer.getNumSamples();
        g.setColour (Theme::accent);
        for (int x = 0; x < r.getWidth(); ++x)
        {
            const int i0 = (int) ((double) x / r.getWidth() * len), i1 = juce::jmin (len - 1, (int) ((double) (x + 1) / r.getWidth() * len)); float mx = 0;
            for (int i = i0; i <= i1; i += juce::jmax (1, (i1 - i0) / 32)) mx = juce::jmax (mx, std::abs (d[i]));
            g.drawVerticalLine (r.getX() + x, (float) r.getCentreY() - mx * r.getHeight() * 0.48f, (float) r.getCentreY() + mx * r.getHeight() * 0.48f);
        }
    }
};
}
