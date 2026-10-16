#include "Theme.h"
#include <BinaryData.h>

namespace vfp::ui
{
    //==============================================================================
    FontSet *FontSet::instance = nullptr;

    FontSet::FontSet()
    {
        auto load = [](const char *data, int size)
        { return juce::Typeface::createSystemTypefaceFor(data, (size_t)size); };

        sans[0] = load(BinaryData::GeistRegular_ttf, BinaryData::GeistRegular_ttfSize);
        sans[1] = load(BinaryData::GeistMedium_ttf, BinaryData::GeistMedium_ttfSize);
        sans[2] = load(BinaryData::GeistSemiBold_ttf, BinaryData::GeistSemiBold_ttfSize);
        sans[3] = load(BinaryData::GeistBold_ttf, BinaryData::GeistBold_ttfSize);
        mono[0] = load(BinaryData::GeistMonoRegular_ttf, BinaryData::GeistMonoRegular_ttfSize);
        mono[1] = load(BinaryData::GeistMonoMedium_ttf, BinaryData::GeistMonoMedium_ttfSize);
        mono[2] = load(BinaryData::GeistMonoSemiBold_ttf, BinaryData::GeistMonoSemiBold_ttfSize);
        instance = this;
    }

    FontSet::~FontSet()
    {
        if (instance == this)
            instance = nullptr;
    }

    namespace
    {
        juce::Font makeFont(juce::Typeface::Ptr typeface, float px, float trackingEm, bool bold)
        {
            juce::Font font = typeface != nullptr ? juce::Font(typeface) : juce::Font(px, bold ? juce::Font::bold : juce::Font::plain);
            font = font.withPointHeight(px);
            if (trackingEm != 0.0f && font.getHeight() > 0.0f)
                font = font.withExtraKerningFactor(trackingEm * px / font.getHeight());
            return font;
        }
    }

    juce::Font sans(float px, Weight weight, float trackingEm)
    {
        const auto index = (size_t)weight;
        auto typeface = FontSet::instance != nullptr ? FontSet::instance->sans[index] : nullptr;
        return makeFont(typeface, px, trackingEm, weight >= Weight::semibold);
    }

    juce::Font mono(float px, Weight weight, float trackingEm)
    {
        const auto index = juce::jmin((size_t)weight, (size_t)2);
        auto typeface = FontSet::instance != nullptr ? FontSet::instance->mono[index] : nullptr;
        return makeFont(typeface, px, trackingEm, weight >= Weight::semibold);
    }

    //==============================================================================
    juce::String withMinus(const juce::String &text)
    {
        return text.replaceCharacter('-', (juce::juce_wchar)0x2212);
    }

    juce::String formatSignedDb(float value, int decimals)
    {
        const auto rounded = juce::String(std::abs(value), decimals);
        const bool negative = value < 0.0f && rounded.getDoubleValue() != 0.0;
        return (negative ? juce::String::charToString((juce::juce_wchar)0x2212) : juce::String("+")) + rounded;
    }

    juce::String formatFrequency(float hz)
    {
        if (hz >= 1000.0f)
            return juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + "kHz";
        return juce::String(juce::roundToInt(hz)) + " Hz";
    }

    //==============================================================================
    juce::Point<float> polar(juce::Point<float> centre, float radius, float degrees)
    {
        const float a = juce::degreesToRadians(degrees);
        return {centre.x + radius * std::sin(a), centre.y - radius * std::cos(a)};
    }

    juce::Path arc(juce::Point<float> centre, float radius, float fromDegrees, float toDegrees)
    {
        if (toDegrees - fromDegrees < 0.01f)
            toDegrees = fromDegrees + 0.01f;

        juce::Path p;
        p.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                        juce::degreesToRadians(fromDegrees), juce::degreesToRadians(toDegrees), true);
        return p;
    }

    //==============================================================================
    void fillVerticalGradient(juce::Graphics &g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, float radius)
    {
        g.setGradientFill(juce::ColourGradient(top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
        if (radius > 0.0f)
            g.fillRoundedRectangle(r, radius);
        else
            g.fillRect(r);
    }

    namespace
    {
        void drawPanelEdges(juce::Graphics &g, juce::Rectangle<float> r, float radius)
        {
            // inset 0 1px 0 rgba(255,255,255,.03)
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.drawHorizontalLine(juce::roundToInt(r.getY() + 1.0f), r.getX() + radius * 0.6f, r.getRight() - radius * 0.6f);

            g.setColour(Colours::panelBorder);
            g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
        }
    }

    void drawPanel(juce::Graphics &g, juce::Rectangle<float> r, float radius)
    {
        fillVerticalGradient(g, r, Colours::panelTop, Colours::panelBottom, radius);
        drawPanelEdges(g, r, radius);
    }

    void drawPlotPanel(juce::Graphics &g, juce::Rectangle<float> r, float radius)
    {
        fillVerticalGradient(g, r, Colours::plotTop, Colours::plotBottom, radius);
        drawPanelEdges(g, r, radius);
    }

    void drawGlowPanel(juce::Graphics &g, juce::Rectangle<float> r, juce::Point<float> glowCentre, float glowRadius)
    {
        g.setColour(Colours::panelBottom);
        g.fillRoundedRectangle(r, 10.0f);

        juce::ColourGradient glow(juce::Colour(0xff1c2733), glowCentre.x, glowCentre.y,
                                  Colours::panelBottom, glowCentre.x + glowRadius, glowCentre.y, true);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(r, 10.0f);
        drawPanelEdges(g, r, 10.0f);
    }

    void drawTile(juce::Graphics &g, juce::Rectangle<float> r)
    {
        g.setColour(Colours::tile);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(Colours::tileBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    }

    void drawLabel(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> r,
                   juce::Colour colour, float px, float trackingEm, juce::Justification just)
    {
        g.setColour(colour);
        g.setFont(labelFont(px, trackingEm));

        // Geist has no Greek, so a "Δ" in a label is drawn as a vector glyph
        const auto delta = juce::String::charToString((juce::juce_wchar)0x394);
        if (!text.contains(delta))
        {
            g.drawText(text.toUpperCase(), r, just, false);
            return;
        }

        const auto font = labelFont(px, trackingEm);
        const auto before = text.upToFirstOccurrenceOf(delta, false, false).toUpperCase();
        const auto after = text.fromFirstOccurrenceOf(delta, false, false).toUpperCase();
        const float glyphW = px * 0.72f;
        const float w1 = font.getStringWidthFloat(before), w2 = font.getStringWidthFloat(after);
        const float total = w1 + glyphW + w2;
        float x = r.getX();
        if (just.testFlags(juce::Justification::horizontallyCentred))
            x = r.getCentreX() - total * 0.5f;
        else if (just.testFlags(juce::Justification::right))
            x = r.getRight() - total;

        g.drawText(before, juce::Rectangle<float>(x, r.getY(), w1 + 1.0f, r.getHeight()), juce::Justification::centredLeft, false);
        drawDelta(g, juce::Rectangle<float>(x + w1, r.getCentreY() - px * 0.38f, glyphW, px * 0.72f), colour);
        g.drawText(after, juce::Rectangle<float>(x + w1 + glyphW, r.getY(), w2 + 1.0f, r.getHeight()), juce::Justification::centredLeft, false);
    }

    void drawDelta(juce::Graphics &g, juce::Rectangle<float> box, juce::Colour colour)
    {
        const float h = juce::jmin(box.getHeight(), box.getWidth() * 1.05f);
        const auto b = box.withSizeKeepingCentre(h * 0.95f, h);
        juce::Path p;
        p.addTriangle(b.getX(), b.getBottom(), b.getRight(), b.getBottom(), b.getCentreX(), b.getY());
        g.setColour(colour);
        g.strokePath(p, juce::PathStrokeType(juce::jmax(1.0f, h * 0.16f), juce::PathStrokeType::mitered));
    }

    float tagWidth(const juce::String &text, float px, float padX)
    {
        return sans(px, Weight::semibold, 0.1f).getStringWidthFloat(text.toUpperCase()) + padX * 2.0f + 2.0f;
    }

    void drawTag(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> r, TagStyle style, float px)
    {
        juce::Colour text_ = Colours::textSecondary, border = Colours::wellBorder;
        if (style == TagStyle::estimate)
        {
            text_ = Colours::blue;
            border = juce::Colour(0xff2c4366);
        }
        else if (style == TagStyle::measured)
        {
            text_ = Colours::amber;
            border = juce::Colour(0xff4a3b28);
        }

        g.setColour(border);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(text_);
        g.setFont(sans(px, Weight::semibold, 0.1f));
        g.drawText(text.toUpperCase(), r, juce::Justification::centred, false);
    }

    void drawLed(juce::Graphics &g, juce::Point<float> centre, LedState state, float size, bool pulse)
    {
        juce::Colour fill(0xff2b3948), edge(0xff3a4d62);
        switch (state)
        {
        case LedState::on:
            fill = Colours::teal;
            edge = juce::Colour(0xff8ff0e6);
            break;
        case LedState::warn:
            fill = Colours::red;
            edge = juce::Colour(0xfff6a6a3);
            break;
        case LedState::amber:
            fill = Colours::amber;
            edge = juce::Colour(0xffebcb9e);
            break;
        case LedState::dim:
            fill = juce::Colour(0xff1e3a3a);
            edge = Colours::tealBorder;
            break;
        case LedState::off:
            break;
        }

        const float r = size * 0.5f;
        if (state == LedState::on && pulse)
        {
            // keyframes pulse: 0% spread 0 / α .45 → 50% spread 4 / α 0 → 100% back
            const double t = std::fmod(juce::Time::getMillisecondCounterHiRes() / 1600.0, 1.0);
            const float spread = (float)(t < 0.5 ? 8.0 * t : 8.0 * (1.0 - t));
            const float alpha = (float)(t < 0.5 ? 0.45 * (1.0 - 2.0 * t) : 0.45 * (2.0 * t - 1.0));
            g.setColour(Colours::teal.withAlpha(alpha));
            g.fillEllipse(centre.x - r - spread, centre.y - r - spread, (r + spread) * 2.0f, (r + spread) * 2.0f);
        }

        g.setColour(fill);
        g.fillEllipse(centre.x - r, centre.y - r, size, size);
        g.setColour(edge);
        g.drawEllipse(centre.x - r + 0.5f, centre.y - r + 0.5f, size - 1.0f, size - 1.0f, 1.0f);
    }

    void drawPip(juce::Graphics &g, juce::Point<float> centre, juce::Colour colour, float size)
    {
        g.setColour(colour.withMultipliedAlpha(0.9f));
        g.fillEllipse(centre.x - size * 0.5f, centre.y - size * 0.5f, size, size);
    }

    void drawFocusRing(juce::Graphics &g, juce::Rectangle<float> r, float radius)
    {
        g.setColour(Colours::focus);
        g.drawRoundedRectangle(r.expanded(3.0f), radius + 3.0f, 2.0f);
    }

    //==============================================================================
    namespace
    {
        juce::Path svg(const char *d)
        {
            return juce::Drawable::parseSVGPath(d);
        }

        juce::Path circle(float cx, float cy, float r)
        {
            juce::Path p;
            p.addEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
            return p;
        }

        struct IconSpec
        {
            juce::Path stroke;
            float width;
            juce::Path filledBackground; // drawn in the background colour, then stroked
            juce::Path filled;           // drawn in the icon colour
        };

        IconSpec makeIcon(Icon icon)
        {
            switch (icon)
            {
            case Icon::chevronLeft:
                return {svg("M10 4 6 8 10 12"), 1.6f, {}, {}};
            case Icon::chevronRight:
                return {svg("M6 4 10 8 6 12"), 1.6f, {}, {}};
            case Icon::chevronDown:
                return {svg("M4 6 8 10 12 6"), 1.6f, {}, {}};
            case Icon::check:
                return {svg("M3.5 8.5 6.5 11.5 12.5 4.5"), 1.6f, {}, {}};
            case Icon::save:
            {
                auto p = svg("M3 2.5h7.5l2.5 2.5v8.5H3z");
                p.addPath(svg("M5.5 2.5v3h4.5v-3M5.5 13.5V9.5h5v4"));
                return {p, 1.4f, {}, {}};
            }
            case Icon::reset:
            {
                auto p = svg("M3.2 8a4.8 4.8 0 1 0 1.5-3.5");
                p.addPath(svg("M3 2.5v3h3"));
                return {p, 1.5f, {}, {}};
            }
            case Icon::copy:
            {
                juce::Path p;
                p.addRoundedRectangle(5.5f, 5.5f, 8.0f, 8.0f, 1.5f);
                p.addPath(svg("M2.5 10.5V4a1.5 1.5 0 0 1 1.5-1.5h6.5"));
                return {p, 1.4f, {}, {}};
            }
            case Icon::undo:
            {
                auto p = svg("M6 4 3 7 6 10");
                p.addPath(svg("M3 7h6.5a3.5 3.5 0 0 1 0 7H8"));
                return {p, 1.5f, {}, {}};
            }
            case Icon::redo:
            {
                auto p = svg("M10 4 13 7 10 10");
                p.addPath(svg("M13 7H6.5a3.5 3.5 0 0 0 0 7H8"));
                return {p, 1.5f, {}, {}};
            }
            case Icon::power:
            {
                auto p = svg("M8 2v5.5");
                p.addPath(svg("M4.6 4.4a5 5 0 1 0 6.8 0"));
                return {p, 1.8f, {}, {}};
            }
            case Icon::settings:
            {
                auto knobs = circle(6.0f, 5.0f, 1.7f);
                knobs.addPath(circle(10.5f, 11.0f, 1.7f));
                auto p = svg("M2.5 5h11M2.5 11h11");
                return {p, 1.5f, knobs, {}};
            }
            case Icon::help:
            {
                auto p = circle(8.0f, 8.0f, 6.0f);
                p.addPath(svg("M6.3 6.3a1.8 1.8 0 1 1 2.5 1.6c-.6.3-.8.6-.8 1.3"));
                return {p, 1.5f, {}, circle(8.0f, 11.3f, 0.6f)};
            }
            case Icon::expand:
            {
                auto p = svg("M9.5 2.5h4v4M13.5 2.5 9 7M6.5 13.5h-4v-4M2.5 13.5 7 9");
                return {p, 1.5f, {}, {}};
            }
            }
            return {};
        }
    }

    void drawIcon(juce::Graphics &g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, juce::Colour background)
    {
        const auto spec = makeIcon(icon);
        const float scale = juce::jmin(area.getWidth(), area.getHeight()) / 16.0f;
        const auto transform = juce::AffineTransform::scale(scale)
                                   .translated(area.getCentreX() - 8.0f * scale, area.getCentreY() - 8.0f * scale);
        const juce::PathStrokeType stroke(spec.width * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        g.setColour(colour);
        g.strokePath(spec.stroke, stroke, transform);

        if (!spec.filledBackground.isEmpty())
        {
            g.setColour(background);
            g.fillPath(spec.filledBackground, transform);
            g.setColour(colour);
            g.strokePath(spec.filledBackground, stroke, transform);
        }

        if (!spec.filled.isEmpty())
            g.fillPath(spec.filled, transform);
    }

    void drawLogo(juce::Graphics &g, juce::Rectangle<float> area)
    {
        const juce::Path frame = svg("M3 8V5.5A2.5 2.5 0 0 1 5.5 3H8M18 3h2.5A2.5 2.5 0 0 1 23 5.5V8M23 18v2.5a2.5 2.5 0 0 1-2.5 2.5H18M8 23H5.5A2.5 2.5 0 0 1 3 20.5V18");
        const juce::Path bars = svg("M9 11v4M13 7.5v11M17 10v6");

        const float scale = area.getWidth() / 26.0f;
        const auto transform = juce::AffineTransform::scale(scale).translated(area.getX(), area.getY());

        g.setColour(Colours::teal);
        g.strokePath(frame, juce::PathStrokeType(1.6f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
        g.setColour(Colours::textPrimary);
        g.strokePath(bars, juce::PathStrokeType(2.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
    }
}
