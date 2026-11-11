#pragma once
#include "ViewBase.h"

namespace noize
{
/** Real-time 3D spectrum: log-spaced FFT bars with a depth history, perspective-projected; bass drives the background pulse. */
class VisualizerView : public ViewBase
{
public:
    explicit VisualizerView (AppContext& c) : ViewBase (c), fft (11), window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        fsBtn.setButtonText ("Fullscreen"); addAndMakeVisible (fsBtn); fsBtn.onClick = [this] { if (onFullscreen) onFullscreen(); };
        bands.fill (0.f); bandsSmooth.fill (0.f);
        for (auto& h : history) h.fill (0.f);
        startUiTimer (40);
    }
    std::function<void()> onFullscreen;
    void resized() override { fsBtn.setBounds (getWidth() - 110, 8, 100, 26); }

    void tick() override
    {
        std::array<float, 2048> buf {};
        ctx.engine.getRenderer().copyLatestSamples (buf.data(), 2048);
        std::fill (fftData.begin(), fftData.end(), 0.f);
        std::copy (buf.begin(), buf.end(), fftData.begin());
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        const float sr = (float) juce::jmax (8000.0, ctx.engine.getRenderer().getSampleRate());
        for (int b = 0; b < nBands; ++b)
        {
            const float f0 = 30.f * std::pow (18000.f / 30.f, (float) b / nBands), f1 = 30.f * std::pow (18000.f / 30.f, (float) (b + 1) / nBands);
            const int i0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 / sr * fftSize)), i1 = juce::jlimit (i0 + 1, fftSize / 2, (int) (f1 / sr * fftSize) + 1);
            float m = 0; for (int i = i0; i < i1; ++i) m = juce::jmax (m, fftData[(size_t) i]);
            const float db = juce::Decibels::gainToDecibels (m / (float) (fftSize / 4), -90.f);
            bands[(size_t) b] = juce::jlimit (0.f, 1.f, (db + 70.f) / 70.f);
            bandsSmooth[(size_t) b] = juce::jmax (bands[(size_t) b], bandsSmooth[(size_t) b] * 0.85f);
        }
        float bass = 0; for (int b = 0; b < 6; ++b) bass += bandsSmooth[(size_t) b]; bass /= 6.f;
        bassEnv = juce::jmax (bass, bassEnv * 0.9f);
        if (++hTick % (Theme::quality == 2 ? 2 : 3) == 0) { for (int r = nRows - 1; r > 0; --r) history[(size_t) r] = history[(size_t) r - 1]; history[0] = bandsSmooth; }
        wave = buf; angle += 0.004f + bassEnv * 0.01f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.fillAll (Theme::bg);
        if (Theme::quality > 0)
        {
            const float pulse = bassEnv * bassEnv;
            g.setGradientFill (juce::ColourGradient (Theme::accent.withAlpha (0.10f + 0.35f * pulse), bounds.getCentreX(), bounds.getHeight() * 0.7f, Theme::bg, 0, 0, true));
            g.fillRect (bounds);
        }
        const int rows = Theme::quality == 2 ? nRows : (Theme::quality == 1 ? 8 : 1);
        const float W = bounds.getWidth(), H = bounds.getHeight() * 0.80f;
        const float focal = W * 0.9f, camY = 170.f - bassEnv * 18.f, camZ = 330.f;
        const float yaw = Theme::quality > 0 ? std::sin (angle) * 0.35f : 0.f;
        auto project = [&] (float x, float y, float z) -> juce::Point<float>
        {
            const float xr = x * std::cos (yaw) - z * std::sin (yaw), zr = x * std::sin (yaw) + z * std::cos (yaw);
            const float d = zr + camZ; const float s = focal / juce::jmax (40.f, d);
            return { W * 0.5f + xr * s, H * 0.92f - (y - camY * 0.0f) * s + 20.f };
        };
        const float bw = 8.f, gap = 2.f, totalW = nBands * (bw + gap), zStep = 22.f;
        for (int r = rows - 1; r >= 0; --r)
        {
            const float z = r * zStep; const float fade = 1.f - (float) r / (float) (rows + 2);
            for (int b = 0; b < nBands; ++b)
            {
                const float v = (Theme::quality == 0 ? bandsSmooth : history[(size_t) r])[(size_t) b];
                const float h = 4.f + v * 150.f * (1.f + (b < 6 ? bassEnv * 0.3f : 0.f));
                const float x0 = -totalW * 0.5f + b * (bw + gap), x1 = x0 + bw;
                auto c = Theme::accent.withMultipliedBrightness (0.35f + 0.65f * v).withAlpha (fade * (r == 0 ? 1.f : 0.85f));
                if (Theme::quality == 0)
                {
                    g.setColour (c); g.fillRect (juce::Rectangle<float> (project (x0, h, z).x, project (x0, h, z).y, project (x1, 0, z).x - project (x0, 0, z).x, project (x0, 0, z).y - project (x0, h, z).y));
                    continue;
                }
                const float z1 = z + 14.f;
                auto f0 = project (x0, 0, z), f1 = project (x1, 0, z), f2 = project (x1, h, z), f3 = project (x0, h, z);          // front
                auto t0 = project (x0, h, z1), t1 = project (x1, h, z1);                                                         // top (back edge)
                auto s1 = project (x1, 0, z1);
                juce::Path front; front.startNewSubPath (f0); front.lineTo (f1); front.lineTo (f2); front.lineTo (f3); front.closeSubPath();
                juce::Path top; top.startNewSubPath (f3); top.lineTo (f2); top.lineTo (t1); top.lineTo (t0); top.closeSubPath();
                juce::Path side; side.startNewSubPath (f1); side.lineTo (s1); side.lineTo (t1); side.lineTo (f2); side.closeSubPath();
                g.setGradientFill (juce::ColourGradient (c.brighter (0.1f), f3.x, f3.y, c.darker (0.7f), f0.x, f0.y, false)); g.fillPath (front);
                g.setColour (c.brighter (0.5f)); g.fillPath (top);
                g.setColour (c.darker (0.5f)); g.fillPath (side);
                if (r == 0 && Theme::quality == 2 && v > 0.6f) { g.setColour (c.withAlpha (0.25f)); g.fillEllipse (f3.x - 6.f, f3.y - 6.f, 12.f + (f2.x - f3.x), 12.f); }
            }
        }
        // oscilloscope
        const auto sc = juce::Rectangle<float> (20.f, bounds.getBottom() - 70.f, W - 40.f, 56.f);
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (sc, 6.f);
        juce::Path wp; for (int x = 0; x < (int) sc.getWidth(); ++x) { const float s = wave[(size_t) (x * 2047 / juce::jmax (1, (int) sc.getWidth() - 1))]; const float y = sc.getCentreY() - juce::jlimit (-1.f, 1.f, s * 1.5f) * sc.getHeight() * 0.45f; if (x == 0) wp.startNewSubPath (sc.getX(), y); else wp.lineTo (sc.getX() + (float) x, y); }
        g.setColour (Theme::accent); g.strokePath (wp, juce::PathStrokeType (1.5f));
        g.setColour (Theme::dim); g.setFont (Theme::font (11.f, true)); g.drawText ("SPECTRUM  30 Hz - 18 kHz   |   render quality: " + juce::StringArray { "2D", "Low", "High" }[Theme::quality], 14, 10, 420, 16, juce::Justification::left);
    }

private:
    static constexpr int fftSize = 2048, nBands = 48, nRows = 16;
    juce::dsp::FFT fft; juce::dsp::WindowingFunction<float> window;
    std::array<float, fftSize * 2> fftData {};
    std::array<float, nBands> bands, bandsSmooth; std::array<std::array<float, nBands>, nRows> history;
    std::array<float, 2048> wave {}; float bassEnv = 0.f, angle = 0.f; int hTick = 0; juce::TextButton fsBtn;
};
}
