#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace vfp::ui
{
    //==============================================================================
    /**
     * Base for custom controls bound to one host parameter. Uses juce::ParameterAttachment,
     * so every drag is a host gesture and an undoable transaction.
     */
    class ParameterControl : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        ParameterControl(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager);

        float getValue() const noexcept { return value; }
        float getProportion() const noexcept { return param.convertTo0to1(value); }
        juce::RangedAudioParameter &getParameter() noexcept { return param; }

        /** Called on the message thread whenever the parameter value changes. */
        std::function<void()> onValueChange;

        void mouseEnter(const juce::MouseEvent &) override;
        void mouseExit(const juce::MouseEvent &) override;
        void focusGained(FocusChangeType cause) override;
        void focusLost(FocusChangeType cause) override;

        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    protected:
        void beginDrag();
        void dragToProportion(float proportion);
        void endDrag();
        void setProportionAsGesture(float proportion);
        void stepBy(float proportionDelta);
        void resetToDefault();

        bool isDragging() const noexcept { return dragging; }
        bool isHovered() const noexcept { return hovered; }
        bool showFocusRing() const noexcept { return keyboardFocused && hasKeyboardFocus(false); }

        juce::RangedAudioParameter &param;
        float value = 0.0f;

    private:
        friend class ParameterValueInterface;
        juce::ParameterAttachment attachment;
        bool dragging = false, hovered = false, keyboardFocused = false;
    };

    //==============================================================================
    /** Rotary control in the design's several sizes. Drag vertically; Shift = fine; double-click = default. */
    class Knob : public ParameterControl
    {
    public:
        enum class Style
        {
            hero,        // 204 px main knob (Simple)
            heroCompact, // 176 px main knob (Compact)
            global,      // 128 px main knob (Advanced)
            mini,        // 36 px footer gain knob (bipolar, blue)
            gate,        // 34 px gate knob (teal)
            band         // 92 px selected-band amount knob
        };

        Knob(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager, Style style);

        void setArcColour(juce::Colour c);

        void paint(juce::Graphics &) override;
        void mouseDown(const juce::MouseEvent &) override;
        void mouseDrag(const juce::MouseEvent &) override;
        void mouseUp(const juce::MouseEvent &) override;
        void mouseDoubleClick(const juce::MouseEvent &) override;
        void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
        bool keyPressed(const juce::KeyPress &) override;

    private:
        void paintHero(juce::Graphics &, float size, bool compact);
        void paintGlobal(juce::Graphics &);
        void paintSmall(juce::Graphics &);

        Style style;
        juce::Colour arcColour;
        float lastDragY = 0.0f, dragProportion = 0.0f;
    };

    //==============================================================================
    /** Horizontal fader (".fd"); bipolar draws its fill from the centre. */
    class Fader : public ParameterControl
    {
    public:
        Fader(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager, bool bipolar);

        static constexpr float thumbOverhang = 7.0f;

        void paint(juce::Graphics &) override;
        void mouseDown(const juce::MouseEvent &) override;
        void mouseDrag(const juce::MouseEvent &) override;
        void mouseUp(const juce::MouseEvent &) override;
        void mouseDoubleClick(const juce::MouseEvent &) override;
        bool keyPressed(const juce::KeyPress &) override;

    private:
        float proportionAt(float x) const;
        bool bipolar;
    };

    //==============================================================================
    /** A value shown as text that can be dragged vertically (".fld" box or plain mono text). */
    class DragValue : public ParameterControl
    {
    public:
        DragValue(juce::RangedAudioParameter &parameter, juce::UndoManager *undoManager,
                  std::function<juce::String(float)> formatter, juce::String unit, bool boxed);

        void setFontSize(float px) { fontSize = px; }
        void setAlignRight(bool shouldAlignRight) { alignRight = shouldAlignRight; }

        void paint(juce::Graphics &) override;
        void mouseDown(const juce::MouseEvent &) override;
        void mouseDrag(const juce::MouseEvent &) override;
        void mouseUp(const juce::MouseEvent &) override;
        void mouseDoubleClick(const juce::MouseEvent &) override;
        void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;

    private:
        std::function<juce::String(float)> format;
        juce::String unit;
        bool boxed, alignRight = false;
        float fontSize = 11.0f;
        float lastDragY = 0.0f, dragProportion = 0.0f;
    };

    //==============================================================================
    /** 28 px square icon button (".ib"). */
    class IconButton : public juce::Button
    {
    public:
        IconButton(const juce::String &name, Icon icon, float size = 28.0f);
        void paintButton(juce::Graphics &, bool highlighted, bool down) override;

    private:
        Icon icon;
    };

    //==============================================================================
    /** Pill button (".chip"), optionally toggling, with pip / glyph / icon prefix and mono suffix. */
    class Chip : public juce::Button
    {
    public:
        enum class Variant
        {
            teal,
            solo,
            amber,
            amberStrong
        };

        Chip(const juce::String &label, Variant variant = Variant::teal);

        void setShowPip(bool shouldShow) { showPip = shouldShow; }
        void setGlyph(const juce::String &g) { glyph = g; }
        void setPowerIcon(bool shouldShow) { powerIcon = shouldShow; }
        void setSuffix(const juce::String &s);
        void setCentred(bool c) { centred = c; }
        float getPreferredWidth() const;

        void paintButton(juce::Graphics &, bool highlighted, bool down) override;

    private:
        juce::String label, glyph, suffix;
        Variant variant;
        bool showPip = false, powerIcon = false, centred = false;
    };

    //==============================================================================
    /** Main bypass button (".byp"): ACTIVE (teal) / BYPASSED (amber). Toggle state = bypassed. */
    class BypassButton : public juce::Button
    {
    public:
        BypassButton();
        float getPreferredWidth() const;
        void paintButton(juce::Graphics &, bool highlighted, bool down) override;
    };

    //==============================================================================
    /** Switch (".tog"). */
    class ToggleSwitch : public juce::Button
    {
    public:
        explicit ToggleSwitch(const juce::String &name);
        void paintButton(juce::Graphics &, bool highlighted, bool down) override;
    };

    //==============================================================================
    /** Tab (".tab"), optional leading LED. */
    class TabButton : public juce::Button
    {
    public:
        TabButton(const juce::String &label, bool withLed);
        void setLedOn(bool on);
        void paintButton(juce::Graphics &, bool highlighted, bool down) override;

    private:
        bool withLed, ledOn = false;
    };

    //==============================================================================
    /** Legend toggle in the spectrum header (".lg"). */
    class LegendButton : public juce::Button
    {
    public:
        LegendButton(const juce::String &label, juce::Colour swatch);
        float getPreferredWidth() const;
        void paintButton(juce::Graphics &, bool highlighted, bool down) override;

    private:
        juce::Colour swatch;
    };

    //==============================================================================
    /** Segmented control (".seg"); optional binding to a choice or bool parameter. */
    class SegmentedControl : public juce::Component
    {
    public:
        enum class Variant
        {
            plain,
            original,
            processed,
            removed
        };

        explicit SegmentedControl(bool small = false);
        ~SegmentedControl() override;

        void addItem(const juce::String &label, const juce::String &tooltip = {},
                     juce::Colour pip = juce::Colours::transparentBlack, Variant variant = Variant::plain,
                     float paddingX = 12.0f);

        void setSelectedIndex(int index, bool notify);
        int getSelectedIndex() const noexcept { return selected; }
        std::function<void(int)> onChange;   // any selection change (user or parameter)
        std::function<void(int)> onUserPick; // user clicks only

        /** Binds to a parameter. indexToValue / valueToIndex map between segment index and denormalised value. */
        void attachToParameter(juce::RangedAudioParameter &param, juce::UndoManager *undoManager,
                               std::function<float(int)> indexToValue, std::function<int(float)> valueToIndex);

        float getPreferredWidth() const;
        void setItemsEnabled(bool enabled);
        void setItemPadding(float paddingX);

        void paint(juce::Graphics &) override;
        void resized() override;

    private:
        class Segment;
        bool small;
        int selected = -1;
        std::vector<std::unique_ptr<Segment>> segments;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        std::function<float(int)> toValue;
    };

    //==============================================================================
    /** The preset selector (".dd"): kind tag, name, MODIFIED marker, chevron. */
    class PresetDropdown : public juce::Button
    {
    public:
        explicit PresetDropdown(bool compact);

        void setPreset(const juce::String &kind, const juce::String &name, bool modified);
        void setOpen(bool isOpen);
        float getPreferredWidth() const;

        void paintButton(juce::Graphics &, bool highlighted, bool down) override;

    private:
        bool compact, open = false, modified = false;
        juce::String kind, name;
    };
}
