#pragma once
#include "ViewBase.h"

namespace noize
{
class ArrangementView : public ViewBase, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget
{
public:
    explicit ArrangementView (AppContext& c) : ViewBase (c)
    {
        auto mk = [this] (juce::TextButton& b, const char* t) { b.setButtonText (t); addAndMakeVisible (b); };
        mk (addMidi, "+ MIDI Track"); mk (addAudio, "+ Audio Track"); mk (zoomIn, "Zoom +"); mk (zoomOut, "Zoom -"); mk (autoBtn, "Automation"); mk (fitBtn, "Fit");
        autoBtn.setClickingTogglesState (true);
        autoBox.addItem ("Volume", 1); autoBox.addItem ("Pan", 2); autoBox.addItem ("Reverb send", 3); autoBox.addItem ("Delay send", 4);
        autoBox.setSelectedId (1, juce::dontSendNotification); addAndMakeVisible (autoBox);
        for (auto s : { "Bar", "1/2", "1/4", "1/8", "1/16", "Off" }) snapBox.addItem (s, snapBox.getNumItems() + 1);
        snapBox.setSelectedId (3, juce::dontSendNotification); addAndMakeVisible (snapBox);
        snapBox.onChange = [this] { static const double v[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.0 }; gridSnap = v[snapBox.getSelectedId() - 1]; };
        addMidi.onClick = [this] { instrumentMenu ([this] (InstrumentType t) { ctx.edit ([&] { auto& tr = ctx.project().addTrack (instrumentNames()[(int) t], false, t); ctx.selectedTrack = tr.id; }); }); };
        addAudio.onClick = [this] { ctx.edit ([&] { auto& tr = ctx.project().addTrack ("Audio " + juce::String (ctx.project().tracks.size() + 1), true); ctx.selectedTrack = tr.id; }); };
        zoomIn.onClick = [this] { pxPerBeat = juce::jmin (400.0, pxPerBeat * 1.3); repaint(); };
        zoomOut.onClick = [this] { pxPerBeat = juce::jmax (6.0, pxPerBeat / 1.3); repaint(); };
        fitBtn.onClick = [this] { pxPerBeat = juce::jmax (6.0, (getWidth() - headerW - 20) / juce::jmax (8.0, ctx.project().lengthBeats)); scrollBeat = 0; repaint(); };
        autoBtn.onClick = [this] { showAuto = autoBtn.getToggleState(); repaint(); };
        startUiTimer (30);
    }

    void resized() override
    {
        auto r = getLocalBounds().removeFromTop (toolH).reduced (6, 5);
        for (auto* b : { &addMidi, &addAudio }) { b->setBounds (r.removeFromLeft (110)); r.removeFromLeft (4); }
        r.removeFromLeft (10);
        for (auto* b : { &zoomOut, &zoomIn, &fitBtn }) { b->setBounds (r.removeFromLeft (64)); r.removeFromLeft (4); }
        r.removeFromLeft (10); snapBox.setBounds (r.removeFromLeft (70)); r.removeFromLeft (10);
        autoBtn.setBounds (r.removeFromLeft (96)); r.removeFromLeft (4); autoBox.setBounds (r.removeFromLeft (110));
    }

    void tick() override
    {
        if (ctx.engine.isPlaying())
        {
            const double p = ctx.engine.getPosition(), vis = (getWidth() - headerW) / pxPerBeat;
            if (p > scrollBeat + vis || p < scrollBeat) scrollBeat = std::floor (p / 4.0) * 4.0;
            repaint();
        }
    }

    //------------------------------------------------------------------------------ painting
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        auto& proj = ctx.project();
        const int w = getWidth();
        const double bpBar = proj.timeSigNum * 4.0 / proj.timeSigDen;
        // ruler
        auto ruler = juce::Rectangle<int> (headerW, toolH, w - headerW, rulerH);
        g.setColour (Theme::panel); g.fillRect (ruler);
        g.saveState(); g.reduceClipRegion (juce::Rectangle<int> (headerW, toolH, w - headerW, getHeight() - toolH));
        if (proj.loopEnabled)
        {
            g.setColour (Theme::accent.withAlpha (0.25f));
            g.fillRect (beatToX (proj.loopStartBeat), toolH, beatToX (proj.loopEndBeat) - beatToX (proj.loopStartBeat), rulerH);
            g.setColour (Theme::accent.withAlpha (0.07f));
            g.fillRect (beatToX (proj.loopStartBeat), toolH + rulerH, beatToX (proj.loopEndBeat) - beatToX (proj.loopStartBeat), getHeight());
        }
        g.setFont (Theme::font (11.f));
        const double step = pxPerBeat * bpBar < 40 ? bpBar * 4 : bpBar;
        for (double b = std::floor (scrollBeat / step) * step; beatToX (b) < w; b += step)
        {
            const int x = beatToX (b); if (x < headerW) continue;
            g.setColour (Theme::dim); g.drawText (juce::String ((int) std::round (b / bpBar) + 1), x + 3, toolH, 40, rulerH, juce::Justification::centredLeft);
            g.setColour (Theme::edge); g.drawVerticalLine (x, (float) toolH, (float) getHeight());
        }
        // rows
        int y = rowsTop() - (int) scrollY;
        for (size_t i = 0; i < proj.tracks.size(); ++i)
        {
            auto& t = proj.tracks[i];
            const int h = rowHeight (t);
            auto row = juce::Rectangle<int> (headerW, y, w - headerW, trackH);
            if (row.getBottom() > toolH + rulerH && row.getY() < getHeight())
            {
                g.setColour ((i & 1) ? Theme::panel.darker (0.2f) : Theme::panel.darker (0.4f)); g.fillRect (row);
                // beat grid
                if (pxPerBeat >= 14)
                    for (double b = std::floor (scrollBeat); beatToX (b) < w; b += 1.0)
                    {
                        const bool bar = std::fmod (b, bpBar) < 1e-6; g.setColour (Theme::edge.withAlpha (bar ? 0.9f : 0.3f));
                        if (beatToX (b) >= headerW) g.drawVerticalLine (beatToX (b), (float) row.getY(), (float) row.getBottom());
                    }
                for (size_t ci = 0; ci < t.clips.size(); ++ci) paintClip (g, t, t.clips[ci], row, (int) i, (int) ci);
                g.setColour (Theme::edge); g.drawHorizontalLine (row.getBottom() - 1, (float) headerW, (float) w);
                if (showAuto && t.id == ctx.selectedTrack)
                {
                    auto lane = juce::Rectangle<int> (headerW, y + trackH, w - headerW, autoH);
                    paintAutomation (g, t, lane);
                }
            }
            y += h;
        }
        // playhead
        const int px = beatToX (ctx.engine.getPosition());
        if (px >= headerW) { g.setColour (juce::Colours::white); g.drawVerticalLine (px, (float) toolH, (float) getHeight()); g.setColour (Theme::accent); g.fillRect (px - 4, toolH, 9, 4); }
        g.restoreState();

        // headers
        y = rowsTop() - (int) scrollY;
        g.saveState(); g.reduceClipRegion (juce::Rectangle<int> (0, toolH + rulerH, headerW, getHeight()));
        for (size_t i = 0; i < proj.tracks.size(); ++i)
        {
            auto& t = proj.tracks[i];
            paintHeader (g, t, juce::Rectangle<int> (0, y, headerW, trackH), (int) i);
            y += rowHeight (t);
        }
        g.restoreState();
        g.setColour (Theme::panel); g.fillRect (0, toolH, headerW, rulerH);
        g.setColour (Theme::dim); g.setFont (Theme::font (11.f, true)); g.drawText ("TRACKS", 10, toolH, 100, rulerH, juce::Justification::centredLeft);
    }

    //------------------------------------------------------------------------------ mouse
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto p = e.getPosition(); auto& proj = ctx.project();
        drag = None;
        if (p.y < toolH) return;
        if (p.y < toolH + rulerH && p.x >= headerW)
        {
            const double b = snapB (xToBeat (p.x));
            if (e.mods.isShiftDown()) { ctx.beginEdit(); drag = LoopSel; loopAnchor = b; proj.loopStartBeat = b; proj.loopEndBeat = b + 1; proj.loopEnabled = true; ctx.touch(); }
            else { ctx.engine.setPosition (juce::jmax (0.0, b)); drag = Scrub; }
            return;
        }
        int ti = trackAt (p.y);
        if (ti < 0) return;
        auto& t = proj.tracks[(size_t) ti];
        if (p.x < headerW) { headerClick (e, t, ti); return; }
        // automation lane?
        if (showAuto && t.id == ctx.selectedTrack && inAutoLane (p.y, ti)) { autoMouse (e, t, ti); return; }
        int ci = clipAt (t, ti, p);
        if (e.mods.isPopupMenu())
        {
            if (ci >= 0) clipMenu (t, ci); else if (p.y >= 0) trackAreaMenu (t, xToBeat (p.x));
            return;
        }
        if (ci >= 0)
        {
            ctx.editClip (t.id, false, ci);
            auto& c = t.clips[(size_t) ci];
            ctx.beginEdit();
            if (e.mods.isAltDown()) { Clip cp = c; t.clips.push_back (cp); ci = (int) t.clips.size() - 1; ctx.editClip (t.id, false, ci); }
            const int rx = beatToX (c.startBeat + c.lengthBeats);
            drag = (p.x > rx - 7) ? Resize : Move; dragTrackId = t.id; dragClip = ci;
            dragOffset = xToBeat (p.x) - t.clips[(size_t) ci].startBeat;
        }
        else ctx.selectTrack (t.id);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto p = e.getPosition(); int ti = trackAt (p.y);
        if (ti < 0 || p.x < headerW || p.y < toolH + rulerH) return;
        auto& t = ctx.project().tracks[(size_t) ti];
        if (clipAt (t, ti, p) >= 0) { if (auto* c = &t.clips[(size_t) clipAt (t, ti, p)]; ! c->isAudio()) { ctx.editClip (t.id, false, clipAt (t, ti, p)); if (onOpenEditor) onOpenEditor(); } return; }
        if (showAuto && t.id == ctx.selectedTrack && inAutoLane (p.y, ti)) return;
        if (t.audioTrack) return;
        ctx.edit ([&] { Clip c; c.name = "Clip " + juce::String (t.clips.size() + 1); c.startBeat = std::floor (snapB (xToBeat (p.x)) / 4.0) * 4.0; c.lengthBeats = 4.0; t.clips.push_back (c); });
        ctx.editClip (t.id, false, (int) t.clips.size() - 1);
        if (onOpenEditor) onOpenEditor();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto p = e.getPosition(); auto& proj = ctx.project();
        if (drag == Scrub) { ctx.engine.setPosition (juce::jmax (0.0, snapB (xToBeat (p.x)))); repaint(); return; }
        if (drag == LoopSel) { const double b = snapB (xToBeat (p.x)); proj.loopStartBeat = juce::jmin (loopAnchor, b); proj.loopEndBeat = juce::jmax (loopAnchor, b) + (b == loopAnchor ? 1.0 : 0.0); ctx.touch(); return; }
        if (drag == AutoPt) { autoDrag (p); return; }
        auto* t = proj.findTrack (dragTrackId); if (! t || dragClip < 0 || dragClip >= (int) t->clips.size()) return;
        auto& c = t->clips[(size_t) dragClip];
        if (drag == Move)
        {
            c.startBeat = juce::jmax (0.0, snapB (xToBeat (p.x) - dragOffset));
            int ti = trackAt (p.y);
            if (ti >= 0 && proj.tracks[(size_t) ti].id != t->id && proj.tracks[(size_t) ti].audioTrack == t->audioTrack)
            {
                auto& dst = proj.tracks[(size_t) ti]; dst.clips.push_back (c); t->clips.erase (t->clips.begin() + dragClip);
                dragTrackId = dst.id; dragClip = (int) dst.clips.size() - 1; ctx.editClip (dst.id, false, dragClip);
            }
            proj.lengthBeats = juce::jmax (proj.lengthBeats, c.startBeat + c.lengthBeats + 4.0);
            ctx.touch();
        }
        else if (drag == Resize)
        {
            c.lengthBeats = juce::jmax (gridSnap > 0 ? gridSnap : 0.25, snapB (xToBeat (p.x)) - c.startBeat);
            proj.lengthBeats = juce::jmax (proj.lengthBeats, c.startBeat + c.lengthBeats + 4.0); ctx.touch();
        }
    }
    void mouseUp (const juce::MouseEvent&) override { drag = None; }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown()) { const double before = xToBeat (e.x); pxPerBeat = juce::jlimit (6.0, 400.0, pxPerBeat * (w.deltaY > 0 ? 1.15 : 0.87)); scrollBeat = juce::jmax (0.0, before - (e.x - headerW) / pxPerBeat); }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY)) scrollBeat = juce::jmax (0.0, scrollBeat - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 8.0);
        else scrollY = juce::jlimit (0.0, (double) juce::jmax (0, totalRowsH() - (getHeight() - rowsTop()) + 20), scrollY - w.deltaY * 80.0);
        repaint();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        auto& proj = ctx.project();
        auto* t = ctx.selTrack(); auto* c = ctx.targetClip();
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && c && t && ! ctx.target.slot)
        { ctx.edit ([&] { if (ctx.target.index < (int) t->clips.size()) t->clips.erase (t->clips.begin() + ctx.target.index); }); ctx.target = {}; return true; }
        if (k == juce::KeyPress ('d', juce::ModifierKeys::ctrlModifier, 0) && c && t && ! ctx.target.slot) { duplicateClip (*t, ctx.target.index); return true; }
        juce::ignoreUnused (proj);
        return false;
    }

    //------------------------------------------------------------------------------ DnD
    bool isInterestedInFileDrag (const juce::StringArray& f) override { for (auto& s : f) if (isAudioFile (s)) return true; return false; }
    void filesDropped (const juce::StringArray& files, int x, int y) override { for (auto& s : files) dropAudio (s, x, y); }
    bool isInterestedInDragSource (const SourceDetails& d) override { return isAudioFile (d.description.toString().upToFirstOccurrenceOf ("\n", false, false)); }
    void itemDropped (const SourceDetails& d) override { dropAudio (d.description.toString().upToFirstOccurrenceOf ("\n", false, false), d.localPosition.x, d.localPosition.y); }

    std::function<void()> onOpenEditor;

private:
    enum Drag { None, Move, Resize, Scrub, LoopSel, AutoPt };
    static constexpr int toolH = 38, rulerH = 24, headerW = 178, trackH = 64, autoH = 52;
    double pxPerBeat = 28.0, scrollBeat = 0.0, scrollY = 0.0, gridSnap = 1.0;
    bool showAuto = false;
    Drag drag = None; int dragTrackId = -1, dragClip = -1, autoIdx = -1; double dragOffset = 0, loopAnchor = 0;
    juce::TextButton addMidi, addAudio, zoomIn, zoomOut, autoBtn, fitBtn; juce::ComboBox snapBox, autoBox;

    static bool isAudioFile (const juce::String& s) { auto e = juce::File (s).getFileExtension().toLowerCase(); return e == ".wav" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".mp3" || e == ".ogg"; }
    int rowsTop() const { return toolH + rulerH; }
    int rowHeight (const Track& t) const { return trackH + (showAuto && t.id == ctx.selectedTrack ? autoH : 0); }
    int totalRowsH() { int h = 0; for (auto& t : ctx.project().tracks) h += rowHeight (t); return h; }
    int beatToX (double b) const { return headerW + (int) ((b - scrollBeat) * pxPerBeat); }
    double xToBeat (int x) const { return (x - headerW) / pxPerBeat + scrollBeat; }
    double snapB (double b) const { return gridSnap > 0 ? std::round (b / gridSnap) * gridSnap : b; }
    int trackAt (int y)
    {
        int yy = rowsTop() - (int) scrollY;
        for (size_t i = 0; i < ctx.project().tracks.size(); ++i) { const int h = rowHeight (ctx.project().tracks[i]); if (y >= yy && y < yy + h) return (int) i; yy += h; }
        return -1;
    }
    int trackTop (int ti) { int yy = rowsTop() - (int) scrollY; for (int i = 0; i < ti; ++i) yy += rowHeight (ctx.project().tracks[(size_t) i]); return yy; }
    bool inAutoLane (int y, int ti) { const int top = trackTop (ti) + trackH; return y >= top && y < top + autoH; }
    int clipAt (Track& t, int ti, juce::Point<int> p)
    {
        const int top = trackTop (ti);
        for (int i = (int) t.clips.size() - 1; i >= 0; --i)
        {
            auto& c = t.clips[(size_t) i];
            juce::Rectangle<int> r (beatToX (c.startBeat), top + 4, juce::jmax (4, (int) (c.lengthBeats * pxPerBeat)), trackH - 8);
            if (r.contains (p)) return i;
        }
        return -1;
    }
    void instrumentMenu (std::function<void (InstrumentType)> cb)
    {
        juce::PopupMenu m; auto names = instrumentNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        m.showMenuAsync ({}, [cb] (int r) { if (r > 0) cb ((InstrumentType) (r - 1)); });
    }

    void paintClip (juce::Graphics& g, Track& t, Clip& c, juce::Rectangle<int> row, int ti, int ci)
    {
        auto r = juce::Rectangle<int> (beatToX (c.startBeat), row.getY() + 4, juce::jmax (4, (int) (c.lengthBeats * pxPerBeat)), trackH - 8);
        if (r.getRight() < headerW || r.getX() > getWidth()) return;
        const auto col = Theme::trackColour (ti);
        const bool sel = ctx.target.trackId == t.id && ! ctx.target.slot && ctx.target.index == ci;
        if (Theme::quality > 0) g.setGradientFill (juce::ColourGradient (col.withAlpha (0.45f), 0, (float) r.getY(), col.withAlpha (0.18f), 0, (float) r.getBottom(), false));
        else g.setColour (col.withAlpha (0.3f));
        g.fillRoundedRectangle (r.toFloat(), 4.f);
        g.setColour (sel ? juce::Colours::white : col); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, sel ? 2.f : 1.f);
        g.saveState(); g.reduceClipRegion (r.reduced (1));
        g.setColour (col); g.fillRect (r.getX(), r.getY(), r.getWidth(), 14);
        g.setColour (juce::Colours::black); g.setFont (Theme::font (10.f, true)); g.drawText (c.name, r.getX() + 4, r.getY(), r.getWidth() - 6, 14, juce::Justification::centredLeft);
        auto body = r.withTrimmedTop (16).reduced (1);
        if (c.isAudio())
        {
            if (auto sd = ctx.engine.getAssets().get (c.audioFile))
            {
                const int len = sd->buffer.getNumSamples(); const float* d = sd->buffer.getReadPointer (0);
                const double secsPerPx = (1.0 / pxPerBeat) / (ctx.project().bpm / 60.0);
                g.setColour (col.withAlpha (0.9f));
                for (int x = 0; x < body.getWidth(); ++x)
                {
                    const double s0 = (x * secsPerPx + c.audioStartBeat / (ctx.project().bpm / 60.0)) * sd->sampleRate;
                    const int i0 = (int) s0, i1 = juce::jmin (len - 1, (int) (s0 + secsPerPx * sd->sampleRate));
                    if (i0 >= len) break;
                    float mx = 0; const int stride = juce::jmax (1, (i1 - i0) / 24);
                    for (int i = i0; i <= i1; i += stride) mx = juce::jmax (mx, std::abs (d[i]));
                    const int hh = (int) (mx * body.getHeight() * 0.5f);
                    g.drawVerticalLine (body.getX() + x, (float) (body.getCentreY() - hh), (float) (body.getCentreY() + hh + 1));
                }
            }
            else { g.setColour (juce::Colours::red); g.setFont (Theme::font (11.f, true)); g.drawText ("MISSING: " + juce::File (c.audioFile).getFileName(), body, juce::Justification::centred); }
        }
        else if (! c.notes.empty())
        {
            int lo = 127, hi = 0; for (auto& n : c.notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
            const float span = (float) juce::jmax (6, hi - lo + 1);
            g.setColour (col);
            for (auto& n : c.notes)
                g.fillRect ((float) body.getX() + (float) (n.startBeat * pxPerBeat), (float) body.getBottom() - (n.pitch - lo + 1) * body.getHeight() / span,
                            juce::jmax (2.f, (float) (n.lengthBeats * pxPerBeat)), juce::jmax (2.f, body.getHeight() / span - 1.f));
        }
        g.restoreState();
    }
    void paintHeader (juce::Graphics& g, Track& t, juce::Rectangle<int> r, int idx)
    {
        const bool sel = t.id == ctx.selectedTrack;
        paintPanel (g, r.reduced (2, 2).toFloat (), {}, 4.f);
        auto col = Theme::trackColour (idx);
        g.setColour (col); g.fillRect (r.getX() + 2, r.getY() + 4, 5, trackH - 8);
        if (t.group >= 0) { g.setColour (Theme::trackColour (t.group + 3)); g.fillRect (r.getX() + 8, r.getY() + 4, 3, trackH - 8); }
        if (sel) { g.setColour (Theme::accent); g.drawRoundedRectangle (r.reduced (2).toFloat(), 4.f, 1.5f); }
        g.setColour (Theme::text); g.setFont (Theme::font (13.f, true)); g.drawText (t.name, r.getX() + 16, r.getY() + 6, 100, 18, juce::Justification::centredLeft);
        g.setColour (Theme::dim); g.setFont (Theme::font (11.f));
        g.drawText (t.audioTrack ? "Audio" : instrumentNames()[(int) t.instrument], r.getX() + 16, r.getY() + 24, 110, 14, juce::Justification::centredLeft);
        auto btn = [&] (juce::Rectangle<int> b, const char* txt, bool on, juce::Colour c)
        { g.setColour (on ? c : Theme::panel); g.fillRoundedRectangle (b.toFloat(), 3.f); g.setColour (Theme::edge); g.drawRoundedRectangle (b.toFloat(), 3.f, 1.f);
          g.setColour (on ? juce::Colours::black : Theme::text); g.setFont (Theme::font (11.f, true)); g.drawText (txt, b, juce::Justification::centred); };
        btn (muteRect (r), "M", t.mute, juce::Colour (0xffff9f43)); btn (soloRect (r), "S", t.solo, Theme::accent);
    }
    juce::Rectangle<int> muteRect (juce::Rectangle<int> r) const { return { r.getRight() - 52, r.getY() + 8, 22, 20 }; }
    juce::Rectangle<int> soloRect (juce::Rectangle<int> r) const { return { r.getRight() - 28, r.getY() + 8, 22, 20 }; }

    void headerClick (const juce::MouseEvent& e, Track& t, int ti)
    {
        auto r = juce::Rectangle<int> (0, trackTop (ti), headerW, trackH);
        ctx.selectTrack (t.id);
        if (e.mods.isPopupMenu()) { trackMenu (t); return; }
        auto applyGroup = [&] (std::function<void (Track&)> f) { for (auto& o : ctx.project().tracks) if (&o == &t || (t.group >= 0 && o.group == t.group)) f (o); };
        if (muteRect (r).contains (e.getPosition())) { ctx.edit ([&] { const bool v = ! t.mute; applyGroup ([v] (Track& o) { o.mute = v; }); }); }
        else if (soloRect (r).contains (e.getPosition())) { ctx.edit ([&] { const bool v = ! t.solo; applyGroup ([v] (Track& o) { o.solo = v; }); }); }
    }
    void trackMenu (Track& t)
    {
        juce::PopupMenu m, inst, grp;
        m.addItem (1, "Rename...");
        m.addItem (2, "Duplicate track");
        m.addItem (3, "Delete track");
        if (! t.audioTrack) { auto n = instrumentNames(); for (int i = 0; i < n.size(); ++i) inst.addItem (100 + i, n[i], true, (int) t.instrument == i); m.addSubMenu ("Instrument", inst); }
        grp.addItem (200, "No group", true, t.group < 0);
        for (int i = 0; i < 4; ++i) grp.addItem (201 + i, "Group " + juce::String (i + 1), true, t.group == i);
        m.addSubMenu ("Track group", grp);
        const int id = t.id;
        m.showMenuAsync ({}, [this, id] (int r)
        {
            auto* t = ctx.project().findTrack (id); if (! t || r == 0) return;
            if (r == 1) promptText ("Rename track", t->name, [this, id] (juce::String s) { ctx.edit ([&] { if (auto* tt = ctx.project().findTrack (id)) tt->name = s.isEmpty() ? "Track" : s; }); });
            else if (r == 2) ctx.edit ([&] { Track c = *t; c.id = ctx.project().nextTrackId++; c.name += " copy"; ctx.project().tracks.push_back (c); });
            else if (r == 3) { ctx.edit ([&] { ctx.project().removeTrack (id); }); ctx.selectedTrack = -1; ctx.target = {}; }
            else if (r >= 100 && r < 200) ctx.edit ([&] { t->instrument = (InstrumentType) (r - 100); t->instrumentParams = defaultParams (t->instrument); });
            else if (r >= 200) ctx.edit ([&] { t->group = r == 200 ? -1 : r - 201; });
        });
    }
    void trackAreaMenu (Track& t, double beat)
    {
        juce::PopupMenu m; m.addItem (1, t.audioTrack ? "Import audio file here..." : "Create MIDI clip here");
        const int id = t.id; const double b = std::floor (snapB (beat) / 4.0) * 4.0;
        m.showMenuAsync ({}, [this, id, b] (int r)
        {
            auto* t = ctx.project().findTrack (id); if (! t || r != 1) return;
            if (! t->audioTrack) ctx.edit ([&] { Clip c; c.name = "Clip"; c.startBeat = b; c.lengthBeats = 4.0; t->clips.push_back (c); });
            else
            {
                chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, id, b] (const juce::FileChooser& fc)
                { auto f = fc.getResult(); if (f.existsAsFile()) addAudioClip (id, f.getFullPathName(), b); });
            }
        });
    }
    void clipMenu (Track& t, int ci)
    {
        juce::PopupMenu m; m.addItem (1, "Duplicate"); m.addItem (2, "Delete"); m.addItem (3, "Rename..."); m.addItem (4, "Split at playhead");
        m.addItem (5, "Set loop to clip"); if (! t.clips[(size_t) ci].isAudio()) m.addItem (6, "Edit in Piano Roll");
        const int id = t.id;
        m.showMenuAsync ({}, [this, id, ci] (int r)
        {
            auto* t = ctx.project().findTrack (id); if (! t || ci >= (int) t->clips.size()) return;
            auto& c = t->clips[(size_t) ci];
            if (r == 1) duplicateClip (*t, ci);
            else if (r == 2) { ctx.edit ([&] { t->clips.erase (t->clips.begin() + ci); }); ctx.target = {}; }
            else if (r == 3) promptText ("Rename clip", c.name, [this, id, ci] (juce::String s) { ctx.edit ([&] { if (auto* tt = ctx.project().findTrack (id)) if (ci < (int) tt->clips.size()) tt->clips[(size_t) ci].name = s; }); });
            else if (r == 4) splitClip (*t, ci);
            else if (r == 5) ctx.edit ([&] { ctx.project().loopStartBeat = c.startBeat; ctx.project().loopEndBeat = c.startBeat + c.lengthBeats; ctx.project().loopEnabled = true; });
            else if (r == 6) { ctx.editClip (id, false, ci); if (onOpenEditor) onOpenEditor(); }
        });
    }
    void duplicateClip (Track& t, int ci)
    {
        ctx.edit ([&] { Clip c = t.clips[(size_t) ci]; c.startBeat += c.lengthBeats; c.name += "'"; t.clips.push_back (c); ctx.project().lengthBeats = juce::jmax (ctx.project().lengthBeats, c.startBeat + c.lengthBeats + 4.0); });
        ctx.editClip (t.id, false, (int) t.clips.size() - 1);
    }
    void splitClip (Track& t, int ci)
    {
        auto c = t.clips[(size_t) ci]; const double pos = ctx.engine.getPosition() - c.startBeat;
        if (pos <= 0.01 || pos >= c.lengthBeats - 0.01) return;
        ctx.edit ([&]
        {
            Clip a = c, b = c; a.lengthBeats = pos; b.startBeat = c.startBeat + pos; b.lengthBeats = c.lengthBeats - pos; b.name = c.name + " 2";
            a.notes.clear(); b.notes.clear();
            for (auto n : c.notes) { if (n.startBeat < pos) { n.lengthBeats = juce::jmin (n.lengthBeats, pos - n.startBeat); a.notes.push_back (n); } else { n.startBeat -= pos; b.notes.push_back (n); } }
            if (c.isAudio()) b.audioStartBeat = c.audioStartBeat + pos;
            t.clips[(size_t) ci] = a; t.clips.push_back (b);
        });
    }
    void addAudioClip (int trackId, const juce::String& path, double beat)
    {
        auto sd = ctx.engine.getAssets().get (path);
        if (! sd) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot import", "Could not decode " + path); return; }
        const double beats = sd->buffer.getNumSamples() / sd->sampleRate * ctx.project().bpm / 60.0;
        ctx.edit ([&]
        {
            auto* t = ctx.project().findTrack (trackId); if (! t) return;
            Clip c; c.name = juce::File (path).getFileNameWithoutExtension(); c.audioFile = path; c.startBeat = juce::jmax (0.0, beat); c.lengthBeats = beats;
            t->clips.push_back (c); ctx.project().lengthBeats = juce::jmax (ctx.project().lengthBeats, c.startBeat + c.lengthBeats + 4.0);
        });
    }
    void dropAudio (const juce::String& path, int x, int y)
    {
        int ti = trackAt (y);
        if (ti < 0) { ctx.edit ([&] { ctx.project().addTrack (juce::File (path).getFileNameWithoutExtension(), true); }); ti = (int) ctx.project().tracks.size() - 1; }
        auto& t = ctx.project().tracks[(size_t) ti];
        if (! t.audioTrack) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Audio clips", "Drop audio files onto an audio track (use + Audio Track)."); return; }
        addAudioClip (t.id, path, snapB (xToBeat (juce::jmax (x, headerW))));
    }

    // ---- automation lane (volume/pan/sends: breakpoints 0..1)
    juce::String autoId() const { static const char* ids[] = { "volume", "pan", "sendReverb", "sendDelay" }; return ids[juce::jmax (0, autoBox.getSelectedId() - 1)]; }
    void paintAutomation (juce::Graphics& g, Track& t, juce::Rectangle<int> lane)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRect (lane);
        g.setColour (Theme::dim); g.setFont (Theme::font (10.f)); g.drawText (autoBox.getText() + " automation  (click = add point, right-click = delete)", lane.getX() + 6, lane.getY() + 1, 400, 12, juce::Justification::left);
        auto it = t.automation.find (autoId());
        if (it == t.automation.end()) return;
        juce::Path p; bool first = true;
        for (auto& pt : it->second)
        {
            const float x = (float) beatToX (pt.first), y = (float) lane.getBottom() - 4 - pt.second * (lane.getHeight() - 8);
            if (first) { p.startNewSubPath (x, y); first = false; } else p.lineTo (x, y);
        }
        g.setColour (Theme::accent); g.strokePath (p, juce::PathStrokeType (1.5f));
        for (auto& pt : it->second) g.fillEllipse ((float) beatToX (pt.first) - 3.f, (float) lane.getBottom() - 4 - pt.second * (lane.getHeight() - 8) - 3.f, 6.f, 6.f);
    }
    void autoMouse (const juce::MouseEvent& e, Track& t, int ti)
    {
        auto& pts = t.automation[autoId()]; const int top = trackTop (ti) + trackH;
        int hit = -1;
        for (size_t i = 0; i < pts.size(); ++i) if (std::abs (beatToX (pts[i].first) - e.x) < 6) { hit = (int) i; break; }
        ctx.beginEdit();
        if (e.mods.isPopupMenu()) { if (hit >= 0) { pts.erase (pts.begin() + hit); if (pts.empty()) t.automation.erase (autoId()); ctx.touch(); } return; }
        if (hit < 0)
        {
            const float v = juce::jlimit (0.f, 1.f, 1.f - (float) (e.y - top - 4) / (autoH - 8));
            pts.push_back ({ juce::jmax (0.0, snapB (xToBeat (e.x))), v });
            std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            for (size_t i = 0; i < pts.size(); ++i) if (std::abs (beatToX (pts[i].first) - e.x) < 6) hit = (int) i;
            ctx.touch();
        }
        drag = AutoPt; autoIdx = hit; dragTrackId = t.id;
    }
    void autoDrag (juce::Point<int> p)
    {
        auto* t = ctx.project().findTrack (dragTrackId); if (! t) return;
        auto it = t->automation.find (autoId()); if (it == t->automation.end() || autoIdx < 0 || autoIdx >= (int) it->second.size()) return;
        int ti = -1; for (size_t i = 0; i < ctx.project().tracks.size(); ++i) if (ctx.project().tracks[i].id == t->id) ti = (int) i;
        const int top = trackTop (ti) + trackH;
        auto& pt = it->second[(size_t) autoIdx];
        pt.second = juce::jlimit (0.f, 1.f, 1.f - (float) (p.y - top - 4) / (autoH - 8));
        pt.first = juce::jmax (0.0, snapB (xToBeat (p.x)));
        ctx.touch();
    }
    std::unique_ptr<juce::FileChooser> chooser;
};
}
