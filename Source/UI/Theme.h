#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace noize
{
/** Visual identity: black/charcoal, metallic greys, white type, neon accent (default #CCFF00). */
namespace Theme
{
inline juce::Colour accent { 0xffccff00 };
inline int quality = 2;                 // 0 = flat 2D, 1 = low, 2 = high
inline const juce::Colour bg       { 0xff0a0b0c };
inline const juce::Colour panel    { 0xff15171a };
inline const juce::Colour panel2   { 0xff1e2125 };
inline const juce::Colour edge     { 0xff2c3036 };
inline const juce::Colour metalHi  { 0xff4a5058 };
inline const juce::Colour metalLo  { 0xff1a1d21 };
inline const juce::Colour text     { 0xfff2f4f5 };
inline const juce::Colour dim      { 0xff8b929a };
inline juce::Colour trackColour (int i)
{
    static const juce::uint32 cols[] = { 0xffccff00, 0xff5ee0ff, 0xffff6ec7, 0xffffb347, 0xffa78bfa, 0xff7dffb2, 0xffff7a7a, 0xffe6e6e6 };
    return juce::Colour (cols[(size_t) (i % 8)]);
}
inline juce::Font font (float h, bool bold = false) { return juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain); }
}

class NoizeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NoizeLookAndFeel()
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, Theme::bg);
        setColour (Label::textColourId, Theme::text);
        setColour (TextButton::buttonColourId, Theme::panel2);
        setColour (TextButton::textColourOffId, Theme::text);
        setColour (TextButton::textColourOnId, Colours::black);
        setColour (ComboBox::backgroundColourId, Theme::panel2);
        setColour (ComboBox::textColourId, Theme::text);
        setColour (ComboBox::outlineColourId, Theme::edge);
        setColour (PopupMenu::backgroundColourId, Theme::panel);
        setColour (PopupMenu::textColourId, Theme::text);
        setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour (PopupMenu::highlightedTextColourId, Colours::black);
        setColour (Slider::textBoxTextColourId, Theme::text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (TextEditor::backgroundColourId, Theme::panel2);
        setColour (TextEditor::textColourId, Theme::text);
        setColour (TextEditor::outlineColourId, Theme::edge);
        setColour (ScrollBar::thumbColourId, Theme::metalHi);
        setColour (AlertWindow::backgroundColourId, Theme::panel);
        setColour (AlertWindow::textColourId, Theme::text);
        setColour (AlertWindow::outlineColourId, Theme::edge);
        setColour (ListBox::backgroundColourId, Theme::panel);
        setColour (TreeView::backgroundColourId, Theme::panel);
        setColour (DocumentWindow::textColourId, Theme::text);
        setColour (TooltipWindow::backgroundColourId, Theme::panel2);
        setColour (TooltipWindow::textColourId, Theme::text);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return Theme::font (juce::jmin (15.f, (float) h * 0.55f), true); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto base = on ? Theme::accent : Theme::panel2;
        if (down) base = base.brighter (0.15f); else if (over) base = base.brighter (0.07f);
        if (Theme::quality > 0 && ! on)
            g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), 0, r.getY(), base.darker (0.25f), 0, r.getBottom(), false));
        else g.setColour (base);
        g.fillRoundedRectangle (r, 4.f);
        g.setColour (on ? Theme::accent.brighter (0.3f) : Theme::edge);
        g.drawRoundedRectangle (r, 4.f, 1.f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? juce::Colours::black : Theme::text.withAlpha (b.isEnabled() ? 1.f : 0.4f));
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, true);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        auto c = area.getCentre();
        auto knob = juce::Rectangle<float> (d, d).withCentre (c);
        const float ang = a0 + pos * (a1 - a0);
        const bool hi = Theme::quality > 0;

        juce::Path arc;   // value ring
        arc.addCentredArc (c.x, c.y, d * 0.5f, d * 0.5f, 0.f, a0, a1, true);
        g.setColour (Theme::edge); g.strokePath (arc, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path val; val.addCentredArc (c.x, c.y, d * 0.5f, d * 0.5f, 0.f, a0, ang, true);
        g.setColour (s.isEnabled() ? Theme::accent : Theme::dim);
        g.strokePath (val, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto body = knob.reduced (d * 0.14f);
        if (hi)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (body.translated (1.5f, 3.f).expanded (1.f));
            g.setGradientFill (juce::ColourGradient (Theme::metalHi.brighter (0.5f), body.getX(), body.getY(), Theme::metalLo, body.getRight(), body.getBottom(), true));
            g.fillEllipse (body);
            auto cap = body.reduced (body.getWidth() * 0.16f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2f34), cap.getX(), cap.getY(), juce::Colour (0xff6a7178), cap.getRight(), cap.getBottom(), false));
            g.fillEllipse (cap);
            g.setColour (juce::Colours::white.withAlpha (0.18f));   // chrome rim highlight
            g.drawEllipse (body.reduced (0.5f), 1.f);
        }
        else { g.setColour (Theme::panel2); g.fillEllipse (body); g.setColour (Theme::edge); g.drawEllipse (body, 1.f); }
        juce::Path p; const float r = body.getWidth() * 0.5f;
        p.addRoundedRectangle (-1.5f, -r + 2.f, 3.f, r * 0.45f, 1.5f);
        g.setColour (Theme::accent);
        g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s) override
    {
        if (st == juce::Slider::LinearVertical)
        {
            const float cx = x + w * 0.5f;
            g.setColour (juce::Colours::black); g.fillRoundedRectangle (cx - 3.f, (float) y, 6.f, (float) h, 3.f);
            g.setColour (Theme::edge); g.drawRoundedRectangle (cx - 3.f, (float) y, 6.f, (float) h, 3.f, 1.f);
            g.setColour (Theme::accent.withAlpha (0.8f)); g.fillRoundedRectangle (cx - 2.f, pos, 4.f, y + h - pos, 2.f);
            auto th = juce::Rectangle<float> (cx - 15.f, pos - 9.f, 30.f, 18.f);
            if (Theme::quality > 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (th.translated (0, 2.f), 3.f);
                g.setGradientFill (juce::ColourGradient (Theme::metalHi.brighter (0.4f), 0, th.getY(), Theme::metalLo, 0, th.getBottom(), false));
            } else g.setColour (Theme::metalHi);
            g.fillRoundedRectangle (th, 3.f);
            g.setColour (Theme::accent); g.fillRect (th.getX() + 3.f, th.getCentreY() - 1.f, th.getWidth() - 6.f, 2.f);
            g.setColour (juce::Colours::white.withAlpha (0.25f)); g.drawRoundedRectangle (th, 3.f, 1.f);
        }
        else
        {
            const float cy = y + h * 0.5f;
            g.setColour (juce::Colours::black); g.fillRoundedRectangle ((float) x, cy - 3.f, (float) w, 6.f, 3.f);
            g.setColour (Theme::accent.withAlpha (0.85f)); g.fillRoundedRectangle ((float) x, cy - 2.f, pos - x, 4.f, 2.f);
            g.setColour (Theme::metalHi.brighter (0.3f)); g.fillEllipse (pos - 7.f, cy - 7.f, 14.f, 14.f);
            g.setColour (Theme::accent); g.fillEllipse (pos - 2.5f, cy - 2.5f, 5.f, 5.f);
        }
        juce::ignoreUnused (s);
    }
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override { return LookAndFeel_V4::getSliderLayout (s); }
};

/** Draws the shared panel background with soft metallic depth. */
inline void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {}, float radius = 6.f)
{
    if (Theme::quality > 0)
    {
        g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (r.translated (0.f, 2.f), radius);
        g.setGradientFill (juce::ColourGradient (Theme::panel2.brighter (0.1f), 0, r.getY(), Theme::panel, 0, r.getBottom(), false));
    }
    else g.setColour (Theme::panel);
    g.fillRoundedRectangle (r, radius);
    g.setColour (Theme::edge); g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.f);
    if (title.isNotEmpty()) { g.setColour (Theme::dim); g.setFont (Theme::font (11.f, true)); g.drawText (title.toUpperCase(), r.reduced (8.f, 3.f).withHeight (14.f), juce::Justification::topLeft); }
}
}
