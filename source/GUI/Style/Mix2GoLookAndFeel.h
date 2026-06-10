#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace mix2go::gui
{
/// Palette + control styling for the Mix2Go streaming UI. Matches the
/// companion Flutter app (dark surfaces, orange→pink→purple accent).
class Mix2GoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ── Palette ──────────────────────────────────────────────────────────
    static juce::Colour bg()         { return juce::Colour(0xff0c0c0e); }
    static juce::Colour surface1()   { return juce::Colour(0xff141416); }
    static juce::Colour surface2()   { return juce::Colour(0xff1a1a1e); }
    static juce::Colour borderDim()  { return juce::Colour::fromFloatRGBA(1, 1, 1, 0.07f); }
    static juce::Colour borderNorm() { return juce::Colour::fromFloatRGBA(1, 1, 1, 0.13f); }
    static juce::Colour textPrim()   { return juce::Colour::fromFloatRGBA(1, 1, 1, 0.92f); }
    static juce::Colour textMuted()  { return juce::Colour::fromFloatRGBA(1, 1, 1, 0.38f); }

    static juce::Colour gradStart()  { return juce::Colour(0xfff97316); }
    static juce::Colour gradMid()    { return juce::Colour(0xffe8445a); }
    static juce::Colour gradEnd()    { return juce::Colour(0xffc026d3); }

    // ── State colours ────────────────────────────────────────────────────
    static juce::Colour stStreaming()  { return juce::Colour(0xff22c55e); }
    static juce::Colour stSearching()  { return juce::Colour(0xfff97316); }
    static juce::Colour stConnecting() { return juce::Colour(0xfffbbf24); }
    static juce::Colour stError()      { return juce::Colour(0xfff87171); }
    static juce::Colour stStopped()    { return juce::Colour::fromFloatRGBA(1, 1, 1, 0.25f); }

    /// Accent gradient across a rectangle (orange → pink → purple).
    static juce::ColourGradient accent(juce::Rectangle<float> r, float alpha = 1.0f)
    {
        juce::ColourGradient g(gradStart().withAlpha(alpha), r.getTopLeft(),
                               gradEnd().withAlpha(alpha), r.getBottomRight(), false);
        g.addColour(0.5, gradMid().withAlpha(alpha));
        return g;
    }

    /// Barlow Condensed Bold (embedded) — matches the companion Flutter app.
    static juce::Font display(float size)
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
            BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);
        return juce::Font(tf).withHeight(size);
    }

    // ── Button ───────────────────────────────────────────────────────────
    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool stop = (bool) b.getProperties().getWithDefault("m2g_stop", false);

        juce::Colour fill   = stop ? stError().withAlpha(0.10f) : surface2();
        juce::Colour border = stop ? stError().withAlpha(0.30f) : borderNorm();
        if (highlighted && b.isEnabled()) fill = fill.brighter(0.10f);
        if (down) r = r.reduced(0.8f);

        g.setColour(fill);   g.fillRoundedRectangle(r, 9.0f);
        g.setColour(border); g.drawRoundedRectangle(r, 9.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool stop = (bool) b.getProperties().getWithDefault("m2g_stop", false);
        auto col = stop ? stError() : textPrim();
        if (! b.isEnabled()) col = textMuted();
        g.setColour(col);
        g.setFont(display(15.0f));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};
} // namespace mix2go::gui
