#pragma once
#include "Widgets.h"

namespace noize
{
/** Base for workspace views: listens to project changes, owns a UI timer. */
class ViewBase : public juce::Component, protected juce::ChangeListener, protected juce::Timer
{
public:
    explicit ViewBase (AppContext& c) : ctx (c) { ctx.addChangeListener (this); }
    ~ViewBase() override { ctx.removeChangeListener (this); }
    virtual void projectChanged() { repaint(); }
    virtual void tick() {}
    virtual void activated() {}      // view became visible
    void startUiTimer (int hz = 30) { startTimerHz (hz); }
protected:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { projectChanged(); }
    void timerCallback() override { if (isShowing()) tick(); }
    AppContext& ctx;
};

inline juce::String noteName (int n)
{
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (nm[n % 12]) + juce::String (n / 12 - 1);
}
inline bool isBlackKey (int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

inline void promptText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r) { if (r == 1) done (w->getTextEditorContents ("t")); }), true);
}
}
