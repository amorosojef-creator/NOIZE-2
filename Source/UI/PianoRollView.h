#pragma once
#include "ViewBase.h"

namespace noize
{
class PianoRollView : public ViewBase
{
public:
    explicit PianoRollView (AppContext& c) : ViewBase (c)
    {
        auto mk = [this] (juce::TextButton& b, const char* t) { b.setButtonText (t); addAndMakeVisible (b); };
        mk (quantizeBtn, "Quantize"); mk (importBtn, "Import MIDI"); mk (exportBtn, "Export MIDI"); mk (lenMinus, "-1 bar"); mk (lenPlus, "+1 bar"); mk (clearBtn, "Clear");
        for (auto s : { "1/4", "1/8", "1/16", "1/32" }) snapBox.addItem (s, snapBox.getNumItems() + 1);
        snapBox.setSelectedId (3, juce::dontSendNotification);
        snapBox.onChange = [this] { static const double v[] = { 1.0, 0.5, 0.25, 0.125 }; ctx.snap = v[snapBox.getSelectedId() - 1]; };
        addAndMakeVisible (snapBox);
        quantizeBtn.onClick = [this] { quantize(); };
        lenMinus.onClick = [this] { changeLength (-4.0); }; lenPlus.onClick = [this] { changeLength (4.0); };
        clearBtn.onClick = [this] { if (auto* cl = ctx.targetClip()) { ctx.edit ([&] { cl->notes.clear(); }); selected = -1; } };
        importBtn.onClick = [this] { importMidi(); }; exportBtn.onClick = [this] { exportMidi(); };
        setWantsKeyboardFocus (true);
        startUiTimer (30);
    }
    ~PianoRollView() override { ctx.engine.setLiveTrack (-1); }

    void activated() override { if (auto* t = ctx.project().findTrack (ctx.target.trackId)) ctx.engine.setLiveTrack (t->id); grabKeyboardFocus(); }
    void tick() override { if (ctx.engine.isPlaying()) repaint(); }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (headerH).reduced (6, 5);
        titleArea = top.removeFromLeft (260);
        snapBox.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6);
        for (auto* b : { &quantizeBtn, &lenMinus, &lenPlus, &clearBtn, &importBtn, &exportBtn }) { b->setBounds (top.removeFromLeft (b == &importBtn || b == &exportBtn ? 96 : 76)); top.removeFromLeft (4); }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        auto* clip = ctx.targetClip();
        auto* trk = ctx.project().findTrack (ctx.target.trackId);
        g.setColour (Theme::text); g.setFont (Theme::font (15.f, true));
        g.drawText (clip ? (trk ? trk->name : juce::String()) + " / " + clip->name : "No clip selected", titleArea, juce::Justification::centredLeft);
        if (clip == nullptr)
        {
            g.setColour (Theme::dim); g.setFont (Theme::font (14.f));
            g.drawText ("Double-click a track in Arrangement, a slot in Session, or open a Channel Rack pattern to edit notes here.", getLocalBounds().withTrimmedTop (headerH), juce::Justification::centred);
            return;
        }
        auto grid = gridArea(); auto keys = keyArea(); auto vel = velArea();
        const auto col = Theme::trackColour (trk ? juce::jmax (0, (int) (trk - &ctx.project().tracks[0])) : 0);

        // --- grid
        g.saveState(); g.reduceClipRegion (grid);
        for (int p = 0; p < 128; ++p)
        {
            const int y = pitchToY (p);
            if (y + rowH < grid.getY() || y > grid.getBottom()) continue;
            g.setColour (isBlackKey (p) ? Theme::bg.brighter (0.03f) : Theme::panel);
            g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
            g.setColour (Theme::edge.withAlpha (p % 12 == 0 ? 0.9f : 0.35f)); g.drawHorizontalLine (y + rowH - 1, (float) grid.getX(), (float) grid.getRight());
        }
        const double len = clip->lengthBeats;
        for (double b = 0; b <= len + 1e-6; b += ctx.snap)
        {
            const int x = beatToX (b);
            const bool bar = std::fmod (b, 4.0) < 1e-6, beat = std::fmod (b, 1.0) < 1e-6;
            g.setColour (Theme::edge.withAlpha (bar ? 1.f : beat ? 0.7f : 0.25f));
            g.drawVerticalLine (x, (float) grid.getY(), (float) grid.getBottom());
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (beatToX (len), grid.getY(), grid.getRight() - beatToX (len), grid.getHeight());
        // notes
        for (size_t i = 0; i < clip->notes.size(); ++i)
        {
            auto& n = clip->notes[i];
            auto r = noteRect (n);
            const bool sel = (int) i == selected;
            auto c = col.withMultipliedBrightness (0.55f + 0.45f * n.velocity);
            if (Theme::quality > 0) g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), 0, (float) r.getY(), c.darker (0.3f), 0, (float) r.getBottom(), false));
            else g.setColour (c);
            g.fillRoundedRectangle (r.toFloat(), 2.f);
            g.setColour (sel ? juce::Colours::white : c.brighter (0.5f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 2.f, sel ? 2.f : 1.f);
        }
        // playhead (arrangement only: show when editing an arrangement clip)
        if (! ctx.target.slot && ctx.engine.isPlaying())
        {
            const double pb = ctx.engine.getPosition() - clip->startBeat;
            if (pb >= 0 && pb <= len) { g.setColour (juce::Colours::white); g.drawVerticalLine (beatToX (pb), (float) grid.getY(), (float) grid.getBottom()); }
        }
        g.restoreState();

        // --- keyboard
        g.saveState(); g.reduceClipRegion (keys);
        for (int p = 0; p < 128; ++p)
        {
            const int y = pitchToY (p);
            if (y + rowH < keys.getY() || y > keys.getBottom()) continue;
            const bool black = isBlackKey (p), down = p == heldKey;
            auto kr = juce::Rectangle<int> (keys.getX(), y, black ? keys.getWidth() * 2 / 3 : keys.getWidth(), rowH);
            if (Theme::quality > 0)
                g.setGradientFill (juce::ColourGradient (down ? Theme::accent : (black ? juce::Colour (0xff444a50) : juce::Colour (0xffe9ecee)), (float) kr.getX(), 0,
                                                         down ? Theme::accent.darker() : (black ? juce::Colour (0xff0e0f11) : juce::Colour (0xff9aa0a6)), (float) kr.getRight(), 0, false));
            else g.setColour (down ? Theme::accent : (black ? juce::Colours::black : juce::Colours::white));
            g.fillRect (kr.reduced (0, 0));
            g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawRect (kr, 1);
            if (p % 12 == 0) { g.setColour (juce::Colours::black); g.setFont (Theme::font (10.f, true)); g.drawText (noteName (p), keys.getX() + 4, y, 40, rowH, juce::Justification::centredLeft); }
        }
        g.restoreState();

        // --- velocity lane
        paintPanel (g, vel.toFloat().expanded (0, 2), "Velocity", 3.f);
        g.saveState(); g.reduceClipRegion (vel.withTrimmedTop (4));
        for (size_t i = 0; i < clip->notes.size(); ++i)
        {
            auto& n = clip->notes[i];
            const int x = beatToX (n.startBeat);
            const int h = (int) (n.velocity * (vel.getHeight() - 18));
            g.setColour ((int) i == selected ? juce::Colours::white : col);
            g.fillRect (x, vel.getBottom() - h, 5, h);
            g.fillEllipse ((float) x - 2, (float) (vel.getBottom() - h - 3), 9.f, 6.f);
        }
        g.restoreState();
    }

    //--------------------------------------------------------------------------- mouse
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto* clip = ctx.targetClip(); if (clip == nullptr) return;
        auto p = e.getPosition();
        mode = None;
        if (keyArea().contains (p)) { heldKey = yToPitch (p.y); ctx.engine.injectMidi (juce::MidiMessage::noteOn (1, heldKey, 0.8f)); repaint(); mode = Key; return; }
        if (velArea().contains (p))
        {
            int best = -1, bd = 12;
            for (size_t i = 0; i < clip->notes.size(); ++i) { const int d = std::abs (beatToX (clip->notes[i].startBeat) + 2 - p.x); if (d < bd) { bd = d; best = (int) i; } }
            if (best >= 0) { selected = best; ctx.beginEdit(); mode = Vel; setVelFromY (p.y); }
            return;
        }
        if (! gridArea().contains (p)) return;
        const double beat = xToBeat (p.x); const int pitch = yToPitch (p.y);
        int hit = -1;
        for (int i = (int) clip->notes.size() - 1; i >= 0; --i) if (noteRect (clip->notes[(size_t) i]).contains (p)) { hit = i; break; }
        if (e.mods.isRightButtonDown() || e.mods.isAltDown())
        {
            if (hit >= 0) { ctx.edit ([&] { clip->notes.erase (clip->notes.begin() + hit); }); selected = -1; }
            return;
        }
        if (hit >= 0)
        {
            selected = hit; ctx.beginEdit();
            auto r = noteRect (clip->notes[(size_t) hit]);
            mode = (p.x > r.getRight() - 6) ? Resize : Move;
            grabBeat = beat - clip->notes[(size_t) hit].startBeat; grabPitch = pitch - clip->notes[(size_t) hit].pitch;
            preview (clip->notes[(size_t) hit].pitch, true);
        }
        else
        {
            if (beat < 0 || beat >= clip->lengthBeats) return;
            ctx.beginEdit();
            Note n; n.pitch = pitch; n.startBeat = snapDown (beat); n.lengthBeats = lastLen; n.velocity = 0.8f;
            clip->notes.push_back (n); selected = (int) clip->notes.size() - 1;
            mode = Resize; grabBeat = 0; preview (pitch, true); ctx.touch();
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* clip = ctx.targetClip(); if (clip == nullptr) return;
        auto p = e.getPosition();
        if (mode == Vel) { setVelFromY (p.y); return; }
        if (selected < 0 || selected >= (int) clip->notes.size()) return;
        auto& n = clip->notes[(size_t) selected];
        if (mode == Move)
        {
            const double nb = juce::jlimit (0.0, clip->lengthBeats - ctx.snap, snapRound (xToBeat (p.x) - grabBeat));
            const int np = juce::jlimit (0, 127, yToPitch (p.y) - grabPitch);
            if (np != n.pitch) { preview (n.pitch, false); preview (np, true); }
            n.startBeat = nb; n.pitch = np; ctx.touch();
        }
        else if (mode == Resize)
        {
            n.lengthBeats = juce::jmax (ctx.snap, snapRound (xToBeat (p.x) - n.startBeat));
            n.lengthBeats = juce::jmin (n.lengthBeats, clip->lengthBeats - n.startBeat);
            lastLen = n.lengthBeats; ctx.touch();
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == Key) ctx.engine.injectMidi (juce::MidiMessage::noteOff (1, heldKey));
        heldKey = -1;
        if (previewPitch >= 0) preview (previewPitch, false);
        mode = None; repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown()) { pxPerBeat = juce::jlimit (20.0, 400.0, pxPerBeat * (w.deltaY > 0 ? 1.15 : 0.87)); }
        else if (e.mods.isShiftDown()) scrollBeat = juce::jmax (0.0, scrollBeat - w.deltaY * 4.0);
        else scrollPitch = juce::jlimit (0.0, 127.0 - 8, scrollPitch - w.deltaY * 6.0);
        repaint();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        auto* clip = ctx.targetClip();
        if (clip == nullptr || selected < 0 || selected >= (int) clip->notes.size()) return false;
        auto& n = clip->notes[(size_t) selected];
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { ctx.edit ([&] { clip->notes.erase (clip->notes.begin() + selected); }); selected = -1; return true; }
        if (k == juce::KeyPress::upKey)    { ctx.edit ([&] { n.pitch = juce::jmin (127, n.pitch + (k.getModifiers().isShiftDown() ? 12 : 1)); }); return true; }
        if (k == juce::KeyPress::downKey)  { ctx.edit ([&] { n.pitch = juce::jmax (0, n.pitch - (k.getModifiers().isShiftDown() ? 12 : 1)); }); return true; }
        if (k == juce::KeyPress::leftKey)  { ctx.edit ([&] { n.startBeat = juce::jmax (0.0, n.startBeat - ctx.snap); }); return true; }
        if (k == juce::KeyPress::rightKey) { ctx.edit ([&] { n.startBeat = juce::jmin (clip->lengthBeats - ctx.snap, n.startBeat + ctx.snap); }); return true; }
        return false;
    }

    void projectChanged() override { if (auto* c = ctx.targetClip()) { if (selected >= (int) c->notes.size()) selected = -1; } else selected = -1; repaint(); }

private:
    enum Mode { None, Move, Resize, Vel, Key };
    static constexpr int headerH = 38, keyW = 62, velH = 84;
    int rowH = 14; double pxPerBeat = 90.0, scrollBeat = 0.0, scrollPitch = 100.0, lastLen = 0.25;
    int selected = -1, heldKey = -1, previewPitch = -1; Mode mode = None; double grabBeat = 0; int grabPitch = 0;
    juce::TextButton quantizeBtn, importBtn, exportBtn, lenMinus, lenPlus, clearBtn; juce::ComboBox snapBox; juce::Rectangle<int> titleArea;

    juce::Rectangle<int> keyArea() const { return { 0, headerH, keyW, getHeight() - headerH - velH - 8 }; }
    juce::Rectangle<int> gridArea() const { return { keyW, headerH, getWidth() - keyW, getHeight() - headerH - velH - 8 }; }
    juce::Rectangle<int> velArea() const { return { keyW, getHeight() - velH, getWidth() - keyW, velH - 4 }; }
    int pitchToY (int p) const { return gridArea().getY() + (int) ((scrollPitch - p) * rowH); }
    int beatToX (double b) const { return gridArea().getX() + (int) ((b - scrollBeat) * pxPerBeat); }
    double xToBeat (int x) const { return (x - gridArea().getX()) / pxPerBeat + scrollBeat; }
    int yToPitch (int y) const { for (int p = 127; p >= 0; --p) { const int py = pitchToY (p); if (y >= py && y < py + rowH) return p; } return 60; }
    juce::Rectangle<int> noteRect (const Note& n) const { return { beatToX (n.startBeat), pitchToY (n.pitch) + 1, juce::jmax (4, (int) (n.lengthBeats * pxPerBeat) - 1), rowH - 2 }; }
    double snapRound (double b) const { return std::round (b / ctx.snap) * ctx.snap; }
    double snapDown (double b) const { return std::floor (b / ctx.snap + 1e-6) * ctx.snap; }
    void setVelFromY (int y)
    {
        auto* clip = ctx.targetClip(); if (! clip || selected < 0 || selected >= (int) clip->notes.size()) return;
        auto v = velArea();
        clip->notes[(size_t) selected].velocity = juce::jlimit (0.05f, 1.f, (float) (v.getBottom() - y) / (float) (v.getHeight() - 18));
        ctx.touch();
    }
    void preview (int pitch, bool on)
    {
        if (on) { if (previewPitch >= 0) ctx.engine.injectMidi (juce::MidiMessage::noteOff (1, previewPitch)); ctx.engine.injectMidi (juce::MidiMessage::noteOn (1, pitch, 0.8f)); previewPitch = pitch; }
        else { ctx.engine.injectMidi (juce::MidiMessage::noteOff (1, pitch)); if (previewPitch == pitch) previewPitch = -1; }
    }
    void quantize()
    {
        auto* clip = ctx.targetClip(); if (! clip) return;
        ctx.edit ([&] { for (auto& n : clip->notes) { n.startBeat = juce::jlimit (0.0, clip->lengthBeats - ctx.snap, snapRound (n.startBeat)); n.lengthBeats = juce::jmax (ctx.snap, snapRound (n.lengthBeats)); } });
    }
    void changeLength (double d)
    {
        auto* clip = ctx.targetClip(); if (! clip) return;
        ctx.edit ([&] { clip->lengthBeats = juce::jmax (1.0, clip->lengthBeats + d); });
    }
    void importMidi()
    {
        auto* clip = ctx.targetClip(); if (! clip) return;
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI file", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult(); auto* cl = ctx.targetClip();
            if (! f.existsAsFile() || cl == nullptr) return;
            juce::FileInputStream in (f); juce::MidiFile mf;
            if (! in.openedOk() || ! mf.readFrom (in)) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", "Not a valid MIDI file."); return; }
            const int tpq = mf.getTimeFormat(); if (tpq <= 0) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", "SMPTE time format is not supported."); return; }
            ctx.beginEdit(); cl->notes.clear(); double endBeat = 0;
            for (int t = 0; t < mf.getNumTracks(); ++t)
            {
                std::map<int, std::pair<double, float>> held;
                for (auto* ev : *mf.getTrack (t))
                {
                    auto& m = ev->message; const double beat = m.getTimeStamp() / tpq;
                    if (m.isNoteOn()) held[m.getNoteNumber()] = { beat, m.getFloatVelocity() };
                    else if (m.isNoteOff() && held.count (m.getNoteNumber()))
                    { auto h = held[m.getNoteNumber()]; held.erase (m.getNoteNumber()); cl->notes.push_back ({ m.getNoteNumber(), h.first, juce::jmax (0.05, beat - h.first), h.second }); endBeat = juce::jmax (endBeat, beat); }
                }
            }
            cl->lengthBeats = juce::jmax (cl->lengthBeats, std::ceil (endBeat / 4.0) * 4.0);
            ctx.touch();
        });
    }
    void exportMidi()
    {
        auto* clip = ctx.targetClip(); if (! clip) return;
        chooser = std::make_unique<juce::FileChooser> ("Export MIDI file", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("clip.mid"), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult(); auto* cl = ctx.targetClip(); if (f == juce::File() || cl == nullptr) return;
            if (f.getFileExtension().isEmpty()) f = f.withFileExtension (".mid");
            constexpr int tpq = 480; juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / ctx.project().bpm)));
            for (auto& n : cl->notes)
            {
                seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, n.velocity), n.startBeat * tpq);
                seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), (n.startBeat + n.lengthBeats) * tpq);
            }
            seq.updateMatchedPairs();
            juce::MidiFile mf; mf.setTicksPerQuarterNote (tpq); mf.addTrack (seq);
            f.deleteFile(); juce::FileOutputStream out (f);
            if (! out.openedOk() || ! mf.writeTo (out)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", "Could not write " + f.getFullPathName());
        });
    }
    std::unique_ptr<juce::FileChooser> chooser;
};
}
