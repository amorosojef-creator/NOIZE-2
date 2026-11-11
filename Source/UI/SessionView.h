#pragma once
#include "ViewBase.h"

namespace noize
{
/** Clip-launching grid. Rows are scenes (= patterns), columns are tracks. */
class SessionView : public ViewBase
{
public:
    explicit SessionView (AppContext& c) : ViewBase (c) { startUiTimer (20); }
    std::function<void()> onOpenEditor;
    void tick() override { if (++frame % 8 == 0) blink = ! blink; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        auto& proj = ctx.project();
        g.setColour (Theme::dim); g.setFont (Theme::font (11.f)); g.drawText ("Click a clip to launch (quantised to the next bar). Double-click an empty slot to create a clip. Right-click for more.", 10, 4, getWidth() - 20, 16, juce::Justification::left);
        for (size_t ti = 0; ti < proj.tracks.size(); ++ti)
        {
            auto& t = proj.tracks[ti];
            auto hr = juce::Rectangle<int> (sceneW + (int) ti * colW, headTop, colW - 4, headH);
            paintPanel (g, hr.toFloat(), {}, 4.f);
            g.setColour (Theme::trackColour ((int) ti)); g.fillRect (hr.getX() + 1, hr.getY() + 3, 4, headH - 6);
            g.setColour (Theme::text); g.setFont (Theme::font (13.f, true)); g.drawText (t.name, hr.getX() + 10, hr.getY() + 2, hr.getWidth() - 40, 22, juce::Justification::centredLeft);
            g.setColour (Theme::dim); g.setFont (Theme::font (10.f)); g.drawText (t.audioTrack ? "Audio" : instrumentNames()[(int) t.instrument], hr.getX() + 10, hr.getY() + 22, hr.getWidth() - 20, 14, juce::Justification::centredLeft);
            auto sr = stopRect ((int) ti);
            g.setColour (Theme::panel2); g.fillRoundedRectangle (sr.toFloat(), 3.f); g.setColour (Theme::text); g.fillRect (sr.reduced (6));
            AppContext::ensureSlots (const_cast<Track&> (t));
            for (int s = 0; s < 8; ++s) paintSlot (g, t, (int) ti, s);
        }
        for (int s = 0; s < 8; ++s)
        {
            auto r = sceneRect (s);
            paintPanel (g, r.toFloat(), {}, 4.f);
            g.setColour (Theme::accent); juce::Path tri; tri.addTriangle ((float) r.getX() + 12, (float) r.getCentreY() - 8, (float) r.getX() + 12, (float) r.getCentreY() + 8, (float) r.getX() + 28, (float) r.getCentreY());
            g.fillPath (tri); g.setColour (Theme::text); g.setFont (Theme::font (13.f, true)); g.drawText ("Scene " + juce::String (s + 1), r.getX() + 34, r.getY(), r.getWidth() - 36, r.getHeight(), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto p = e.getPosition(); auto& proj = ctx.project();
        for (int s = 0; s < 8; ++s) if (sceneRect (s).contains (p)) { ctx.pattern = s; ctx.engine.getRenderer().launchScene (s); return; }
        for (size_t ti = 0; ti < proj.tracks.size(); ++ti)
        {
            if (stopRect ((int) ti).contains (p)) { ctx.engine.getRenderer().stopTrackSlot ((int) ti); return; }
            for (int s = 0; s < 8; ++s)
                if (slotRect ((int) ti, s).contains (p))
                {
                    auto& t = proj.tracks[ti]; AppContext::ensureSlots (t);
                    const bool has = ! t.slots[(size_t) s].notes.empty() || t.slots[(size_t) s].isAudio();
                    ctx.selectedTrack = t.id;
                    if (e.mods.isPopupMenu()) { slotMenu (t, (int) ti, s); return; }
                    if (has) { ctx.pattern = s; ctx.engine.getRenderer().launchSlot ((int) ti, s); }
                    return;
                }
        }
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto& proj = ctx.project();
        for (size_t ti = 0; ti < proj.tracks.size(); ++ti)
            for (int s = 0; s < 8; ++s)
                if (slotRect ((int) ti, s).contains (e.getPosition()))
                {
                    auto& t = proj.tracks[ti];
                    if (t.audioTrack) return;
                    ctx.engine.getRenderer().stopTrackSlot ((int) ti);
                    ctx.editClip (t.id, true, s);
                    if (onOpenEditor) onOpenEditor();
                }
    }

private:
    static constexpr int sceneW = 120, colW = 128, headTop = 26, headH = 54, rowH = 44, gridTop = headTop + headH + 6;
    bool blink = false; int frame = 0;
    juce::Rectangle<int> slotRect (int ti, int s) const { return { sceneW + ti * colW, gridTop + s * (rowH + 4), colW - 4, rowH }; }
    juce::Rectangle<int> sceneRect (int s) const { return { 6, gridTop + s * (rowH + 4), sceneW - 12, rowH }; }
    juce::Rectangle<int> stopRect (int ti) const { return { sceneW + ti * colW + colW - 34, headTop + 4, 24, 20 }; }

    void paintSlot (juce::Graphics& g, Track& t, int ti, int s)
    {
        auto r = slotRect (ti, s);
        auto& clip = t.slots[(size_t) s];
        const bool has = ! clip.notes.empty() || clip.isAudio();
        auto& rend = ctx.engine.getRenderer();
        const bool playing = rend.getActiveSlot (ti) == s, queued = rend.getQueuedSlot (ti) == s;
        const auto col = Theme::trackColour (ti);
        if (has)
        {
            if (Theme::quality > 0) g.setGradientFill (juce::ColourGradient (col.withAlpha (playing ? 0.9f : 0.5f), 0, (float) r.getY(), col.withAlpha (playing ? 0.6f : 0.25f), 0, (float) r.getBottom(), false));
            else g.setColour (col.withAlpha (playing ? 0.9f : 0.4f));
            g.fillRoundedRectangle (r.toFloat(), 4.f);
            g.setColour (playing ? juce::Colours::white : (queued && blink ? Theme::accent : col)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, playing || queued ? 2.f : 1.f);
            g.setColour (playing ? juce::Colours::black : Theme::text);
            juce::Path tri; tri.addTriangle ((float) r.getX() + 10, (float) r.getCentreY() - 7, (float) r.getX() + 10, (float) r.getCentreY() + 7, (float) r.getX() + 24, (float) r.getCentreY()); g.fillPath (tri);
            g.setFont (Theme::font (12.f, true)); g.drawText (clip.name + (clip.isAudio() ? "" : "  (" + juce::String (clip.notes.size()) + ")"), r.getX() + 30, r.getY(), r.getWidth() - 34, r.getHeight(), juce::Justification::centredLeft, true);
        }
        else { g.setColour (Theme::panel); g.fillRoundedRectangle (r.toFloat(), 4.f); g.setColour (Theme::edge); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, 1.f); }
    }
    void slotMenu (Track& t, int ti, int s)
    {
        juce::PopupMenu m; auto& clip = t.slots[(size_t) s];
        const bool has = ! clip.notes.empty() || clip.isAudio();
        m.addItem (1, "Edit in Piano Roll", ! t.audioTrack); m.addItem (2, "Delete clip", has); m.addItem (3, "Rename...", has);
        m.addItem (4, "Record MIDI into slot (stops on Stop)", ! t.audioTrack); m.addItem (5, "Copy to Arrangement at playhead", has);
        const int id = t.id;
        m.showMenuAsync ({}, [this, id, ti, s] (int r)
        {
            auto* t = ctx.project().findTrack (id); if (! t || r == 0) return; juce::ignoreUnused (ti);
            if (r == 1) { ctx.editClip (id, true, s); if (onOpenEditor) onOpenEditor(); }
            else if (r == 2) ctx.edit ([&] { t->slots[(size_t) s] = Clip(); t->slots[(size_t) s].lengthBeats = 4.0; });
            else if (r == 3) promptText ("Rename clip", t->slots[(size_t) s].name, [this, id, s] (juce::String n) { ctx.edit ([&] { if (auto* tt = ctx.project().findTrack (id)) tt->slots[(size_t) s].name = n; }); });
            else if (r == 4) { juce::String err; ctx.slotRecord = { id, s }; if (! ctx.engine.startRecording (id, &err)) { ctx.slotRecord = {}; juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Record", err); } }
            else if (r == 5) ctx.edit ([&] { Clip c = t->slots[(size_t) s]; c.startBeat = std::floor (ctx.engine.getPosition() / 4.0) * 4.0; t->clips.push_back (c); });
        });
    }
};
}
