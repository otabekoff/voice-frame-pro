#include "Widgets.h"

namespace vfp::ui
{
    namespace
    {
        /** Baseline for text set in a CSS line box of the given height (line-height: 1 → height = px). */
        float baselineIn(const juce::Font &font, float top, float lineHeight)
        {
            return top + 0.5f * (lineHeight + font.getAscent() - font.getDescent());
        }

        void strokeArc(juce::Graphics &g, const juce::Path &p, float width)
        {
            g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void drawLine(juce::Graphics &g, juce::Point<float> a, juce::Point<float> b, float width)
        {
            juce::Path p;
            p.startNewSubPath(a);
            p.lineTo(b);
            strokeArc(g, p, width);
        }

        /** "68" in a large mono face followed by a smaller unit, centred on cx. */
        void drawValueWithUnit(juce::Graphics &g, const juce::String &value, const juce::String &unit,
                               float cx, float top, float valuePx, float unitPx, Weight valueWeight = Weight::medium)
        {
            const auto valueFont = mono(valuePx, valueWeight);
            const auto unitFont = mono(unitPx);
            const float w1 = valueFont.getStringWidthFloat(value);
            const float w2 = unitFont.getStringWidthFloat(unit) + 1.0f;
            const float x = cx - 0.5f * (w1 + w2);
            const float baseline = baselineIn(valueFont, top, valuePx);

            g.setColour(Colours::textPrimary);
            g.setFont(valueFont);
            g.drawSingleLineText(value, juce::roundToInt(x), juce::roundToInt(baseline));
            g.setColour(Colours::textSecondary);
            g.setFont(unitFont);
            g.drawSingleLineText(unit, juce::roundToInt(x + w1 + 1.0f), juce::roundToInt(baseline));
        }
    }

    //==============================================================================
    // ParameterControl
    //==============================================================================
    class ParameterValueInterface : public juce::AccessibilityRangedNumericValueInterface
    {
    public:
        explicit ParameterValueInterface(ParameterControl &c) : owner(c) {}

        bool isReadOnly() const override { return !owner.isEnabled(); }
        double getCurrentValue() const override { return owner.value; }

        void setValue(double newValue) override
        {
            owner.attachment.setValueAsCompleteGesture((float)newValue);
        }

        AccessibleValueRange getRange() const override
        {
            const auto &range = owner.param.getNormalisableRange();
            return {{range.start, range.end}, range.interval > 0.0f ? range.interval : (range.end - range.start) / 100.0};
        }

    private:
        ParameterControl &owner;
    };

    ParameterControl::ParameterControl(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager)
        : param(parameter),
          value(parameter.convertFrom0to1(parameter.getValue())),
          attachment(parameter, [this](float v)
                     {
                         value = v;
                         repaint();
                         if (onValueChange)
                             onValueChange(); },
                     undoManager)
    {
        setWantsKeyboardFocus(true);
        setTitle(parameter.getName(64));
        attachment.sendInitialUpdate();
    }

    void ParameterControl::mouseEnter(const juce::MouseEvent &)
    {
        hovered = true;
        repaint();
    }

    void ParameterControl::mouseExit(const juce::MouseEvent &)
    {
        hovered = false;
        repaint();
    }

    void ParameterControl::focusGained(FocusChangeType cause)
    {
        keyboardFocused = cause == focusChangedByTabKey;
        repaint();
    }

    void ParameterControl::focusLost(FocusChangeType)
    {
        keyboardFocused = false;
        repaint();
    }

    std::unique_ptr<juce::AccessibilityHandler> ParameterControl::createAccessibilityHandler()
    {
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
            juce::AccessibilityHandler::Interfaces{std::make_unique<ParameterValueInterface>(*this)});
    }

    void ParameterControl::beginDrag()
    {
        attachment.beginGesture();
        dragging = true;
        repaint();
    }

    void ParameterControl::dragToProportion(float proportion)
    {
        attachment.setValueAsPartOfGesture(param.convertFrom0to1(juce::jlimit(0.0f, 1.0f, proportion)));
    }

    void ParameterControl::endDrag()
    {
        if (dragging)
            attachment.endGesture();
        dragging = false;
        repaint();
    }

    void ParameterControl::setProportionAsGesture(float proportion)
    {
        attachment.setValueAsCompleteGesture(param.convertFrom0to1(juce::jlimit(0.0f, 1.0f, proportion)));
    }

    void ParameterControl::stepBy(float proportionDelta)
    {
        setProportionAsGesture(getProportion() + proportionDelta);
    }

    void ParameterControl::resetToDefault()
    {
        attachment.setValueAsCompleteGesture(param.convertFrom0to1(param.getDefaultValue()));
    }

    //==============================================================================
    // Knob
    //==============================================================================
    Knob::Knob(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager, Style s)
        : ParameterControl(parameter, undoManager), style(s)
    {
        arcColour = style == Style::mini ? Colours::blue : Colours::teal;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void Knob::setArcColour(juce::Colour c)
    {
        arcColour = c;
        repaint();
    }

    void Knob::mouseDown(const juce::MouseEvent &e)
    {
        if (!isEnabled())
            return;
        lastDragY = e.position.y;
        dragProportion = getProportion();
        beginDrag();
    }

    void Knob::mouseDrag(const juce::MouseEvent &e)
    {
        if (!isDragging())
            return;

        // Pixels of vertical travel for the full range, matching the design's drag rates
        float pixels = 200.0f;
        switch (style)
        {
        case Style::mini:
            pixels = 320.0f;
            break;
        case Style::gate:
            pixels = 150.0f;
            break;
        default:
            break;
        }

        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        dragProportion = juce::jlimit(0.0f, 1.0f, dragProportion + (lastDragY - e.position.y) / pixels * fine);
        lastDragY = e.position.y;
        dragToProportion(dragProportion);
    }

    void Knob::mouseUp(const juce::MouseEvent &)
    {
        endDrag();
    }

    void Knob::mouseDoubleClick(const juce::MouseEvent &)
    {
        if (isEnabled())
            resetToDefault();
    }

    void Knob::mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &wheel)
    {
        if (isEnabled() && wheel.deltaY != 0.0f)
            stepBy(wheel.deltaY > 0.0f ? 0.01f : -0.01f);
    }

    bool Knob::keyPressed(const juce::KeyPress &key)
    {
        const float step = key.getModifiers().isShiftDown() ? 0.01f : 0.05f;
        if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::rightKey))
        {
            stepBy(step);
            return true;
        }
        if (key.isKeyCode(juce::KeyPress::downKey) || key.isKeyCode(juce::KeyPress::leftKey))
        {
            stepBy(-step);
            return true;
        }
        return false;
    }

    void Knob::paint(juce::Graphics &g)
    {
        switch (style)
        {
        case Style::hero:
            paintHero(g, 204.0f, false);
            break;
        case Style::heroCompact:
            paintHero(g, 176.0f, true);
            break;
        case Style::global:
            paintGlobal(g);
            break;
        default:
            paintSmall(g);
            break;
        }

        if (showFocusRing())
            drawFocusRing(g, getLocalBounds().toFloat().reduced(3.0f), getWidth() * 0.5f);
    }

    namespace
    {
        void fillKnobBody(juce::Graphics &g, juce::Point<float> c, float r, juce::Colour stroke)
        {
            // radialGradient cx=50% cy=34% r=70% of the body's bounding box
            const float d = r * 2.0f;
            juce::ColourGradient body(juce::Colour(0xff2c3a49), c.x, c.y - r + 0.34f * d,
                                      juce::Colour(0xff131a22), c.x, c.y - r + 0.34f * d + 0.7f * d, true);
            body.addColour(0.55, juce::Colour(0xff1d2732));
            g.setGradientFill(body);
            g.fillEllipse(c.x - r, c.y - r, d, d);
            g.setColour(stroke);
            g.drawEllipse(c.x - r, c.y - r, d, d, 1.0f);
        }

        void drawHalo(juce::Graphics &g, const juce::Path &arcPath, float width, float blur, float alpha)
        {
            juce::Path stroked;
            juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(stroked, arcPath);
            juce::DropShadow(Colours::teal.withAlpha(alpha), juce::roundToInt(blur * 2.0f), {}).drawForPath(g, stroked);
        }

        void strokeGradientArc(juce::Graphics &g, const juce::Path &arcPath, juce::Rectangle<float> box, float width)
        {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3fbdb3), box.getBottomLeft(),
                                                   juce::Colour(0xff78e6dc), box.getTopRight(), false));
            strokeArc(g, arcPath, width);
        }
    }

    void Knob::paintHero(juce::Graphics &g, float basis, bool compact)
    {
        const float k = (float)getWidth() / basis;
        const juce::Point<float> c(basis * 0.5f * k, basis * 0.5f * k);
        const float valueDeg = knobAngleDegrees(getProportion());
        const juce::Colour bodyStroke = isDragging() ? Colours::teal : (isHovered() ? juce::Colour(0xff4a5f76) : juce::Colour(0xff33445a));

        // Ticks
        juce::Path minor, major;
        for (int i = 0; i <= 40; ++i)
        {
            const float deg = -135.0f + 270.0f * (float)i / 40.0f;
            const bool isMajor = i % 10 == 0;
            const float r0 = (compact ? (isMajor ? 78.0f : 80.0f) : (isMajor ? 91.0f : 93.0f)) * k;
            const float r1 = (compact ? 84.0f : 98.0f) * k;
            auto &p = isMajor && !compact ? major : minor;
            p.startNewSubPath(polar(c, r0, deg));
            p.lineTo(polar(c, r1, deg));
        }
        g.setColour(compact ? juce::Colour(0xff34465a) : juce::Colour(0xff2f3e4e));
        strokeArc(g, minor, 1.2f * k);
        g.setColour(juce::Colour(0xff4a5d72));
        strokeArc(g, major, 1.6f * k);

        // Track, halo and value arc
        const float trackR = (compact ? 72.0f : 84.0f) * k;
        const auto track = arc(c, trackR, -135.0f, 135.0f);
        g.setColour(Colours::trackDark);
        strokeArc(g, track, (compact ? 8.0f : 9.0f) * k);
        g.setColour(juce::Colour(0xff1e2934));
        strokeArc(g, track, (compact ? 4.5f : 5.0f) * k);

        const auto valueArc = arc(c, trackR, -135.0f, valueDeg);
        const float haloAlpha = isDragging() ? 0.55f : (isHovered() ? 0.34f : 0.22f);
        drawHalo(g, valueArc, (compact ? 8.0f : 9.0f) * k, (compact ? 3.5f : 4.0f) * k, haloAlpha);
        strokeGradientArc(g, valueArc, getLocalBounds().toFloat(), (compact ? 4.5f : 5.0f) * k);

        // Default (50 %) mark
        if (!compact)
        {
            const float defDeg = knobAngleDegrees(param.getDefaultValue());
            juce::Path mark;
            mark.addTriangle(polar(c, 99.5f * k, defDeg - 2.2f), polar(c, 99.5f * k, defDeg + 2.2f), polar(c, 95.0f * k, defDeg));
            g.setColour(Colours::textSecondary);
            g.fillPath(mark);

            const float ringR = 72.0f * k;
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a4b5d), 0.0f, c.y - ringR, juce::Colour(0xff18202a), 0.0f, c.y + ringR, false));
            g.fillEllipse(c.x - ringR, c.y - ringR, ringR * 2.0f, ringR * 2.0f);
        }

        // Body
        const float bodyR = (compact ? 60.0f : 70.0f) * k;
        fillKnobBody(g, c, bodyR, bodyStroke);

        if (!compact)
        {
            const float innerR = 61.0f * k;
            g.setColour(Colours::well.withAlpha(0.55f));
            g.drawEllipse(c.x - innerR, c.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);
        }

        const float hlY = (compact ? 64.0f : 74.0f) * k, hlRx = (compact ? 38.0f : 46.0f) * k, hlRy = (compact ? 18.0f : 22.0f) * k;
        g.setColour(juce::Colours::white.withAlpha(0.035f));
        g.fillEllipse(c.x - hlRx, hlY - hlRy, hlRx * 2.0f, hlRy * 2.0f);

        // Pointer
        const auto p1 = polar(c, (compact ? 38.0f : 44.0f) * k, valueDeg);
        const auto p2 = polar(c, (compact ? 53.0f : 62.0f) * k, valueDeg);
        g.setColour(Colours::trackDark);
        drawLine(g, p1, p2, (compact ? 5.5f : 6.0f) * k);
        g.setColour(Colours::tealLight);
        drawLine(g, p1, p2, (compact ? 2.8f : 3.0f) * k);

        // Readout
        const float textTop = (compact ? 66.0f : 76.0f) * k;
        const float valuePx = (compact ? 30.0f : 36.0f) * k;
        drawValueWithUnit(g, juce::String(juce::roundToInt(value)), "%", c.x, textTop, valuePx, (compact ? 14.0f : 16.0f) * k);

        const float intensityPx = (compact ? 8.5f : 9.0f) * k;
        g.setColour(Colours::textTertiary);
        g.setFont(sans(intensityPx, Weight::semibold, 0.16f));
        g.drawText("INTENSITY", juce::Rectangle<float>(0.0f, textTop + valuePx + (compact ? 2.0f : 3.0f) * k, (float)getWidth(), intensityPx * 1.3f),
                   juce::Justification::centredTop, false);
    }

    void Knob::paintGlobal(juce::Graphics &g)
    {
        const float k = (float)getWidth() / 128.0f;
        const juce::Point<float> c(64.0f * k, 64.0f * k);
        const float valueDeg = knobAngleDegrees(getProportion());

        const auto track = arc(c, 54.0f * k, -135.0f, 135.0f);
        g.setColour(Colours::trackDark);
        strokeArc(g, track, 7.0f * k);
        g.setColour(juce::Colour(0xff1e2934));
        strokeArc(g, track, 4.0f * k);

        const auto valueArc = arc(c, 54.0f * k, -135.0f, valueDeg);
        drawHalo(g, valueArc, 7.0f * k, 3.0f * k, isDragging() ? 0.55f : (isHovered() ? 0.34f : 0.22f));
        strokeGradientArc(g, valueArc, getLocalBounds().toFloat(), 4.0f * k);

        fillKnobBody(g, c, 44.0f * k, isDragging() ? Colours::teal : (isHovered() ? juce::Colour(0xff4a5f76) : juce::Colour(0xff33445a)));

        g.setColour(Colours::tealLight);
        drawLine(g, polar(c, 26.0f * k, valueDeg), polar(c, 40.0f * k, valueDeg), 2.5f * k);

        drawValueWithUnit(g, juce::String(juce::roundToInt(value)), "%", c.x, 52.0f * k, 22.0f * k, 11.0f * k, Weight::regular);
    }

    void Knob::paintSmall(juce::Graphics &g)
    {
        struct Geometry
        {
            float basis, trackR, trackW, arcW, bodyR, p0, p1, pointerW;
            juce::Colour body;
        };

        Geometry geo{36.0f, 15.0f, 4.0f, 2.5f, 11.0f, 4.0f, 10.0f, 2.0f, juce::Colour(0xff202b37)};
        if (style == Style::gate)
            geo = {34.0f, 14.0f, 4.0f, 2.4f, 10.0f, 3.5f, 9.0f, 1.8f, juce::Colour(0xff202b37)};
        else if (style == Style::band)
            geo = {92.0f, 40.0f, 6.0f, 3.5f, 31.0f, 14.0f, 27.0f, 2.2f, juce::Colour(0xff1f2a36)};

        const float k = (float)getWidth() / geo.basis;
        const juce::Point<float> c(geo.basis * 0.5f * k, geo.basis * 0.5f * k);
        const float valueDeg = knobAngleDegrees(getProportion());

        if (!isEnabled())
            g.beginTransparencyLayer(0.4f);

        g.setColour(Colours::trackDark);
        strokeArc(g, arc(c, geo.trackR * k, -135.0f, 135.0f), geo.trackW * k);

        // Bipolar knobs (gains) draw their arc from the zero position
        const bool bipolar = style == Style::mini && param.getNormalisableRange().start < 0.0f && param.getNormalisableRange().end > 0.0f;
        float fromDeg = -135.0f, toDeg = valueDeg;
        if (bipolar)
        {
            const float zeroDeg = knobAngleDegrees(param.convertTo0to1(0.0f));
            fromDeg = juce::jmin(zeroDeg, valueDeg);
            toDeg = juce::jmax(zeroDeg, valueDeg);
        }
        g.setColour(arcColour);
        strokeArc(g, arc(c, geo.trackR * k, fromDeg, toDeg), geo.arcW * k);

        const float r = geo.bodyR * k;
        g.setColour(geo.body);
        g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour(isDragging() || isHovered() ? juce::Colour(0xff4a5f76) : juce::Colour(0xff33445a));
        g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setColour(Colours::textPrimary);
        drawLine(g, polar(c, geo.p0 * k, valueDeg), polar(c, geo.p1 * k, valueDeg), geo.pointerW * k);

        if (style == Style::band)
        {
            const auto font = mono(15.0f * k);
            g.setFont(font);
            g.drawText(juce::String(juce::roundToInt(value)) + "%", juce::Rectangle<float>(0.0f, 39.0f * k, (float)getWidth(), 15.0f * k),
                       juce::Justification::centred, false);
        }

        if (!isEnabled())
            g.endTransparencyLayer();
    }

    //==============================================================================
    // Fader
    //==============================================================================
    Fader::Fader(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager, bool isBipolar)
        : ParameterControl(parameter, undoManager), bipolar(isBipolar)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    float Fader::proportionAt(float x) const
    {
        const float trackW = (float)getWidth() - thumbOverhang * 2.0f;
        return juce::jlimit(0.0f, 1.0f, (x - thumbOverhang) / juce::jmax(1.0f, trackW));
    }

    void Fader::paint(juce::Graphics &g)
    {
        if (!isEnabled())
            g.beginTransparencyLayer(0.35f);

        const auto track = juce::Rectangle<float>(thumbOverhang, 7.0f, (float)getWidth() - thumbOverhang * 2.0f, 4.0f);
        g.setColour(Colours::trackDark);
        g.fillRoundedRectangle(track, 2.0f);
        g.setColour(juce::Colour(0xff1e2934));
        g.drawRoundedRectangle(track.reduced(0.5f), 2.0f, 1.0f);

        const float p = getProportion();
        const float x = track.getX() + p * track.getWidth();
        const float from = bipolar ? track.getCentreX() : track.getX();
        g.setColour(Colours::blue);
        g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(from, x), track.getY(), std::abs(x - from), track.getHeight()), 2.0f);

        const auto thumb = juce::Rectangle<float>(x - 7.0f, 2.0f, 14.0f, 14.0f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(thumb.translated(0.0f, 1.0f).expanded(0.5f));
        g.setColour(juce::Colour(0xff2a3747));
        g.fillEllipse(thumb);
        g.setColour(isHovered() || isDragging() ? Colours::textSecondary : juce::Colour(0xff5a7088));
        g.drawEllipse(thumb.reduced(0.5f), 1.0f);

        if (!isEnabled())
            g.endTransparencyLayer();

        if (showFocusRing())
            drawFocusRing(g, getLocalBounds().toFloat().reduced(2.0f), 4.0f);
    }

    void Fader::mouseDown(const juce::MouseEvent &e)
    {
        if (!isEnabled())
            return;
        beginDrag();
        dragToProportion(proportionAt(e.position.x));
    }

    void Fader::mouseDrag(const juce::MouseEvent &e)
    {
        if (isDragging())
            dragToProportion(proportionAt(e.position.x));
    }

    void Fader::mouseUp(const juce::MouseEvent &)
    {
        endDrag();
    }

    void Fader::mouseDoubleClick(const juce::MouseEvent &)
    {
        if (isEnabled())
            resetToDefault();
    }

    bool Fader::keyPressed(const juce::KeyPress &key)
    {
        const float step = key.getModifiers().isShiftDown() ? 0.005f : 0.025f;
        if (key.isKeyCode(juce::KeyPress::rightKey) || key.isKeyCode(juce::KeyPress::upKey))
        {
            stepBy(step);
            return true;
        }
        if (key.isKeyCode(juce::KeyPress::leftKey) || key.isKeyCode(juce::KeyPress::downKey))
        {
            stepBy(-step);
            return true;
        }
        return false;
    }

    //==============================================================================
    // DragValue
    //==============================================================================
    DragValue::DragValue(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager,
                         std::function<juce::String(float)> formatter, juce::String unitText, bool isBoxed)
        : ParameterControl(parameter, undoManager), format(std::move(formatter)), unit(std::move(unitText)), boxed(isBoxed)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void DragValue::paint(juce::Graphics &g)
    {
        auto r = getLocalBounds().toFloat();
        if (boxed)
        {
            g.setColour(Colours::well);
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(isHovered() || isDragging() ? Colours::wellBorderHover : Colours::wellBorder);
            g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
            r = r.reduced(8.0f, 0.0f);
        }

        const auto valueFont = mono(fontSize);
        const auto text = format(value);
        const float textW = valueFont.getStringWidthFloat(text);

        // Value followed by its unit; unboxed values can hug either edge
        const auto unitFont = mono(boxed ? fontSize : fontSize - 2.0f);
        const float unitGap = boxed ? 6.0f : 4.0f;
        const float total = textW + (unit.isNotEmpty() ? unitGap + unitFont.getStringWidthFloat(unit) : 0.0f);
        const float x = (!boxed && alignRight) ? r.getRight() - total : r.getX();

        g.setFont(valueFont);
        g.setColour(Colours::textPrimary);
        g.drawText(text, juce::Rectangle<float>(x, r.getY(), textW + 1.0f, r.getHeight()), juce::Justification::centredLeft, false);

        if (unit.isNotEmpty())
        {
            g.setColour(Colours::textTertiary);
            g.setFont(unitFont);
            g.drawText(unit, juce::Rectangle<float>(x + textW + unitGap, r.getY(), r.getRight() - (x + textW + unitGap), r.getHeight()),
                       juce::Justification::centredLeft, false);
        }

        if (showFocusRing())
            drawFocusRing(g, getLocalBounds().toFloat(), 5.0f);
    }

    void DragValue::mouseDown(const juce::MouseEvent &e)
    {
        lastDragY = e.position.y;
        dragProportion = getProportion();
        beginDrag();
    }

    void DragValue::mouseDrag(const juce::MouseEvent &e)
    {
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        dragProportion = juce::jlimit(0.0f, 1.0f, dragProportion + (lastDragY - e.position.y) / 240.0f * fine);
        lastDragY = e.position.y;
        dragToProportion(dragProportion);
    }

    void DragValue::mouseUp(const juce::MouseEvent &)
    {
        endDrag();
    }

    void DragValue::mouseDoubleClick(const juce::MouseEvent &)
    {
        resetToDefault();
    }

    void DragValue::mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &wheel)
    {
        if (wheel.deltaY != 0.0f)
            stepBy(wheel.deltaY > 0.0f ? 0.01f : -0.01f);
    }

    //==============================================================================
    // IconButton
    //==============================================================================
    IconButton::IconButton(const juce::String &name, Icon i, float size)
        : juce::Button(name), icon(i)
    {
        setSize(juce::roundToInt(size), juce::roundToInt(size));
        setTitle(name);
    }

    void IconButton::paintButton(juce::Graphics &g, bool highlighted, bool down)
    {
        auto r = getLocalBounds().toFloat();
        const bool enabled = isEnabled();

        if (enabled && (highlighted || down))
        {
            g.setColour(down ? Colours::tile : Colours::hover);
            g.fillRoundedRectangle(r, 6.0f);
            g.setColour(Colours::wellBorder);
            g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
        }

        const auto colour = enabled && highlighted ? Colours::textPrimary : Colours::textSecondary;
        const float iconSize = r.getWidth() < 26.0f ? 14.0f : 16.0f;
        drawIcon(g, icon, r.withSizeKeepingCentre(iconSize, iconSize), colour.withAlpha(enabled ? 1.0f : 0.35f),
                 highlighted ? Colours::hover : Colours::headerBottom);

        if (hasKeyboardFocus(false))
            drawFocusRing(g, r, 6.0f);
    }

    //==============================================================================
    // Chip
    //==============================================================================
    Chip::Chip(const juce::String &text, Variant v)
        : juce::Button(text), label(text), variant(v)
    {
        setTitle(text);
    }

    void Chip::setSuffix(const juce::String &s)
    {
        if (suffix != s)
        {
            suffix = s;
            repaint();
        }
    }

    float Chip::getPreferredWidth() const
    {
        float w = 20.0f + sans(11.0f, Weight::semibold, 0.06f).getStringWidthFloat(label);
        if (showPip)
            w += 6.0f + 7.0f;
        if (glyph.isNotEmpty())
            w += 14.0f + 7.0f;
        if (powerIcon)
            w += 14.0f + 7.0f;
        if (suffix.isNotEmpty())
            w += 4.0f + mono(11.0f, Weight::medium).getStringWidthFloat(suffix);
        return std::ceil(w);
    }

    void Chip::paintButton(juce::Graphics &g, bool highlighted, bool down)
    {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();

        juce::Colour fill = Colours::tile, border = Colours::wellBorder, ink = Colours::textSecondary;
        if (on)
        {
            switch (variant)
            {
            case Variant::teal:
                ink = Colours::tealSoft;
                border = Colours::tealBorder;
                fill = juce::Colour(0xff132424);
                break;
            case Variant::solo:
                ink = juce::Colour(0xffcfe0ff);
                border = juce::Colour(0xff3c5f99);
                fill = juce::Colour(0xff18243a);
                break;
            case Variant::amber:
                ink = Colours::amberLight;
                border = Colours::amberBorder;
                fill = juce::Colour(0xff2e2417);
                break;
            case Variant::amberStrong:
                ink = Colours::amberLight;
                border = Colours::amberBorder;
                fill = Colours::amberFill;
                break;
            }
        }
        else if (highlighted || down)
        {
            border = Colours::wellBorderHover;
            ink = Colours::textPrimary;
        }

        g.setColour(fill);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(border);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

        const auto labelFont = sans(11.0f, Weight::semibold, 0.06f);
        const auto suffixFont = mono(11.0f, Weight::medium);
        float x = 10.0f;
        if (centred)
            x = 0.5f * (r.getWidth() - (getPreferredWidth() - 20.0f));

        const float cy = r.getCentreY();
        if (showPip)
        {
            drawPip(g, {x + 3.0f, cy}, ink);
            x += 13.0f;
        }
        if (glyph.isNotEmpty())
        {
            if (glyph == juce::String::charToString((juce::juce_wchar)0x394))
            {
                drawDelta(g, {x + 1.0f, cy - 4.5f, 9.0f, 9.0f}, ink);
            }
            else
            {
                g.setColour(ink);
                g.setFont(mono(11.0f, Weight::semibold));
                g.drawText(glyph, juce::Rectangle<float>(x, 0.0f, 14.0f, r.getHeight()), juce::Justification::centredLeft, false);
            }
            x += 21.0f;
        }
        if (powerIcon)
        {
            drawIcon(g, Icon::power, {x, cy - 7.0f, 14.0f, 14.0f}, ink);
            x += 21.0f;
        }

        g.setColour(ink);
        g.setFont(labelFont);
        const float labelW = labelFont.getStringWidthFloat(label);
        g.drawText(label, juce::Rectangle<float>(x, 0.0f, labelW + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        x += labelW + 4.0f;

        if (suffix.isNotEmpty())
        {
            g.setFont(suffixFont);
            g.drawText(suffix, juce::Rectangle<float>(x, 0.0f, r.getRight() - x, r.getHeight()), juce::Justification::centredLeft, false);
        }

        if (hasKeyboardFocus(false))
            drawFocusRing(g, r, 6.0f);
    }

    //==============================================================================
    // BypassButton
    //==============================================================================
    BypassButton::BypassButton() : juce::Button("Bypass")
    {
        setClickingTogglesState(true);
        setTitle("Main bypass");
    }

    float BypassButton::getPreferredWidth() const
    {
        return std::ceil(9.0f + 15.0f + 7.0f + sans(11.0f, Weight::bold, 0.1f).getStringWidthFloat("BYPASSED") + 12.0f);
    }

    void BypassButton::paintButton(juce::Graphics &g, bool highlighted, bool)
    {
        auto r = getLocalBounds().toFloat();
        const bool bypassed = getToggleState();

        const juce::Colour fill = bypassed ? (highlighted ? juce::Colour(0xff45341f) : Colours::amberFill)
                                           : (highlighted ? juce::Colour(0xff163332) : Colours::tealFill);
        const juce::Colour border = bypassed ? Colours::amberBorder : Colours::tealBorder;
        const juce::Colour ink = bypassed ? Colours::amberLight : Colours::tealText;

        g.setColour(fill);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(border);
        g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);

        drawIcon(g, Icon::power, {9.0f, r.getCentreY() - 7.5f, 15.0f, 15.0f}, ink);
        g.setColour(ink);
        g.setFont(sans(11.0f, Weight::bold, 0.1f));
        g.drawText(bypassed ? "BYPASSED" : "ACTIVE", r.withTrimmedLeft(31.0f), juce::Justification::centredLeft, false);

        if (hasKeyboardFocus(false))
            drawFocusRing(g, r, 7.0f);
    }

    //==============================================================================
    // ToggleSwitch
    //==============================================================================
    ToggleSwitch::ToggleSwitch(const juce::String &name) : juce::Button(name)
    {
        setClickingTogglesState(true);
        setTitle(name);
        setSize(30, 16);
    }

    void ToggleSwitch::paintButton(juce::Graphics &g, bool, bool)
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();

        g.setColour(on ? juce::Colour(0xff1c4744) : Colours::tile);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(on ? juce::Colour(0xff2d8f87) : Colours::wellBorderHover);
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);

        g.setColour(on ? Colours::tealSoft : Colours::textSecondary);
        g.fillEllipse(on ? 17.0f : 3.0f, 3.0f, 10.0f, 10.0f);

        if (hasKeyboardFocus(false))
            drawFocusRing(g, r, 8.0f);
    }

    //==============================================================================
    // TabButton
    //==============================================================================
    TabButton::TabButton(const juce::String &label, bool led) : juce::Button(label), withLed(led)
    {
        setRadioGroupId(0x7ab);
        setClickingTogglesState(true);
    }

    void TabButton::setLedOn(bool on)
    {
        if (ledOn != on)
        {
            ledOn = on;
            repaint();
        }
    }

    void TabButton::paintButton(juce::Graphics &g, bool highlighted, bool)
    {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();
        float x = 12.0f;

        if (withLed)
        {
            drawLed(g, {x + 3.5f, r.getCentreY()}, ledOn ? LedState::on : LedState::off, 7.0f, false);
            x += 14.0f;
        }

        g.setColour(on || highlighted ? Colours::textPrimary : Colours::textSecondary);
        g.setFont(sans(10.5f, Weight::semibold, 0.1f));
        g.drawText(getButtonText(), r.withTrimmedLeft(x), juce::Justification::centredLeft, false);

        if (on)
        {
            g.setColour(Colours::teal);
            g.fillRect(r.removeFromBottom(2.0f));
        }
    }

    //==============================================================================
    // LegendButton
    //==============================================================================
    LegendButton::LegendButton(const juce::String &label, juce::Colour c) : juce::Button(label), swatch(c)
    {
        setClickingTogglesState(true);
        setToggleState(true, juce::dontSendNotification);
    }

    float LegendButton::getPreferredWidth() const
    {
        return std::ceil(8.0f + 14.0f + 6.0f + sans(11.0f).getStringWidthFloat(getButtonText()));
    }

    void LegendButton::paintButton(juce::Graphics &g, bool highlighted, bool)
    {
        const auto r = getLocalBounds().toFloat();
        const float alpha = getToggleState() ? 1.0f : 0.45f;

        g.setColour(swatch.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(4.0f, r.getCentreY() - 1.5f, 14.0f, 3.0f, 1.5f);
        g.setColour((highlighted ? Colours::textPrimary : Colours::textSecondary).withMultipliedAlpha(alpha));
        g.setFont(sans(11.0f));
        g.drawText(getButtonText(), r.withTrimmedLeft(24.0f), juce::Justification::centredLeft, false);
    }

    //==============================================================================
    // SegmentedControl
    //==============================================================================
    class SegmentedControl::Segment : public juce::Button
    {
    public:
        Segment(SegmentedControl &o, int i, const juce::String &label, juce::Colour p, Variant v, float pad)
            : juce::Button(label), owner(o), index(i), pip(p), variant(v), paddingX(pad)
        {
            setTitle(label);
            onClick = [this]
            { owner.setSelectedIndex(index, true); };
        }

        float getPreferredWidth() const
        {
            float w = paddingX * 2.0f + font().getStringWidthFloat(getButtonText());
            if (!pip.isTransparent())
                w += 12.0f;
            return std::ceil(w);
        }

        juce::Font font() const { return sans(owner.small ? 10.0f : 11.0f, Weight::semibold, 0.06f); }

        void paintButton(juce::Graphics &g, bool highlighted, bool) override
        {
            const auto r = getLocalBounds().toFloat();
            const bool on = getToggleState();
            juce::Colour fill = juce::Colours::transparentBlack, ink = Colours::textSecondary, ring = juce::Colours::transparentBlack;

            if (on)
            {
                switch (variant)
                {
                case Variant::removed:
                    fill = juce::Colour(0xff3a2e1f);
                    ink = Colours::amberLight;
                    ring = juce::Colour(0xff6b5233);
                    break;
                case Variant::original:
                    fill = juce::Colour(0xff22303f);
                    ink = Colours::textSoft;
                    ring = juce::Colour(0xff3a5170);
                    break;
                case Variant::processed:
                    fill = juce::Colour(0xff173533);
                    ink = Colours::tealSoft;
                    ring = Colours::tealBorder;
                    break;
                case Variant::plain:
                    fill = Colours::selected;
                    ink = Colours::textPrimary;
                    break;
                }
            }
            else if (highlighted && isEnabled())
            {
                fill = juce::Colour(0xff18212b);
                ink = Colours::textPrimary;
            }

            if (!fill.isTransparent())
            {
                if (variant == Variant::plain && on)
                {
                    g.setColour(juce::Colours::black.withAlpha(0.45f));
                    g.fillRoundedRectangle(r.translated(0.0f, 1.0f), 5.0f);
                }
                g.setColour(fill);
                g.fillRoundedRectangle(r, 5.0f);
                if (!ring.isTransparent())
                {
                    g.setColour(ring);
                    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
                }
                else
                {
                    g.setColour(juce::Colours::white.withAlpha(0.06f));
                    g.drawHorizontalLine(juce::roundToInt(r.getY() + 1.0f), r.getX() + 3.0f, r.getRight() - 3.0f);
                }
            }

            if (!isEnabled())
                ink = ink.withMultipliedAlpha(0.4f);

            float x = paddingX;
            const float contentW = getPreferredWidth() - paddingX * 2.0f;
            x = juce::jmax(paddingX, 0.5f * (r.getWidth() - contentW));
            if (!pip.isTransparent())
            {
                drawPip(g, {x + 3.0f, r.getCentreY()}, pip);
                x += 12.0f;
            }

            g.setColour(ink);
            g.setFont(font());
            g.drawText(getButtonText(), r.withTrimmedLeft(x), juce::Justification::centredLeft, false);

            if (hasKeyboardFocus(false))
                drawFocusRing(g, r, 5.0f);
        }

        SegmentedControl &owner;
        int index;
        juce::Colour pip;
        Variant variant;
        float paddingX;
    };

    SegmentedControl::SegmentedControl(bool isSmall) : small(isSmall) {}

    SegmentedControl::~SegmentedControl() = default;

    void SegmentedControl::addItem(const juce::String &label, const juce::String &tooltip, juce::Colour pip, Variant variant, float paddingX)
    {
        auto segment = std::make_unique<Segment>(*this, (int)segments.size(), label, pip, variant, small ? 9.0f : paddingX);
        segment->setTooltip(tooltip);
        addAndMakeVisible(*segment);
        segments.push_back(std::move(segment));
    }

    void SegmentedControl::setSelectedIndex(int index, bool notify)
    {
        if (notify && onUserPick)
            onUserPick(index);

        if (notify && attachment != nullptr && toValue)
        {
            attachment->setValueAsCompleteGesture(toValue(index));
            return;
        }

        selected = index;
        for (auto &s : segments)
            s->setToggleState(s->index == index, juce::dontSendNotification);

        if (notify && onChange)
            onChange(index);
    }

    void SegmentedControl::attachToParameter(juce::RangedAudioParameter &param, juce::UndoManager *undoManager,
                                             std::function<float(int)> indexToValue, std::function<int(float)> valueToIndex)
    {
        toValue = std::move(indexToValue);
        attachment = std::make_unique<juce::ParameterAttachment>(
            param, [this, valueToIndex](float v)
            {
                setSelectedIndex(valueToIndex(v), false);
                if (onChange)
                    onChange(selected); },
            undoManager);
        attachment->sendInitialUpdate();
    }

    float SegmentedControl::getPreferredWidth() const
    {
        float w = 6.0f; // padding 2 ×2 + border
        for (auto &s : segments)
            w += s->getPreferredWidth() + 2.0f;
        return w - 2.0f;
    }

    void SegmentedControl::setItemPadding(float paddingX)
    {
        for (auto &s : segments)
            s->paddingX = small ? juce::jmin(paddingX, 9.0f) : paddingX;
    }

    void SegmentedControl::setItemsEnabled(bool enabled)
    {
        for (auto &s : segments)
            s->setEnabled(enabled);
    }

    void SegmentedControl::paint(juce::Graphics &g)
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour(Colours::well);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(Colours::wellBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
    }

    void SegmentedControl::resized()
    {
        auto r = getLocalBounds().toFloat().reduced(3.0f);
        float total = 0.0f;
        for (auto &s : segments)
            total += s->getPreferredWidth();

        // Distribute any extra width evenly (segments can be stretched with flex: 1)
        const float extra = juce::jmax(0.0f, r.getWidth() - total - 2.0f * (float)(segments.size() - 1)) / (float)juce::jmax<size_t>(1, segments.size());
        float x = r.getX();
        for (auto &s : segments)
        {
            const float w = s->getPreferredWidth() + extra;
            s->setBounds(juce::Rectangle<float>(x, r.getY(), w, r.getHeight()).toNearestInt());
            x += w + 2.0f;
        }
    }

    //==============================================================================
    // PresetDropdown
    //==============================================================================
    PresetDropdown::PresetDropdown(bool isCompact) : juce::Button("Presets"), compact(isCompact)
    {
        setTitle("Preset");
    }

    void PresetDropdown::setPreset(const juce::String &newKind, const juce::String &newName, bool isModified)
    {
        if (kind != newKind || name != newName || modified != isModified)
        {
            kind = newKind;
            name = newName;
            modified = isModified;
            setDescription(name);
            repaint();
        }
    }

    void PresetDropdown::setOpen(bool isOpen)
    {
        if (open != isOpen)
        {
            open = isOpen;
            repaint();
        }
    }

    float PresetDropdown::getPreferredWidth() const
    {
        if (compact)
            return 220.0f;

        const float content = 6.0f + tagWidth(kind) + 8.0f + sans(12.5f, Weight::medium).getStringWidthFloat(name) + 8.0f +
                              (modified ? 11.0f + sans(10.0f, Weight::semibold, 0.06f).getStringWidthFloat("MODIFIED") + 8.0f : 0.0f) +
                              14.0f + 8.0f;
        return juce::jlimit(288.0f, 380.0f, std::ceil(content));
    }

    void PresetDropdown::paintButton(juce::Graphics &g, bool highlighted, bool)
    {
        auto r = getLocalBounds().toFloat();

        if (open)
        {
            g.setColour(Colours::teal.withAlpha(0.12f));
            g.fillRoundedRectangle(r.expanded(3.0f), 10.0f);
        }

        g.setColour(highlighted && !open ? juce::Colour(0xff111820) : Colours::well);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(open ? Colours::teal : (highlighted ? Colours::wellBorderHover : Colours::wellBorder));
        g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);

        auto content = r.withTrimmedLeft(compact ? 10.0f : 6.0f).withTrimmedRight(8.0f);

        // Chevron
        drawIcon(g, Icon::chevronDown, content.removeFromRight(14.0f).withSizeKeepingCentre(14.0f, 14.0f), Colours::textSecondary);
        content.removeFromRight(8.0f);

        // MODIFIED / MOD marker
        if (modified)
        {
            const auto font = compact ? sans(9.5f, Weight::bold, 0.06f) : sans(10.0f, Weight::semibold, 0.06f);
            const juce::String marker = compact ? "MOD" : "MODIFIED";
            const float w = font.getStringWidthFloat(marker) + 1.0f;
            auto area = content.removeFromRight(w + (compact ? 10.0f : 11.0f));
            drawPip(g, {area.getX() + 3.0f, area.getCentreY()}, Colours::amber, 6.0f);
            g.setColour(Colours::amber);
            g.setFont(font);
            g.drawText(marker, area.withTrimmedLeft(compact ? 10.0f : 11.0f), juce::Justification::centredLeft, false);
            content.removeFromRight(8.0f);
        }

        if (!compact)
        {
            const float tw = tagWidth(kind);
            drawTag(g, kind, content.removeFromLeft(tw).withSizeKeepingCentre(tw, 15.0f), TagStyle::plain);
            content.removeFromLeft(8.0f);
        }

        g.setColour(Colours::textPrimary);
        g.setFont(sans(compact ? 12.0f : 12.5f, Weight::medium));
        g.drawFittedText(name, content.toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);

        if (hasKeyboardFocus(false))
            drawFocusRing(g, r, 7.0f);
    }
}
