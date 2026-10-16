#pragma once

#include "Displays.h"

namespace vfp::ui
{
    //==============================================================================
    enum class ViewMode
    {
        simple = 0,
        advanced,
        compact
    };

    /** Everything the views need from the editor. */
    struct EditorContext
    {
        VoiceFrameProProcessor &processor;
        juce::AudioProcessorValueTreeState &apvts;
        juce::UndoManager &undo;
        EditorModel &model;

        std::function<void()> togglePresetMenu;
        std::function<void()> savePresetAs;
        std::function<void(juce::Component &)> showSettings;
        std::function<void()> showHelp;
        std::function<void(ViewMode)> setMode;

        juce::RangedAudioParameter &param(const juce::String &id) const { return *apvts.getParameter(id); }
        float value(const juce::String &id) const { return apvts.getRawParameterValue(id)->load(); }
        void setParam(const juce::String &id, float denormalised) const;
        int numChannels() const { return processor.getNumActiveChannels(); }
        bool linked() const { return value("stereoLink") > 0.5f; }
        juce::String channelText() const;
    };

    /** A component refreshed from the editor's 30 Hz timer. */
    class RefreshingComponent : public juce::Component
    {
    public:
        virtual void refresh() = 0;
    };

    //==============================================================================
    class HeaderBar : public RefreshingComponent
    {
    public:
        HeaderBar(EditorContext &ctx, bool compact);

        void refresh() override;
        void paint(juce::Graphics &) override;
        void resized() override;

        juce::Rectangle<int> getPresetAnchor() const { return dropdown.getBounds(); }
        void setPresetMenuOpen(bool open) { dropdown.setOpen(open); }

    private:
        EditorContext &ctx;
        bool compact;

        IconButton prevButton{"Previous preset", Icon::chevronLeft};
        PresetDropdown dropdown;
        IconButton nextButton{"Next preset", Icon::chevronRight};
        IconButton saveButton{"Save preset as", Icon::save};
        IconButton resetButton{"Reset to preset values", Icon::reset};
        SegmentedControl abControl;
        IconButton copyButton{"Copy current settings to other slot", Icon::copy};
        IconButton undoButton{"Undo", Icon::undo};
        IconButton redoButton{"Redo", Icon::redo};
        BypassButton bypassButton;
        IconButton settingsButton{"Settings and about", Icon::settings};
        IconButton helpButton{"Help", Icon::help};
        IconButton expandButton{"Expand to full size", Icon::expand};
        juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

        std::vector<float> separators;
    };

    //==============================================================================
    class FooterBar : public RefreshingComponent,
                      public juce::TooltipClient
    {
    public:
        FooterBar(EditorContext &ctx, ViewMode mode);

        juce::String getTooltip() override;
        void refresh() override;
        void paint(juce::Graphics &) override;
        void resized() override;

    private:
        EditorContext &ctx;
        ViewMode mode;

        std::unique_ptr<Knob> inputKnob, outputKnob;
        std::unique_ptr<DragValue> inputValue, outputValue;
        SegmentedControl monitor;
        Chip limiterChip{"LIMITER"};
        SegmentedControl modeControl;
        juce::AudioProcessorValueTreeState::ButtonAttachment limiterAttachment;

        juce::Rectangle<float> inputLabelArea, outputLabelArea, monitorLabelArea, channelBadge, compactText;
        juce::String laidOutChannelText;
        bool showChannelIcon = true;
        std::vector<float> separators;
    };

    //==============================================================================
    class StatusBar : public RefreshingComponent
    {
    public:
        explicit StatusBar(EditorContext &ctx);
        void refresh() override;
        void paint(juce::Graphics &) override;

    private:
        EditorContext &ctx;
        juce::StringArray items;
    };

    //==============================================================================
    /** Simple / Advanced / Compact body (everything between header and footer). */
    std::unique_ptr<RefreshingComponent> createBody(EditorContext &ctx, ViewMode mode);

    //==============================================================================
    /** Preset list popover ("Factory" / "User" sections, Save As / Reset). */
    class PresetMenu : public juce::Component
    {
    public:
        PresetMenu(EditorContext &ctx, bool compact, std::function<void()> onClose);
        ~PresetMenu() override;

        int getPreferredHeight() const;
        void paint(juce::Graphics &) override;
        void resized() override;

    private:
        class Row;
        EditorContext &ctx;
        bool compact;
        std::function<void()> close;
        juce::Viewport viewport;
        juce::Component list;
        std::vector<std::unique_ptr<Row>> rows;
        std::vector<std::pair<float, juce::String>> sectionLabels; // y in list, text
        std::vector<float> dividers;
        Chip saveAsChip{juce::String::fromUTF8("Save As\xe2\x80\xa6")};
        Chip resetChip{"Reset to Preset"};
        float listHeight = 0.0f;
    };

    //==============================================================================
    /** In-editor modal panel (save preset name prompt, help). */
    class Dialog : public juce::Component
    {
    public:
        Dialog(const juce::String &title, const juce::String &body, bool withTextField,
               const juce::String &confirmLabel, std::function<void(const juce::String &)> onConfirm,
               std::function<void()> onDismiss);

        void paint(juce::Graphics &) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent &) override;
        bool keyPressed(const juce::KeyPress &) override;
        void parentHierarchyChanged() override;

    private:
        juce::Rectangle<float> panelBounds() const;
        void confirm();

        juce::String title, body;
        std::unique_ptr<juce::TextEditor> field;
        Chip confirmChip, cancelChip{"Cancel"};
        std::function<void(const juce::String &)> onConfirm;
        std::function<void()> onDismiss;
    };
}
