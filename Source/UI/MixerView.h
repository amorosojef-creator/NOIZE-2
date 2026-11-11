#pragma once
#include "ViewBase.h"

namespace noize
{
/** One insert-effect list used by every strip (tracks and master). */
class InsertList : public juce::Component
{
public:
    InsertList (AppContext& c, std::function<std::vector<EffectSlot>*()> getter, std::function<void (int, int)> openEditor)
        : ctx (c), get (std::move (getter)), openEd (std::move (openEditor)) {}
    int heightNeeded() const { auto* v = get(); return ((v ? (int) v->size() : 0) + 1) * (rowH + 2); }
    void paint (juce::Graphics& g) override
    {
        auto* v = get(); if (! v) return;
        for (size_t i = 0; i <= v->size(); ++i)
        {
            auto r = juce::Rectangle<int> (0, (int) i * (rowH + 2), getWidth(), rowH).toFloat();
            if (i == v->size()) { g.setColour (Theme::panel); g.fillRoundedRectangle (r, 3.f); g.setColour (Theme::dim); g.setFont (Theme::font (11.f, true)); g.drawText ("+ add effect", r, juce::Justification::centred); break; }
            auto& s = (*v)[i];
            g.setColour (s.bypass ? Theme::panel2 : Theme::accent.withAlpha (0.85f)); g.fillRoundedRectangle (r, 3.f);
            g.setColour (s.bypass ? Theme::dim : juce::Colours::black); g.setFont (Theme::font (11.f, true));
            g.drawText (effectNames()[(int) s.type], r.reduced (5, 0), juce::Justification::centredLeft, true);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* v = get(); if (! v) return;
        const int i = e.y / (rowH + 2);
        if (i == (int) v->size())
        {
            juce::PopupMenu m; auto n = effectNames(); for (int k = 0; k < n.size(); ++k) m.addItem (k + 1, n[k]);
            m.showMenuAsync ({}, [this] (int r) { if (r > 0) { if (auto* vv = get()) ctx.edit ([&] { vv->push_back ({ (EffectType) (r - 1), false, defaultParams ((EffectType) (r - 1)) }); }); } });
            return;
        }
        if (i < 0 || i >= (int) v->size()) return;
        if (e.mods.isPopupMenu() || e.x > getWidth() - 16)
        {
            juce::PopupMenu m; m.addItem (1, (*v)[(size_t) i].bypass ? "Enable" : "Bypass"); m.addItem (2, "Edit parameters..."); m.addItem (3, "Move up", i > 0); m.addItem (4, "Move down", i + 1 < (int) v->size()); m.addItem (5, "Remove");
            m.showMenuAsync ({}, [this, i] (int r)
            {
                auto* vv = get(); if (! vv || i >= (int) vv->size() || r == 0) return;
                if (r == 2) { openEd (i, 0); return; }
                ctx.edit ([&] { if (r == 1) (*vv)[(size_t) i].bypass = ! (*vv)[(size_t) i].bypass; else if (r == 3) std::swap ((*vv)[(size_t) i], (*vv)[(size_t) i - 1]);
                                else if (r == 4) std::swap ((*vv)[(size_t) i], (*vv)[(size_t) i + 1]); else if (r == 5) vv->erase (vv->begin() + i); });
            });
        }
        else if (e.getNumberOfClicks() > 1) openEd (i, 0);
        else ctx.edit ([&] { (*v)[(size_t) i].bypass = ! (*v)[(size_t) i].bypass; });
    }
    static constexpr int rowH = 22;
private:
    AppContext& ctx; std::function<std::vector<EffectSlot>*()> get; std::function<void (int, int)> openEd;
};

class MixerView : public ViewBase
{
public:
    explicit MixerView (AppContext& c) : ViewBase (c) { startUiTimer (30); refresh (true); }
    ~MixerView() override { editors.clear(); }
    void activated() override { refresh (false); }
    void projectChanged() override { refresh (false); repaint(); }
    void resized() override { layout(); }

    void tick() override
    {
        auto& rend = ctx.engine.getRenderer();
        for (size_t i = 0; i < strips.size(); ++i) strips[i]->meter.setLevel (rend.takeTrackPeak ((int) i, 0), rend.takeTrackPeak ((int) i, 1));
        master.meter.setLevel (rend.takeMasterPeak (0), rend.takeMasterPeak (1));
    }
    void paint (juce::Graphics& g) override { g.fillAll (Theme::bg); }

private:
    struct Strip : public juce::Component
    {
        Strip (MixerView& o, int trackId) : owner (o), id (trackId) {}
        MixerView& owner; int id; bool isMaster = false;
        juce::Slider fader, pan, sendR, sendD; juce::TextButton mute { "M" }, solo { "S" }, route { "Out" };
        MeterBar meter; std::unique_ptr<InsertList> inserts; juce::Label nameLbl, dbLbl;
        juce::Colour col;
        void paint (juce::Graphics& g) override
        {
            paintPanel (g, getLocalBounds().toFloat().reduced (2.f), {}, 6.f);
            g.setColour (col); g.fillRect (6, 6, getWidth() - 12, 4);
            g.setColour (Theme::dim); g.setFont (Theme::font (10.f, true));
            g.drawText ("INSERTS", 8, 38, getWidth() - 16, 12, juce::Justification::left);
            if (! isMaster) { g.drawText ("SEND R", 8, sendR.getY() - 11, 40, 10, juce::Justification::left); g.drawText ("SEND D", getWidth() / 2 + 2, sendD.getY() - 11, 40, 10, juce::Justification::left); }
            g.drawText ("PAN", 8, pan.getY() - 11, 40, 10, juce::Justification::left);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            r.removeFromTop (6);
            nameLbl.setBounds (r.removeFromTop (22));
            r.removeFromTop (14);
            const int ih = inserts ? juce::jmin (inserts->heightNeeded(), 6 * (InsertList::rowH + 2)) : 0;
            if (inserts) inserts->setBounds (r.removeFromTop (6 * (InsertList::rowH + 2)).withHeight (ih));
            else r.removeFromTop (6 * (InsertList::rowH + 2));
            r = getLocalBounds().reduced (6).withTrimmedTop (6 + 22 + 14 + 6 * (InsertList::rowH + 2) + 14);
            if (! isMaster) { auto sr = r.removeFromTop (46); const int hw = sr.getWidth() / 2; sendR.setBounds (sr.removeFromLeft (hw).withTrimmedTop (10)); sendD.setBounds (sr.withTrimmedTop (10)); r.removeFromTop (4); }
            auto pr = r.removeFromTop (isMaster ? 0 : 56); pan.setBounds (pr.withTrimmedTop (10)); r.removeFromTop (2);
            auto bt = r.removeFromBottom (24); r.removeFromBottom (4);
            if (! isMaster) { mute.setBounds (bt.removeFromLeft (bt.getWidth() / 2 - 2)); solo.setBounds (bt.withTrimmedLeft (4)); }
            dbLbl.setBounds (r.removeFromBottom (16));
            meter.setBounds (r.removeFromRight (18).reduced (1, 4)); r.removeFromRight (4);
            fader.setBounds (r);
        }
    };

    std::vector<std::unique_ptr<Strip>> strips; Strip master { *this, -1 };
    juce::String signature; juce::OwnedArray<juce::Component> dummy;
    std::vector<std::unique_ptr<FxEditorWindow>> editors;

    static constexpr int stripW = 118;

    void layout()
    {
        int x = 6;
        for (auto& s : strips) { s->setBounds (x, 6, stripW, getHeight() - 12); x += stripW + 4; }
        master.setBounds (getWidth() - stripW - 6, 6, stripW, getHeight() - 12);
    }

    juce::String makeSignature()
    {
        juce::String s;
        for (auto& t : ctx.project().tracks) { s << t.id << ":" << t.name << ":" << t.effects.size() << ":"; for (auto& e : t.effects) s << (int) e.type << (e.bypass ? "b" : "o"); s << ";"; }
        s << "M" << ctx.project().masterEffects.size(); for (auto& e : ctx.project().masterEffects) s << (int) e.type << (e.bypass ? "b" : "o");
        return s;
    }

    void setupCommon (Strip& s)
    {
        for (auto* sl : { &s.fader }) { sl->setSliderStyle (juce::Slider::LinearVertical); sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); }
        s.fader.setRange (0.0, 1.25, 0.001); s.fader.setDoubleClickReturnValue (true, 0.8);
        s.pan.setSliderStyle (juce::Slider::RotaryVerticalDrag); s.pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); s.pan.setRange (-1, 1, 0.01); s.pan.setDoubleClickReturnValue (true, 0.0);
        s.pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        for (auto* sl : { &s.sendR, &s.sendD }) { sl->setSliderStyle (juce::Slider::RotaryVerticalDrag); sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); sl->setRange (0, 1, 0.01); sl->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true); }
        s.nameLbl.setJustificationType (juce::Justification::centred); s.nameLbl.setFont (Theme::font (13.f, true));
        s.dbLbl.setJustificationType (juce::Justification::centred); s.dbLbl.setFont (Theme::font (11.f)); s.dbLbl.setColour (juce::Label::textColourId, Theme::dim);
        for (auto* c : std::initializer_list<juce::Component*> { &s.fader, &s.pan, &s.meter, &s.nameLbl, &s.dbLbl }) s.addAndMakeVisible (c);
    }

    void refresh (bool force)
    {
        auto sig = makeSignature();
        auto& proj = ctx.project();
        if (force || sig != signature)
        {
            signature = sig; strips.clear(); closeEditors();
            for (size_t i = 0; i < proj.tracks.size(); ++i)
            {
                const int id = proj.tracks[i].id;
                auto s = std::make_unique<Strip> (*this, id); setupCommon (*s); s->col = Theme::trackColour ((int) i);
                for (auto* b : { &s->mute, &s->solo }) { b->setClickingTogglesState (false); s->addAndMakeVisible (b); }
                s->addAndMakeVisible (s->sendR); s->addAndMakeVisible (s->sendD);
                s->mute.onClick = [this, id] { ctx.edit ([&] { if (auto* t = ctx.project().findTrack (id)) t->mute = ! t->mute; }); };
                s->solo.onClick = [this, id] { ctx.edit ([&] { if (auto* t = ctx.project().findTrack (id)) t->solo = ! t->solo; }); };
                s->fader.onDragStart = [this] { ctx.beginEdit(); }; s->pan.onDragStart = [this] { ctx.beginEdit(); };
                s->sendR.onDragStart = [this] { ctx.beginEdit(); }; s->sendD.onDragStart = [this] { ctx.beginEdit(); };
                s->fader.onValueChange = [this, id, sp = s.get()] { if (auto* t = ctx.project().findTrack (id)) { t->volume = (float) sp->fader.getValue(); ctx.touch(); } };
                s->pan.onValueChange = [this, id, sp = s.get()] { if (auto* t = ctx.project().findTrack (id)) { t->pan = (float) sp->pan.getValue(); ctx.touch(); } };
                s->sendR.onValueChange = [this, id, sp = s.get()] { if (auto* t = ctx.project().findTrack (id)) { t->sendReverb = (float) sp->sendR.getValue(); ctx.touch(); } };
                s->sendD.onValueChange = [this, id, sp = s.get()] { if (auto* t = ctx.project().findTrack (id)) { t->sendDelay = (float) sp->sendD.getValue(); ctx.touch(); } };
                s->inserts = std::make_unique<InsertList> (ctx, [this, id] () -> std::vector<EffectSlot>* { auto* t = ctx.project().findTrack (id); return t ? &t->effects : nullptr; },
                                                           [this, id] (int slot, int) { openEditor (id, slot); });
                s->addAndMakeVisible (*s->inserts);
                s->addMouseListener (this, true);
                addAndMakeVisible (*s); strips.push_back (std::move (s));
            }
            master.isMaster = true; setupCommon (master); master.col = Theme::accent; master.nameLbl.setText ("MASTER", juce::dontSendNotification);
            master.fader.onDragStart = [this] { ctx.beginEdit(); };
            master.fader.onValueChange = [this] { ctx.project().masterVolume = (float) master.fader.getValue(); ctx.touch(); };
            master.inserts = std::make_unique<InsertList> (ctx, [this] () -> std::vector<EffectSlot>* { return &ctx.project().masterEffects; }, [this] (int slot, int) { openEditor (-1, slot); });
            master.pan.setVisible (false); master.sendR.setVisible (false); master.sendD.setVisible (false); master.mute.setVisible (false); master.solo.setVisible (false);
            master.addAndMakeVisible (*master.inserts); addAndMakeVisible (master);
            layout();
            for (auto& s : strips) s->resized();
            master.resized();
        }
        for (size_t i = 0; i < strips.size() && i < proj.tracks.size(); ++i)
        {
            auto& s = *strips[i]; auto& t = proj.tracks[i];
            s.nameLbl.setText (t.name, juce::dontSendNotification);
            if (! s.fader.isMouseButtonDown()) s.fader.setValue (t.volume, juce::dontSendNotification);
            if (! s.pan.isMouseButtonDown()) s.pan.setValue (t.pan, juce::dontSendNotification);
            if (! s.sendR.isMouseButtonDown()) s.sendR.setValue (t.sendReverb, juce::dontSendNotification);
            if (! s.sendD.isMouseButtonDown()) s.sendD.setValue (t.sendDelay, juce::dontSendNotification);
            s.mute.setToggleState (t.mute, juce::dontSendNotification); s.solo.setToggleState (t.solo, juce::dontSendNotification);
            s.dbLbl.setText (juce::Decibels::toString (juce::Decibels::gainToDecibels (t.volume * t.volume), 1), juce::dontSendNotification);
            s.repaint();
        }
        if (! master.fader.isMouseButtonDown()) master.fader.setValue (proj.masterVolume, juce::dontSendNotification);
        master.dbLbl.setText (juce::Decibels::toString (juce::Decibels::gainToDecibels (proj.masterVolume * proj.masterVolume), 1), juce::dontSendNotification);
        master.repaint();
    }

    void closeEditors() { editors.clear(); }
    void openEditor (int trackId, int slot)
    {
        auto getter = [this, trackId, slot] () -> ParamMap*
        {
            auto* v = trackId < 0 ? &ctx.project().masterEffects : (ctx.project().findTrack (trackId) ? &ctx.project().findTrack (trackId)->effects : nullptr);
            return (v && slot < (int) v->size()) ? &(*v)[(size_t) slot].params : nullptr;
        };
        auto* v = trackId < 0 ? &ctx.project().masterEffects : (ctx.project().findTrack (trackId) ? &ctx.project().findTrack (trackId)->effects : nullptr);
        if (! v || slot >= (int) v->size()) return;
        auto title = effectNames()[(int) (*v)[(size_t) slot].type] + (trackId < 0 ? " (Master)" : "");
        editors.push_back (std::make_unique<FxEditorWindow> (ctx, title, getter, true, [this] { juce::MessageManager::callAsync ([this] { editors.erase (std::remove_if (editors.begin(), editors.end(), [] (auto& w) { return ! w->isVisible(); }), editors.end()); }); }));
    }
};
}
