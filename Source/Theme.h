#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Design tokens and drawing helpers for the VoiceFrame Pro interface.
 * Colours, type and geometry follow the "VOICEFRAME PRO — Plugin UI" design canvas.
 */
namespace vfp::ui
{
    namespace Colours
    {
        // Grounds
        inline const juce::Colour background{0xff0d1218};
        inline const juce::Colour backgroundTop{0xff121a23};
        inline const juce::Colour headerTop{0xff19222d};
        inline const juce::Colour headerBottom{0xff141b24};
        inline const juce::Colour footerTop{0xff151c25};
        inline const juce::Colour footerBottom{0xff121820};
        inline const juce::Colour statusBar{0xff0b1015};
        inline const juce::Colour statusBorder{0xff1a232d};
        inline const juce::Colour panelTop{0xff19222d};
        inline const juce::Colour panelBottom{0xff161e27};
        inline const juce::Colour panelBorder{0xff243140};
        inline const juce::Colour plotTop{0xff121a23};
        inline const juce::Colour plotBottom{0xff0f161e};
        inline const juce::Colour well{0xff0f151c};
        inline const juce::Colour wellBorder{0xff2b3948};
        inline const juce::Colour wellBorderHover{0xff3a4d62};
        inline const juce::Colour tile{0xff121921};
        inline const juce::Colour tileBorder{0xff1f2a36};
        inline const juce::Colour hover{0xff202b37};
        inline const juce::Colour selected{0xff243140};
        inline const juce::Colour popup{0xff1b2430};
        inline const juce::Colour popupBorder{0xff34465a};
        inline const juce::Colour trackDark{0xff0b1016};
        inline const juce::Colour gridMinor{0xff1c2631};
        inline const juce::Colour gridMajor{0xff263343};

        // Text
        inline const juce::Colour textPrimary{0xffe9f1f6};
        inline const juce::Colour textBody{0xffd5e1ea};
        inline const juce::Colour textSoft{0xffcfe0f5};
        inline const juce::Colour textSecondary{0xff94a5b7};
        inline const juce::Colour textTertiary{0xff8394a7};
        inline const juce::Colour textHint{0xff6f8196};
        inline const juce::Colour textAxis{0xff6b7c8f};
        inline const juce::Colour textDisabled{0xff5e6e80};

        // Accents
        inline const juce::Colour teal{0xff55d9ce};
        inline const juce::Colour tealLight{0xff7fede3};
        inline const juce::Colour tealSoft{0xffb8f2ec};
        inline const juce::Colour tealText{0xff9fede5};
        inline const juce::Colour tealBorder{0xff2d6964};
        inline const juce::Colour tealFill{0xff132a2a};
        inline const juce::Colour blue{0xff78afff};
        inline const juce::Colour blueDark{0xff3c6bb0};
        inline const juce::Colour amber{0xffd7ab70};
        inline const juce::Colour amberLight{0xfff0cfa0};
        inline const juce::Colour amberBorder{0xff7a5c33};
        inline const juce::Colour amberFill{0xff3a2c1a};
        inline const juce::Colour red{0xffed7773};
        inline const juce::Colour inputCurve{0xff8da2b8};
        inline const juce::Colour focus{0xff78afff};

        // Band colours, low to high
        inline const juce::Colour bands[6] = {juce::Colour(0xff5fd3c8), juce::Colour(0xff5bc3d6), juce::Colour(0xff66b2e4),
                                              juce::Colour(0xff78a9fa), juce::Colour(0xff8ea2f0), juce::Colour(0xffa39ae4)};
    }

    //==============================================================================
    enum class Weight
    {
        regular,
        medium,
        semibold,
        bold
    };

    /** Embedded Geist / Geist Mono typefaces. Keep one alive (via SharedResourcePointer) while UI exists. */
    class FontSet
    {
    public:
        FontSet();
        ~FontSet();

        juce::Typeface::Ptr sans[4];
        juce::Typeface::Ptr mono[3];

        static FontSet *instance;
    };

    /** Geist at a CSS-style pixel size (em size), with optional letter-spacing in em. */
    juce::Font sans(float px, Weight weight = Weight::regular, float trackingEm = 0.0f);

    /** Geist Mono at a CSS-style pixel size (em size). */
    juce::Font mono(float px, Weight weight = Weight::regular, float trackingEm = 0.0f);

    /** The small uppercase label style used across the design (".lbl"). */
    inline juce::Font labelFont(float px = 10.0f, float trackingEm = 0.12f) { return sans(px, Weight::semibold, trackingEm); }

    //==============================================================================
    // Formatting
    inline juce::String utf8(const char *text) { return juce::String::fromUTF8(text); }
    juce::String withMinus(const juce::String &text);           // '-' -> U+2212
    juce::String formatSignedDb(float value, int decimals = 1); // "+0.0", "−3.2"
    juce::String formatFrequency(float hz);                     // "120 Hz", "1.50kHz"

    //==============================================================================
    // Geometry: design angles are in degrees, 0 = 12 o'clock, clockwise.
    inline float knobAngleDegrees(float proportion) { return -135.0f + 270.0f * proportion; }
    juce::Point<float> polar(juce::Point<float> centre, float radius, float degrees);
    juce::Path arc(juce::Point<float> centre, float radius, float fromDegrees, float toDegrees);

    //==============================================================================
    // Drawing
    void fillVerticalGradient(juce::Graphics &g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, float radius = 0.0f);
    void drawPanel(juce::Graphics &g, juce::Rectangle<float> r, float radius = 10.0f);
    void drawPlotPanel(juce::Graphics &g, juce::Rectangle<float> r, float radius = 10.0f);
    void drawGlowPanel(juce::Graphics &g, juce::Rectangle<float> r, juce::Point<float> glowCentre, float glowRadius);
    void drawTile(juce::Graphics &g, juce::Rectangle<float> r);
    void drawLabel(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> r,
                   juce::Colour colour = Colours::textSecondary, float px = 10.0f, float trackingEm = 0.12f,
                   juce::Justification just = juce::Justification::centredLeft);

    /** "Δ" as a vector glyph (the embedded fonts have no Greek). */
    void drawDelta(juce::Graphics &g, juce::Rectangle<float> box, juce::Colour colour);

    enum class TagStyle
    {
        plain,
        estimate,
        measured
    };
    float tagWidth(const juce::String &text, float px = 9.0f, float padX = 5.0f);
    void drawTag(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> r, TagStyle style, float px = 9.0f);

    enum class LedState
    {
        off,
        on,
        warn,
        amber,
        dim
    };
    void drawLed(juce::Graphics &g, juce::Point<float> centre, LedState state, float size = 9.0f, bool pulse = true);
    void drawPip(juce::Graphics &g, juce::Point<float> centre, juce::Colour colour, float size = 6.0f);
    void drawFocusRing(juce::Graphics &g, juce::Rectangle<float> r, float radius);

    //==============================================================================
    // Icons (16×16 design grid), stroked in the given colour
    enum class Icon
    {
        chevronLeft,
        chevronRight,
        chevronDown,
        save,
        reset,
        copy,
        undo,
        redo,
        power,
        settings,
        help,
        expand,
        check
    };
    void drawIcon(juce::Graphics &g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, juce::Colour background = Colours::headerBottom);
    void drawLogo(juce::Graphics &g, juce::Rectangle<float> area);
}
