#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "Views.h"

//==============================================================================
/**
 * LookAndFeel for VoiceFrame Pro: Geist type, two-line tooltips (title + body),
 * and dark popup menus matching the design.
 */
class VoiceFrameLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoiceFrameLookAndFeel();
    ~VoiceFrameLookAndFeel() override = default;

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &font) override;

    juce::Rectangle<int> getTooltipBounds(const juce::String &tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics &g, const juce::String &text, int width, int height) override;

    void drawPopupMenuBackground(juce::Graphics &g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String &text,
                           const juce::String &shortcutKeyText,
                           const juce::Drawable *icon, const juce::Colour *textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics &g, const juce::Rectangle<int> &area, const juce::String &sectionName) override;
    juce::Font getPopupMenuFont() override;

private:
    static juce::TextLayout layoutTooltip(const juce::String &text);
};

//==============================================================================
/**
 * VoiceFrame Pro AudioProcessorEditor (Simple 1120×680 · Advanced 1120×680 · Compact 860×540).
 */
class VoiceFrameProEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit VoiceFrameProEditor(VoiceFrameProProcessor &);
    ~VoiceFrameProEditor() override;

    //==============================================================================
    void paint(juce::Graphics &) override;
    void resized() override;

private:
    /** Unscaled design surface; scaled as a whole for the interface-size setting. */
    class Content : public juce::Component
    {
    public:
        void paint(juce::Graphics &) override;
    };

    /** Transparent layer behind a popover that closes it on outside clicks. */
    class Dismisser : public juce::Component
    {
    public:
        explicit Dismisser(std::function<void()> f) : onDismiss(std::move(f)) {}
        void mouseDown(const juce::MouseEvent &) override { onDismiss(); }

    private:
        std::function<void()> onDismiss;
    };

    void timerCallback() override;

    void setMode(vfp::ui::ViewMode newMode);
    void rebuild();
    void applyScale(float newScale);
    juce::Rectangle<int> getBaseBounds() const;

    void togglePresetMenu();
    void closePresetMenu();
    void showSaveDialog();
    void showHelp();
    void showSettings(juce::Component &target);
    void closeDialog();

    VoiceFrameProProcessor &audioProcessor;
    juce::SharedResourcePointer<vfp::ui::FontSet> fontSet;
    VoiceFrameLookAndFeel customLookAndFeel;
    vfp::ui::EditorModel model;
    vfp::ui::EditorContext context;

    Content content;
    std::unique_ptr<vfp::ui::HeaderBar> header;
    std::unique_ptr<vfp::ui::RefreshingComponent> body;
    std::unique_ptr<vfp::ui::FooterBar> footer;
    std::unique_ptr<vfp::ui::StatusBar> status;
    std::unique_ptr<Dismisser> dismisser;
    std::unique_ptr<vfp::ui::PresetMenu> presetMenu;
    std::unique_ptr<vfp::ui::Dialog> dialog;
    juce::TooltipWindow tooltipWindow{this, 450};

    vfp::ui::ViewMode mode = vfp::ui::ViewMode::simple;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceFrameProEditor)
};
