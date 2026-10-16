#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace vfp::ui;

//==============================================================================
VoiceFrameLookAndFeel::VoiceFrameLookAndFeel()
{
    setColour(juce::Label::textColourId, Colours::textPrimary);
    setColour(juce::PopupMenu::backgroundColourId, Colours::popup);
    setColour(juce::PopupMenu::textColourId, Colours::textBody);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Colours::selected);
    setColour(juce::PopupMenu::highlightedTextColourId, Colours::textPrimary);
    setColour(juce::ScrollBar::thumbColourId, Colours::wellBorderHover);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff232f3c));
    setColour(juce::TooltipWindow::textColourId, Colours::textBody);
    setColour(juce::TooltipWindow::outlineColourId, Colours::popupBorder);
}

juce::Typeface::Ptr VoiceFrameLookAndFeel::getTypefaceForFont(const juce::Font &font)
{
    if (FontSet::instance != nullptr && font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return FontSet::instance->sans[font.isBold() ? 2 : 0];

    return LookAndFeel_V4::getTypefaceForFont(font);
}

juce::TextLayout VoiceFrameLookAndFeel::layoutTooltip(const juce::String &text)
{
    // "Title\nBody": the first line is set as a bold heading, as in the design's tooltips
    const auto title = text.upToFirstOccurrenceOf("\n", false, false);
    const auto body = text.fromFirstOccurrenceOf("\n", false, false);

    juce::AttributedString s;
    s.append(title, sans(11.0f, Weight::semibold), Colours::textPrimary);
    if (body.isNotEmpty())
    {
        s.append("\n", sans(4.0f), Colours::textBody);
        s.append(body, sans(11.0f), Colours::textBody);
    }
    s.setLineSpacing(2.0f);

    const float maxWidth = body.isEmpty() ? 400.0f : 230.0f;
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(s, maxWidth);
    return layout;
}

juce::Rectangle<int> VoiceFrameLookAndFeel::getTooltipBounds(const juce::String &tipText, juce::Point<int> screenPos,
                                                             juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip(tipText);
    const int w = juce::roundToInt(layout.getWidth()) + 22;
    const int h = juce::roundToInt(layout.getHeight()) + 18;

    auto bounds = juce::Rectangle<int>(w, h).withCentre({screenPos.x, screenPos.y + 22 + h / 2});
    if (bounds.getBottom() > parentArea.getBottom())
        bounds.setY(screenPos.y - 14 - h);
    return bounds.constrainedWithin(parentArea);
}

void VoiceFrameLookAndFeel::drawTooltip(juce::Graphics &g, const juce::String &text, int width, int height)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height);
    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);

    layoutTooltip(text).draw(g, r.reduced(10.0f, 8.0f).translated(0.0f, 0.5f));
}

void VoiceFrameLookAndFeel::drawPopupMenuBackground(juce::Graphics &g, int width, int height)
{
    g.fillAll(Colours::popup);
    g.setColour(Colours::popupBorder);
    g.drawRect(0, 0, width, height, 1);
}

void VoiceFrameLookAndFeel::drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                                              bool isSeparator, bool isActive, bool isHighlighted,
                                              bool isTicked, bool hasSubMenu, const juce::String &text,
                                              const juce::String & /*shortcutKeyText*/,
                                              const juce::Drawable * /*icon*/, const juce::Colour * /*textColour*/)
{
    if (isSeparator)
    {
        g.setColour(Colours::wellBorder);
        g.fillRect(area.getX() + 8, area.getCentreY(), area.getWidth() - 16, 1);
        return;
    }

    const auto r = area.toFloat().reduced(4.0f, 1.0f);
    if (isHighlighted && isActive)
    {
        g.setColour(Colours::selected);
        g.fillRoundedRectangle(r, 5.0f);
    }

    if (isTicked)
        drawIcon(g, Icon::check, {r.getX() + 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f}, Colours::teal);

    g.setColour(!isActive ? Colours::textHint : (isHighlighted ? Colours::textPrimary : Colours::textBody));
    g.setFont(getPopupMenuFont());
    g.drawText(text, r.withTrimmedLeft(26.0f).withTrimmedRight(18.0f), juce::Justification::centredLeft, true);

    if (hasSubMenu)
        drawIcon(g, Icon::chevronRight, {r.getRight() - 16.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f}, Colours::textSecondary);
}

void VoiceFrameLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics &g, const juce::Rectangle<int> &area, const juce::String &sectionName)
{
    vfp::ui::drawLabel(g, sectionName, area.toFloat().withTrimmedLeft(14.0f).withTrimmedTop(4.0f), Colours::textSecondary, 9.5f);
}

juce::Font VoiceFrameLookAndFeel::getPopupMenuFont()
{
    return sans(12.0f);
}

//==============================================================================
void VoiceFrameProEditor::Content::paint(juce::Graphics &g)
{
    // radial-gradient(120% 80% at 50% 0%, #121A23 0%, #0D1218 60%)
    const auto r = getLocalBounds().toFloat();
    g.fillAll(Colours::background);
    g.setGradientFill(juce::ColourGradient(Colours::backgroundTop, r.getCentreX(), 0.0f,
                                           Colours::background, r.getCentreX(), r.getHeight() * 0.8f * 0.6f, true));
    g.fillRect(r);
}

//==============================================================================
VoiceFrameProEditor::VoiceFrameProEditor(VoiceFrameProProcessor &p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      model(p),
      context{p, p.getAPVTS(), p.getUndoManager(), model, {}, {}, {}, {}, {}}
{
    setLookAndFeel(&customLookAndFeel);

    context.togglePresetMenu = [this]
    { togglePresetMenu(); };
    context.savePresetAs = [this]
    { showSaveDialog(); };
    context.showSettings = [this](juce::Component &target)
    { showSettings(target); };
    context.showHelp = [this]
    { showHelp(); };
    context.setMode = [this](ViewMode m)
    {
        // Deferred: the request usually comes from a component that rebuild() destroys
        juce::Component::SafePointer<VoiceFrameProEditor> safe(this);
        juce::MessageManager::callAsync([safe, m]
                                        {
                                            if (safe != nullptr)
                                                safe->setMode(m); });
    };

    // The start-up preset load must not appear as an undoable step
    if (audioProcessor.needsInitialUndoReset)
    {
        audioProcessor.needsInitialUndoReset = false;
        audioProcessor.getUndoManager().clearUndoHistory();
        juce::Component::SafePointer<VoiceFrameProEditor> safe(this);
        juce::Timer::callAfterDelay(400, [safe]
                                    {
                                        if (safe != nullptr)
                                            safe->audioProcessor.getUndoManager().clearUndoHistory(); });
    }

    auto &ui = audioProcessor.getUIState();
    mode = static_cast<ViewMode>(juce::jlimit(0, 2, (int)ui.getProperty("mode", 0)));
    scale = juce::jlimit(0.75f, 1.5f, (float)ui.getProperty("scale", 1.0f));

    addAndMakeVisible(content);
    rebuild();

    // 30 FPS UI metering update
    startTimerHz(30);
}

VoiceFrameProEditor::~VoiceFrameProEditor()
{
    stopTimer();
    dialog.reset();
    presetMenu.reset();
    setLookAndFeel(nullptr);
}

juce::Rectangle<int> VoiceFrameProEditor::getBaseBounds() const
{
    return mode == ViewMode::compact ? juce::Rectangle<int>(860, 540) : juce::Rectangle<int>(1120, 680);
}

void VoiceFrameProEditor::setMode(ViewMode newMode)
{
    if (newMode == mode)
        return;

    mode = newMode;
    audioProcessor.getUIState().setProperty("mode", (int)mode, nullptr);
    rebuild();
}

void VoiceFrameProEditor::rebuild()
{
    closePresetMenu();
    closeDialog();

    status.reset();
    footer.reset();
    body.reset();
    header.reset();

    const bool compact = mode == ViewMode::compact;
    const auto base = getBaseBounds();
    content.setBounds(base);

    header = std::make_unique<HeaderBar>(context, compact);
    body = createBody(context, mode);
    footer = std::make_unique<FooterBar>(context, mode);

    const int headerH = compact ? 42 : 48;
    const int footerH = compact ? 48 : 60;
    const int statusH = compact ? 0 : 22;

    header->setBounds(0, 0, base.getWidth(), headerH);
    footer->setBounds(0, base.getHeight() - statusH - footerH, base.getWidth(), footerH);
    body->setBounds(0, headerH, base.getWidth(), base.getHeight() - headerH - footerH - statusH);

    content.addAndMakeVisible(*body);
    content.addAndMakeVisible(*footer);
    content.addAndMakeVisible(*header); // header last: its popovers sit above the body

    if (!compact)
    {
        status = std::make_unique<StatusBar>(context);
        status->setBounds(0, base.getHeight() - statusH, base.getWidth(), statusH);
        content.addAndMakeVisible(*status);
    }

    applyScale(scale);
    timerCallback();
}

void VoiceFrameProEditor::applyScale(float newScale)
{
    scale = newScale;
    audioProcessor.getUIState().setProperty("scale", scale, nullptr);
    content.setTransform(juce::AffineTransform::scale(scale));

    const auto base = getBaseBounds();
    setSize(juce::roundToInt((float)base.getWidth() * scale), juce::roundToInt((float)base.getHeight() * scale));
}

//==============================================================================
void VoiceFrameProEditor::timerCallback()
{
    model.update(juce::Time::getMillisecondCounterHiRes() * 0.001);

    if (header != nullptr)
        header->refresh();
    if (body != nullptr)
        body->refresh();
    if (footer != nullptr)
        footer->refresh();
    if (status != nullptr)
        status->refresh();
}

//==============================================================================
void VoiceFrameProEditor::togglePresetMenu()
{
    if (presetMenu != nullptr)
    {
        closePresetMenu();
        return;
    }

    const bool compact = mode == ViewMode::compact;
    dismisser = std::make_unique<Dismisser>([this]
                                            { closePresetMenu(); });
    dismisser->setBounds(content.getLocalBounds());
    content.addAndMakeVisible(*dismisser);

    presetMenu = std::make_unique<PresetMenu>(context, compact, [this]
                                              {
                                                  juce::Component::SafePointer<VoiceFrameProEditor> safe(this);
                                                  juce::MessageManager::callAsync([safe]
                                                                                  {
                                                                                      if (safe != nullptr)
                                                                                          safe->closePresetMenu(); }); });

    // 38 px below the selector's top edge, plus the popover's 16 px shadow margin
    const auto anchor = header->getPresetAnchor();
    const int width = (compact ? 260 : 320) + 32;
    const int top = anchor.getY() + (compact ? 34 : 38) - 16;
    const int height = juce::jmin(presetMenu->getPreferredHeight(), content.getHeight() - top - 8 + 16);
    presetMenu->setBounds(anchor.getX() - 16, top, width, height);
    content.addAndMakeVisible(*presetMenu);
    header->setPresetMenuOpen(true);
}

void VoiceFrameProEditor::closePresetMenu()
{
    presetMenu.reset();
    dismisser.reset();
    if (header != nullptr)
        header->setPresetMenuOpen(false);
}

void VoiceFrameProEditor::closeDialog()
{
    dialog.reset();
}

void VoiceFrameProEditor::showSaveDialog()
{
    closePresetMenu();
    juce::Component::SafePointer<VoiceFrameProEditor> safe(this);

    dialog = std::make_unique<Dialog>(
        utf8("Save preset as\xe2\x80\xa6"),
        "Stores the current parameter values as a User preset. Monitoring settings and bypass are not stored.",
        true, "Save",
        [safe](const juce::String &name)
        {
            if (safe == nullptr || name.trim().isEmpty())
                return;
            safe->audioProcessor.getPresetManager().saveUserPreset(name);
            juce::MessageManager::callAsync([safe]
                                            { if (safe != nullptr) safe->closeDialog(); });
        },
        [safe]
        {
            juce::MessageManager::callAsync([safe]
                                            { if (safe != nullptr) safe->closeDialog(); });
        });

    dialog->setBounds(content.getLocalBounds());
    content.addAndMakeVisible(*dialog);
}

void VoiceFrameProEditor::showHelp()
{
    closePresetMenu();
    juce::Component::SafePointer<VoiceFrameProEditor> safe(this);
    auto dismiss = [safe]
    {
        juce::MessageManager::callAsync([safe]
                                        { if (safe != nullptr) safe->closeDialog(); });
    };

    const juce::String text = utf8(
        "Noise Reduction sets how strongly the RNNoise result replaces the original signal "
        "(0% = original, 100% = full processing).\n\n"
        "Monitor \xe2\x80\xba Removed plays input minus output. If you hear words there, lower the amount.\n\n"
        "Knobs: drag vertically \xc2\xb7 Shift = fine \xc2\xb7 double-click = default \xc2\xb7 mouse wheel = small steps.\n\n"
        "Advanced splits the blend into six crossover bands and adds a voice gate driven by RNNoise\xe2\x80\x99s voice-activity estimate.\n\n"
        "Hover any label for an explanation.");

    dialog = std::make_unique<Dialog>("Help", text, false, "Close", [dismiss](const juce::String &)
                                      { dismiss(); },
                                      dismiss);
    dialog->setBounds(content.getLocalBounds());
    content.addAndMakeVisible(*dialog);
}

void VoiceFrameProEditor::showSettings(juce::Component &target)
{
    closePresetMenu();

    juce::PopupMenu sizes;
    for (int pct : {75, 100, 125, 150})
        sizes.addItem(100 + pct, juce::String(pct) + "%", true, juce::roundToInt(scale * 100.0f) == pct);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&customLookAndFeel);
    menu.addSubMenu("Interface size", sizes);
    menu.addItem(2, "Compact view", true, mode == ViewMode::compact);
    menu.addItem(3, "Reset clip indicators");
    menu.addItem(4, "Show user presets folder");
    menu.addSeparator();
    menu.addSectionHeader("About");
    menu.addItem(10, "VoiceFrame Pro " + juce::String(JucePlugin_VersionString), false);
    menu.addItem(11, "Engine: RNNoise 0.1.1 (Xiph.Org, BSD-3-Clause)", false);
    menu.addItem(12, "Type: Geist & Geist Mono (SIL OFL 1.1)", false);

    juce::Component::SafePointer<VoiceFrameProEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target), [safe](int result)
                       {
                           if (safe == nullptr || result == 0)
                               return;

                           if (result > 100)
                               safe->applyScale((float)(result - 100) / 100.0f);
                           else if (result == 2)
                               safe->setMode(safe->mode == ViewMode::compact ? ViewMode::simple : ViewMode::compact);
                           else if (result == 3)
                               safe->audioProcessor.resetClipIndicators();
                           else if (result == 4)
                           {
                               auto dir = vfp::PresetManager::getUserPresetDirectory();
                               dir.createDirectory();
                               dir.startAsProcess();
                           } });
}

//==============================================================================
void VoiceFrameProEditor::paint(juce::Graphics &g)
{
    g.fillAll(Colours::background);
}

void VoiceFrameProEditor::resized()
{
}
