#pragma once
#include "ViewBase.h"

namespace noize
{
/** FL-style channel rack: channel strips on the left, step sequencer for the selected channel, patterns = session slots. */
class ChannelRackView : public ViewBase
{
public:
    explicit ChannelRackView (AppContext& c) : ViewBase (c)
    {
        for (int i = 0; i < 8; ++i)
        {
            auto* b = patBtns.add (new juce::TextButton (juce::String (i + 1)));
            b->setClickingTogglesState (false); addAndMakeVisible (b);
            b->onClick = [this, i] { ctx.pattern = i; ensureTarget(); ctx.sendChangeMessage(); };
        }
        auto mk = [this] (juce::TextButton& b, const char* t) { b.setButtonText (t); addAndMakeVisible (b); };
        mk (dupBtn, "Duplicate pattern"); mk (clearBtn, "Clear"); mk (toArrBtn, "Place on timeline"); mk (addCh, "+ Channel"); mk (openRoll, "Piano Roll");
        stepsBox.addItem ("16 steps (1 bar)", 1); stepsBox.addItem ("32 steps (2 bars)", 2); stepsBox.addItem ("64 steps (4 bars)", 3); stepsBox.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (stepsBox);
        octBox.addItem ("Octave C2", 3); octBox.addItem ("Octave C3", 4); octBox.addItem ("Octave C4", 5); octBox.addItem ("Octave C5", 6); octBox.setSelectedId (4, juce::dontSendNotification); addAndMakeVisible (octBox);
        octBox.onChange = [this] { repaint(); };
        stepsBox.onChange = [this] { setSteps (stepsBox.getSelectedId() == 1 ? 16 : stepsBox.getSelectedId() == 2 ? 32 : 64); };
        dupBtn.onClick = [this] { duplicatePattern(); };
        clearBtn.onClick = [this] { ctx.edit ([&] { for (auto& t : ctx.project().tracks) { AppContext::ensureSlots (t); t.slots[(size_t) ctx.pattern].notes.clear(); } }); };
        toArrBtn.onClick = [this] { placeOnTimeline(); };
        openRoll.onClick = [this] { ensureTarget(); if (onOpenEditor) onOpenEditor(); };
        addCh.onClick = [this]
        {
            juce::PopupMenu m; auto n = instrumentNames(); for (int i = 0; i < n.size(); ++i) m.addItem (i + 1, n[i]);
            m.showMenuAsync ({}, [this] (int r) { if (r > 0) ctx.edit ([&] { auto& t = ctx.project().addTrack (instrumentNames()[r - 1], false, (InstrumentType) (r - 1)); ctx.selectedTrack = t.id; }); });
        };
        startUiTimer (20);
    }
    std::function<void()> onOpenEditor;
    void tick() override { if (ctx.engine.isPlaying()) repaint(); }
    void activated() override { ensureTarget(); }
    void projectChanged() override { ensureTarget(); repaint(); }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (topH).reduced (6, 6);
        top.removeFromLeft (70);
        for (auto* b : patBtns) { b->setBounds (top.removeFromLeft (34)); top.removeFromLeft (3); }
        top.removeFromLeft (8);
        for (auto* b : { &dupBtn, &clearBtn, &toArrBtn, &openRoll }) { b->setBounds (top.removeFromLeft (b == &dupBtn || b == &toArrBtn ? 130 : 80)); top.removeFromLeft (4); }
        stepsBox.setBounds (top.removeFromLeft (140)); top.removeFromLeft (4); octBox.setBounds (top.removeFromLeft (110));
        addCh.setBounds (6, getHeight() - 34, chW - 12, 28);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        for (int i = 0; i < 8; ++i) patBtns[i]->setToggleState (i == ctx.pattern, juce::dontSendNotification);
        g.setColour (Theme::dim); g.setFont (Theme::font (11.f, true)); g.drawText ("PATTERN", 8, 0, 70, topH, juce::Justification::centredLeft);
        auto& proj = ctx.project();
        // channel list
        for (size_t i = 0; i < proj.tracks.size(); ++i)
        {
            auto& t = proj.tracks[i]; auto r = chRect ((int) i);
            const bool sel = t.id == ctx.selectedTrack;
            paintPanel (g, r.toFloat(), {}, 4.f);
            if (sel) { g.setColour (Theme::accent); g.drawRoundedRectangle (r.toFloat().reduced (1.f), 4.f, 1.5f); }
            g.setColour (Theme::trackColour ((int) i)); g.fillRect (r.getX() + 2, r.getY() + 4, 4, r.getHeight() - 8);
            g.setColour (Theme::text); g.setFont (Theme::font (13.f, true)); g.drawText (t.name, r.getX() + 12, r.getY() + 3, 90, 18, juce::Justification::centredLeft);
            g.setColour (Theme::dim); g.setFont (Theme::font (10.f)); g.drawText (t.audioTrack ? "Audio" : instrumentNames()[(int) t.instrument], r.getX() + 12, r.getY() + 20, 90, 14, juce::Justification::centredLeft);
            auto mr = juce::Rectangle<int> (r.getRight() - 28, r.getY() + 6, 22, 20);
            g.setColour (t.mute ? juce::Colour (0xffff9f43) : Theme::panel); g.fillRoundedRectangle (mr.toFloat(), 3.f);
            g.setColour (t.mute ? juce::Colours::black : Theme::text); g.setFont (Theme::font (11.f, true)); g.drawText ("M", mr, juce::Justification::centred);
            // mini pan/volume bars
            auto vr = juce::Rectangle<int> (r.getX() + 110, r.getY() + 22, r.getWidth() - 150, 6);
            g.setColour (Theme::bg); g.fillRect (vr); g.setColour (Theme::accent); g.fillRect (vr.withWidth ((int) (vr.getWidth() * t.volume / 1.25f)));
            if (! t.audioTrack) { AppContext::ensureSlots (t); const auto& cl = t.slots[(size_t) ctx.pattern]; g.setColour (cl.notes.empty() ? Theme::edge : Theme::accent); g.fillEllipse ((float) r.getRight() - 40, (float) r.getY() + 12, 6.f, 6.f); }
        }
        auto* t = ctx.selTrack();
        auto area = seqArea();
        paintPanel (g, area.toFloat().expanded (4), t ? t->name + "  -  pattern " + juce::String (ctx.pattern + 1) : juce::String ("no channel"), 6.f);
        if (t == nullptr || t->audioTrack) { g.setColour (Theme::dim); g.drawText ("Select a MIDI/instrument channel to sequence it.", area, juce::Justification::centred); return; }
        AppContext::ensureSlots (*t);
        auto& clip = t->slots[(size_t) ctx.pattern];
        const int steps = juce::jmax (1, (int) std::round (clip.lengthBeats * 4)); const auto rows = rowPitches (*t); const int labW = 78;
        const float sw = juce::jmin (46.f, (float) (area.getWidth() - labW) / (float) steps);
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const int y = area.getY() + 20 + (int) r * rowH2;
            g.setColour (Theme::panel2); g.fillRoundedRectangle ((float) area.getX(), (float) y + 1, (float) labW - 6, (float) rowH2 - 3, 3.f);
            g.setColour (Theme::text); g.setFont (Theme::font (12.f, true));
            g.drawText (t->instrument == InstrumentType::DrumMachine ? drumName (rows[r]) : noteName (rows[r]), area.getX() + 6, y, labW - 10, rowH2, juce::Justification::centredLeft);
            for (int s = 0; s < steps; ++s)
            {
                auto cell = juce::Rectangle<float> (area.getX() + labW + s * sw, (float) y + 1, sw - 3.f, (float) rowH2 - 3.f);
                const bool on = hasNote (clip, rows[r], s), group4 = (s / 4) % 2 == 0;
                if (on) { if (Theme::quality > 0) g.setGradientFill (juce::ColourGradient (Theme::accent.brighter (0.2f), 0, cell.getY(), Theme::accent.darker (0.3f), 0, cell.getBottom(), false)); else g.setColour (Theme::accent); }
                else g.setColour (group4 ? Theme::panel2.brighter (0.08f) : Theme::panel2.darker (0.1f));
                g.fillRoundedRectangle (cell, 3.f);
                g.setColour (on ? Theme::accent.brighter() : Theme::edge); g.drawRoundedRectangle (cell, 3.f, 1.f);
            }
        }
        if (ctx.engine.isPlaying() && ! rows.empty())
        {
            const double pb = std::fmod (ctx.engine.getPosition(), clip.lengthBeats); const int cur = (int) (pb * 4);
            g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect ((float) area.getX() + labW + cur * sw, (float) area.getY() + 16, sw - 3.f, 3.f);
        }
        g.setColour (Theme::dim); g.setFont (Theme::font (11.f)); g.drawText ("Click steps to toggle. Right-click a step to set accent velocity.", area.getX(), area.getBottom() - 16, area.getWidth(), 14, juce::Justification::left);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto p = e.getPosition(); auto& proj = ctx.project();
        for (size_t i = 0; i < proj.tracks.size(); ++i)
            if (chRect ((int) i).contains (p))
            {
                auto& t = proj.tracks[i]; auto r = chRect ((int) i);
                if (e.mods.isPopupMenu()) { chMenu (t); return; }
                if (juce::Rectangle<int> (r.getRight() - 28, r.getY() + 6, 22, 20).contains (p)) { ctx.edit ([&] { t.mute = ! t.mute; }); return; }
                if (juce::Rectangle<int> (r.getX() + 110, r.getY() + 16, r.getWidth() - 150, 16).contains (p))
                { volTrack = t.id; ctx.beginEdit(); setVol (p.x, r); return; }
                ctx.selectTrack (t.id); ensureTarget();
                if (! t.audioTrack) { ctx.engine.setLiveTrack (t.id); ctx.engine.injectMidi (juce::MidiMessage::noteOn (1, 60, 0.7f)); juce::Timer::callAfterDelay (150, [this] { ctx.engine.injectMidi (juce::MidiMessage::noteOff (1, 60)); }); }
                return;
            }
        auto* t = ctx.selTrack(); if (! t || t->audioTrack) return;
        AppContext::ensureSlots (*t); auto& clip = t->slots[(size_t) ctx.pattern];
        auto area = seqArea(); const int labW = 78;
        const int steps = juce::jmax (1, (int) std::round (clip.lengthBeats * 4)); const float sw = juce::jmin (46.f, (float) (area.getWidth() - labW) / (float) steps);
        const auto rows = rowPitches (*t);
        const int row = (p.y - area.getY() - 20) / rowH2, step = (int) ((p.x - area.getX() - labW) / sw);
        if (p.x < area.getX() + labW || row < 0 || row >= (int) rows.size() || step < 0 || step >= steps) return;
        const int pitch = rows[(size_t) row];
        ctx.edit ([&]
        {
            const double sb = step * 0.25;
            auto it = std::find_if (clip.notes.begin(), clip.notes.end(), [&] (const Note& n) { return n.pitch == pitch && std::abs (n.startBeat - sb) < 0.01; });
            if (e.mods.isPopupMenu()) { if (it != clip.notes.end()) it->velocity = it->velocity > 0.95f ? 0.6f : 1.0f; }
            else if (it != clip.notes.end()) clip.notes.erase (it);
            else { clip.notes.push_back ({ pitch, sb, 0.22, 0.9f }); ctx.engine.setLiveTrack (t->id); ctx.engine.injectMidi (juce::MidiMessage::noteOn (1, pitch, 0.8f)); juce::Timer::callAfterDelay (120, [this, pitch] { ctx.engine.injectMidi (juce::MidiMessage::noteOff (1, pitch)); }); }
        });
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (volTrack < 0) return;
        auto& proj = ctx.project();
        for (size_t i = 0; i < proj.tracks.size(); ++i) if (proj.tracks[i].id == volTrack) setVol (e.x, chRect ((int) i));
    }
    void mouseUp (const juce::MouseEvent&) override { volTrack = -1; }

private:
    static constexpr int topH = 40, chW = 270, chH = 38, rowH2 = 30;
    juce::OwnedArray<juce::TextButton> patBtns; juce::TextButton dupBtn, clearBtn, toArrBtn, addCh, openRoll; juce::ComboBox stepsBox, octBox; int volTrack = -1;
    juce::Rectangle<int> chRect (int i) const { return { 6, topH + 4 + i * (chH + 3), chW - 12, chH }; }
    juce::Rectangle<int> seqArea() const { return { chW + 10, topH + 24, getWidth() - chW - 24, getHeight() - topH - 50 }; }
    void setVol (int x, juce::Rectangle<int> r)
    {
        auto* t = ctx.project().findTrack (volTrack); if (! t) return;
        const float f = juce::jlimit (0.f, 1.f, (float) (x - (r.getX() + 110)) / (float) (r.getWidth() - 150));
        t->volume = f * 1.25f; ctx.touch();
    }
    static bool hasNote (const Clip& c, int pitch, int step) { for (auto& n : c.notes) if (n.pitch == pitch && std::abs (n.startBeat - step * 0.25) < 0.01) return true; return false; }
    static juce::String drumName (int p) { switch (p) { case 36: return "Kick"; case 38: return "Snare"; case 39: return "Clap"; case 42: return "Hat"; case 46: return "Open Hat"; case 49: return "Crash"; case 45: return "Tom"; } return noteName (p); }
    std::vector<int> rowPitches (const Track& t) const
    {
        if (t.instrument == InstrumentType::DrumMachine) return { 36, 38, 39, 42, 46, 49, 45 };
        const int base = (octBox.getSelectedId() + 0) * 12 + 12;   // id 3 -> C2 (36)
        std::vector<int> v; for (int i = 11; i >= 0; --i) v.push_back (base - 12 + i); return v;
    }
    void ensureTarget()
    {
        if (auto* t = ctx.selTrack()) { if (! t->audioTrack) { AppContext::ensureSlots (*t); ctx.target = { t->id, true, ctx.pattern }; } }
    }
    void setSteps (int steps)
    {
        ctx.edit ([&] { for (auto& t : ctx.project().tracks) { AppContext::ensureSlots (t); auto& c = t.slots[(size_t) ctx.pattern]; c.lengthBeats = steps / 4.0; if (c.name == "Clip") c.name = "Pattern"; } });
    }
    void duplicatePattern()
    {
        int dst = -1; auto& proj = ctx.project();
        for (int s = 0; s < 8 && dst < 0; ++s) { bool empty = true; for (auto& t : proj.tracks) { AppContext::ensureSlots (t); if (! t.slots[(size_t) s].notes.empty()) empty = false; } if (empty) dst = s; }
        if (dst < 0) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Duplicate pattern", "All 8 pattern slots are in use."); return; }
        ctx.edit ([&] { for (auto& t : proj.tracks) { t.slots[(size_t) dst] = t.slots[(size_t) ctx.pattern]; } });
        ctx.pattern = dst; ensureTarget(); ctx.sendChangeMessage();
    }
    void placeOnTimeline()
    {
        const double at = std::floor (ctx.engine.getPosition() / 4.0) * 4.0;
        ctx.edit ([&]
        {
            double maxEnd = 0;
            for (auto& t : ctx.project().tracks)
            {
                AppContext::ensureSlots (t); auto c = t.slots[(size_t) ctx.pattern];
                if (c.notes.empty()) continue;
                c.startBeat = at; c.name = "Pattern " + juce::String (ctx.pattern + 1); t.clips.push_back (c); maxEnd = juce::jmax (maxEnd, at + c.lengthBeats);
            }
            ctx.project().lengthBeats = juce::jmax (ctx.project().lengthBeats, maxEnd + 4.0);
        });
        ctx.engine.setPosition (at + (ctx.project().tracks.empty() ? 4.0 : juce::jmax (4.0, ctx.project().tracks[0].slots[(size_t) ctx.pattern].lengthBeats)));
    }
    void chMenu (Track& t)
    {
        juce::PopupMenu m, inst; auto n = instrumentNames(); for (int i = 0; i < n.size(); ++i) inst.addItem (100 + i, n[i], true, (int) t.instrument == i);
        m.addSubMenu ("Instrument", inst); m.addItem (1, "Delete channel");
        const int id = t.id;
        m.showMenuAsync ({}, [this, id] (int r) { auto* t = ctx.project().findTrack (id); if (! t || r == 0) return;
            if (r == 1) { ctx.edit ([&] { ctx.project().removeTrack (id); }); ctx.selectedTrack = -1; ctx.target = {}; }
            else ctx.edit ([&] { t->instrument = (InstrumentType) (r - 100); t->instrumentParams = defaultParams (t->instrument); }); });
    }
};
}
