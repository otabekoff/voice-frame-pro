#include "Views.h"

namespace vfp::ui
{
    namespace
    {
        const juce::String middot = juce::String::fromUTF8("\xc2\xb7");

        juce::Rectangle<float> centredBox(float cx, float cy, float w, float h)
        {
            return juce::Rectangle<float>(w, h).withCentre({cx, cy});
        }

        /** Tooltip lookup for painted regions: the first rectangle containing the mouse wins. */
        struct TooltipRegions
        {
            std::vector<std::pair<juce::Rectangle<float>, juce::String>> regions;

            void clear() { regions.clear(); }
            void add(juce::Rectangle<float> r, const juce::String &text) { regions.push_back({r, text}); }

            juce::String find(const juce::Component &c) const
            {
                const auto pos = c.getMouseXYRelative().toFloat();
                for (const auto &[r, text] : regions)
                    if (r.contains(pos))
                        return text;
                return {};
            }
        };

        void drawSeparator(juce::Graphics &g, float x, float cy, float height)
        {
            g.setColour(Colours::wellBorder);
            g.fillRect(juce::Rectangle<float>(x, cy - height * 0.5f, 1.0f, height));
        }

        void drawLabelValue(juce::Graphics &g, juce::Rectangle<float> area, const juce::String &label, const juce::String &value,
                            bool alignRight)
        {
            const auto just = alignRight ? juce::Justification::centredRight : juce::Justification::centredLeft;
            drawLabel(g, label, area.withHeight(12.0f), Colours::textSecondary, 10.0f, 0.12f, just);

            const auto valueFont = mono(12.0f);
            const auto unitFont = mono(10.0f);
            const float vw = valueFont.getStringWidthFloat(value), uw = unitFont.getStringWidthFloat("dB");
            const float total = vw + 4.0f + uw;
            const float x = alignRight ? area.getRight() - total : area.getX();
            const float baseline = area.getY() + 15.0f + 11.5f;

            g.setColour(Colours::textPrimary);
            g.setFont(valueFont);
            g.drawSingleLineText(value, juce::roundToInt(x), juce::roundToInt(baseline));
            g.setColour(Colours::textTertiary);
            g.setFont(unitFont);
            g.drawSingleLineText("dB", juce::roundToInt(x + vw + 4.0f), juce::roundToInt(baseline));
        }
    }

    //==============================================================================
    // EditorContext
    //==============================================================================
    void EditorContext::setParam(const juce::String &id, float denormalised) const
    {
        if (auto *p = apvts.getParameter(id))
        {
            undo.beginNewTransaction();
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(denormalised));
            p->endChangeGesture();
        }
    }

    juce::String EditorContext::channelText() const
    {
        if (numChannels() < 2)
            return "MONO";
        return "STEREO " + middot + (linked() ? " LINKED" : " DUAL MONO");
    }

    //==============================================================================
    // HeaderBar
    //==============================================================================
    HeaderBar::HeaderBar(EditorContext &c, bool isCompact)
        : ctx(c), compact(isCompact), dropdown(isCompact),
          bypassAttachment(c.apvts, "bypass", bypassButton)
    {
        auto &presets = ctx.processor.getPresetManager();

        prevButton.onClick = [this, &presets]
        {
            ctx.undo.beginNewTransaction();
            presets.loadPrevious();
        };
        nextButton.onClick = [this, &presets]
        {
            ctx.undo.beginNewTransaction();
            presets.loadNext();
        };
        resetButton.onClick = [this, &presets]
        {
            ctx.undo.beginNewTransaction();
            presets.resetToPreset();
        };
        saveButton.onClick = [this]
        { ctx.savePresetAs(); };
        dropdown.onClick = [this]
        { ctx.togglePresetMenu(); };

        abControl.addItem("A", "Slot A", juce::Colours::transparentBlack, SegmentedControl::Variant::plain, 10.0f);
        abControl.addItem("B", "Slot B", juce::Colours::transparentBlack, SegmentedControl::Variant::plain, 10.0f);
        abControl.setTitle("A/B comparison");
        abControl.onUserPick = [this, &presets](int slot)
        {
            ctx.undo.beginNewTransaction();
            presets.selectSlot(slot);
        };

        copyButton.onClick = [&presets]
        { presets.copyActiveToOther(); };
        undoButton.onClick = [this]
        { ctx.undo.undo(); };
        redoButton.onClick = [this]
        { ctx.undo.redo(); };
        settingsButton.onClick = [this]
        { ctx.showSettings(settingsButton); };
        helpButton.onClick = [this]
        { ctx.showHelp(); };
        expandButton.onClick = [this]
        { ctx.setMode(ViewMode::simple); };

        prevButton.setTooltip("Previous preset");
        nextButton.setTooltip("Next preset");
        saveButton.setTooltip(utf8("Save preset as\xe2\x80\xa6\nStores current values as a User preset."));
        resetButton.setTooltip("Reset to preset\nDiscard changes since the preset was loaded.");
        undoButton.setTooltip("Undo");
        redoButton.setTooltip("Redo");
        bypassButton.setTooltip(utf8("Main bypass\nClick to toggle. Host-automatable as \xe2\x80\x9c" "Bypass\xe2\x80\x9d."));
        settingsButton.setTooltip("Settings & About\nScaling, meters, engine version.");
        helpButton.setTooltip("Help\nHover any label for an explanation.");
        expandButton.setTooltip("Expand to full size");

        for (juce::Component *comp : {(juce::Component *)&prevButton, (juce::Component *)&dropdown, (juce::Component *)&nextButton,
                                      (juce::Component *)&abControl, (juce::Component *)&bypassButton})
            addAndMakeVisible(comp);

        if (compact)
        {
            addAndMakeVisible(expandButton);
        }
        else
        {
            for (auto *b : {&saveButton, &resetButton, &copyButton, &undoButton, &redoButton, &settingsButton, &helpButton})
                addAndMakeVisible(b);
        }

        refresh();
    }

    void HeaderBar::refresh()
    {
        auto &presets = ctx.processor.getPresetManager();
        const auto &preset = presets.getCurrentPreset();
        const bool modified = presets.isModified();
        const auto oldWidth = dropdown.getPreferredWidth();

        dropdown.setPreset(preset.factory ? "Factory" : "User", preset.name, modified);
        resetButton.setEnabled(modified);
        undoButton.setEnabled(ctx.undo.canUndo());
        redoButton.setEnabled(ctx.undo.canRedo());

        const int slot = presets.getActiveSlot();
        if (abControl.getSelectedIndex() != slot)
            abControl.setSelectedIndex(slot, false);
        copyButton.setTooltip(utf8(slot == 0 ? "Copy A \xe2\x86\x92 B" : "Copy B \xe2\x86\x92 A") + "\nCopies the active slot into the other slot.");

        if (dropdown.getPreferredWidth() != oldWidth)
            resized();
    }

    void HeaderBar::paint(juce::Graphics &g)
    {
        const auto r = getLocalBounds().toFloat();
        fillVerticalGradient(g, r, Colours::headerTop, Colours::headerBottom);
        g.setColour(Colours::wellBorder);
        g.fillRect(r.withTop(r.getBottom() - 1.0f));

        const float cy = r.getCentreY();
        const float left = compact ? 12.0f : 16.0f;
        drawLogo(g, {left, cy - 13.0f, 26.0f, 26.0f});

        const float textX = left + 26.0f + 10.0f;
        const auto wordFont = sans(compact ? 12.0f : 13.0f, Weight::bold, 0.2f);
        const auto proFont = sans(compact ? 12.0f : 13.0f, Weight::bold, 0.14f);
        const float wordW = wordFont.getStringWidthFloat("VOICEFRAME");
        const float wordTop = compact ? cy - 7.0f : cy - 12.75f;

        g.setFont(wordFont);
        g.setColour(Colours::textPrimary);
        g.drawText("VOICEFRAME", juce::Rectangle<float>(textX, wordTop, wordW + 4.0f, 14.0f), juce::Justification::centredLeft, false);
        g.setFont(proFont);
        g.setColour(Colours::teal);
        g.drawText("PRO", juce::Rectangle<float>(textX + wordW + (compact ? 5.0f : 6.0f), wordTop, 40.0f, 14.0f), juce::Justification::centredLeft, false);

        if (!compact)
        {
            g.setFont(sans(10.5f, Weight::regular, 0.02f));
            g.setColour(Colours::textSecondary);
            g.drawText("Real-Time Voice Denoiser", juce::Rectangle<float>(textX, cy + 2.0f, 200.0f, 12.0f), juce::Justification::centredLeft, false);
        }

        for (float x : separators)
            drawSeparator(g, x, cy, 20.0f);
    }

    void HeaderBar::resized()
    {
        separators.clear();
        const int h = getHeight();
        const float cy = (float)h * 0.5f;
        const float gap = compact ? 10.0f : 6.0f;
        float x = (float)getWidth() - (compact ? 10.0f : 12.0f);

        auto placeRight = [&](juce::Component &c, float w, float ch)
        {
            x -= w;
            c.setBounds(juce::Rectangle<float>(x, cy - ch * 0.5f, w, ch).toNearestInt());
            x -= gap;
        };
        auto separator = [&]
        {
            x -= 4.0f;
            separators.push_back(x - 1.0f);
            x -= 5.0f + gap;
        };

        if (compact)
        {
            placeRight(expandButton, 28.0f, 28.0f);
            placeRight(bypassButton, bypassButton.getPreferredWidth(), 30.0f);
            placeRight(abControl, abControl.getPreferredWidth(), 32.0f);
        }
        else
        {
            placeRight(helpButton, 28.0f, 28.0f);
            placeRight(settingsButton, 28.0f, 28.0f);
            placeRight(bypassButton, bypassButton.getPreferredWidth(), 30.0f);
            separator();
            placeRight(redoButton, 28.0f, 28.0f);
            placeRight(undoButton, 28.0f, 28.0f);
            separator();
            placeRight(copyButton, 28.0f, 28.0f);
            placeRight(abControl, abControl.getPreferredWidth(), 32.0f);
        }

        const float rightGroupLeft = x + gap;
        const float wordmark = sans(compact ? 12.0f : 13.0f, Weight::bold, 0.2f).getStringWidthFloat("VOICEFRAME PRO");
        const float regionLeft = compact ? 12.0f + 36.0f + wordmark + 10.0f : 16.0f + 236.0f + 14.0f;
        const float regionRight = rightGroupLeft - (compact ? 10.0f : 14.0f);

        const float innerGap = compact ? 10.0f : 4.0f;
        const float ddW = dropdown.getPreferredWidth();
        const float total = compact ? 28.0f + ddW + 28.0f + innerGap * 2.0f : 28.0f * 4.0f + ddW + innerGap * 4.0f;
        float cx = juce::jmax(regionLeft, 0.5f * (regionLeft + regionRight) - 0.5f * total);

        auto placeLeft = [&](juce::Component &c, float w, float ch)
        {
            c.setBounds(juce::Rectangle<float>(cx, cy - ch * 0.5f, w, ch).toNearestInt());
            cx += w + innerGap;
        };

        placeLeft(prevButton, 28.0f, 28.0f);
        placeLeft(dropdown, ddW, 30.0f);
        placeLeft(nextButton, 28.0f, 28.0f);
        if (!compact)
        {
            placeLeft(saveButton, 28.0f, 28.0f);
            placeLeft(resetButton, 28.0f, 28.0f);
        }
    }

    //==============================================================================
    // FooterBar
    //==============================================================================
    FooterBar::FooterBar(EditorContext &c, ViewMode m)
        : ctx(c), mode(m), modeControl(false),
          limiterAttachment(c.apvts, "limiter", limiterChip)
    {
        monitor.addItem("ORIGINAL", "Original\nHear the unprocessed input. Use it to compare against the result.",
                        Colours::inputCurve, SegmentedControl::Variant::original);
        monitor.addItem("PROCESSED", utf8("Processed\nHear the current output \xe2\x80\x94 what the host receives."),
                        Colours::teal, SegmentedControl::Variant::processed);
        monitor.addItem("REMOVED", utf8("Removed (difference)\nHear input minus output. If you hear words here, reduce the amount \xe2\x80\x94 speech is being damaged."),
                        Colours::amber, SegmentedControl::Variant::removed);
        monitor.setTitle("Monitoring");
        monitor.attachToParameter(
            ctx.param("monitor"), &ctx.undo, [](int i)
            { return (float)i; },
            [](float v)
            { return juce::roundToInt(v); });
        monitor.onUserPick = [this](int)
        {
            if (ctx.value("bandDelta") > 0.5f)
                ctx.setParam("bandDelta", 0.0f);
        };
        addAndMakeVisible(monitor);

        if (mode != ViewMode::compact)
        {
            limiterChip.setClickingTogglesState(true);
            limiterChip.setShowPip(true);
            limiterChip.setTooltip(utf8("Output limiter\nOptional safety limiter after output gain. Ceiling is set in Advanced \xe2\x80\xba Output."));
            addAndMakeVisible(limiterChip);

            modeControl.addItem("SIMPLE");
            modeControl.addItem("ADVANCED");
            modeControl.setTitle("Interface mode");
            modeControl.setSelectedIndex(mode == ViewMode::advanced ? 1 : 0, false);
            modeControl.onUserPick = [this](int i)
            { ctx.setMode(i == 0 ? ViewMode::simple : ViewMode::advanced); };
            addAndMakeVisible(modeControl);
        }

        if (mode == ViewMode::simple)
        {
            inputKnob = std::make_unique<Knob>(ctx.param("inputGain"), &ctx.undo, Knob::Style::mini);
            outputKnob = std::make_unique<Knob>(ctx.param("outputGain"), &ctx.undo, Knob::Style::mini);
            inputKnob->setTooltip("Input gain\nApplied before denoising. Double-click = 0 dB.");
            outputKnob->setTooltip("Output gain\nApplied after denoising, before the limiter. Double-click = 0 dB.");
            inputKnob->onValueChange = [this]
            { repaint(); };
            outputKnob->onValueChange = [this]
            { repaint(); };
            addAndMakeVisible(*inputKnob);
            addAndMakeVisible(*outputKnob);
        }
        else if (mode == ViewMode::advanced)
        {
            auto fmt = [](float v)
            { return formatSignedDb(v); };
            inputValue = std::make_unique<DragValue>(ctx.param("inputGain"), &ctx.undo, fmt, "dB", false);
            outputValue = std::make_unique<DragValue>(ctx.param("outputGain"), &ctx.undo, fmt, "dB", false);
            inputValue->setTooltip("Input gain\nDrag vertically to adjust. Double-click = 0 dB.");
            inputValue->setFontSize(12.0f);
            outputValue->setFontSize(12.0f);
            outputValue->setAlignRight(true);
            outputValue->setTooltip("Output gain\nDrag vertically, or use Stereo & Output. Double-click = 0 dB.");
            addAndMakeVisible(*inputValue);
            addAndMakeVisible(*outputValue);
        }

        refresh();
    }

    juce::String FooterBar::getTooltip()
    {
        if (channelBadge.contains(getMouseXYRelative().toFloat()))
            return "Channel configuration\nSet by the host track. Stereo channels are processed with linked gain unless Dual mono is chosen in Advanced.";
        return {};
    }

    void FooterBar::refresh()
    {
        const bool limiterOn = ctx.value("limiter") > 0.5f;
        limiterChip.setSuffix(limiterOn ? withMinus(juce::String(ctx.value("limiterCeiling"), 1)) + " dB" : "OFF");

        if (ctx.channelText() != laidOutChannelText)
        {
            laidOutChannelText = ctx.channelText();
            resized();
        }
        repaint();
    }

    void FooterBar::paint(juce::Graphics &g)
    {
        const auto r = getLocalBounds().toFloat();
        fillVerticalGradient(g, r, Colours::footerTop, Colours::footerBottom);
        g.setColour(Colours::panelBorder);
        g.fillRect(r.withHeight(1.0f));

        const float cy = r.getCentreY();
        for (float x : separators)
        {
            g.setColour(Colours::panelBorder);
            g.fillRect(juce::Rectangle<float>(x, cy - 15.0f, 1.0f, 30.0f));
        }

        drawLabel(g, "Monitor", monitorLabelArea);

        if (mode == ViewMode::compact)
        {
            // Right-hand readouts: gains · limiter · channels
            float x = r.getRight() - 12.0f;
            const auto chan = ctx.channelText();
            const auto chanFont = sans(10.0f, Weight::semibold, 0.06f);
            float w = chanFont.getStringWidthFloat(chan) + 2.0f;
            g.setFont(chanFont);
            g.setColour(Colours::textSoft);
            g.drawText(chan, juce::Rectangle<float>(x - w, 0.0f, w, r.getHeight()), juce::Justification::centredRight, false);
            x -= w + 14.0f;

            const bool limiterOn = ctx.value("limiter") > 0.5f;
            const juce::String lim = limiterOn ? "LIM " + withMinus(juce::String(ctx.value("limiterCeiling"), 1)) : "LIM OFF";
            w = chanFont.getStringWidthFloat(lim) + 2.0f;
            g.setColour(limiterOn ? Colours::tealSoft : Colours::textSecondary);
            g.drawText(lim, juce::Rectangle<float>(x - w, 0.0f, w, r.getHeight()), juce::Justification::centredRight, false);
            drawPip(g, {x - w - 9.0f, cy}, limiterOn ? Colours::tealSoft : Colours::textSecondary);
            x -= w + 12.0f + 14.0f;

            const auto f = mono(10.5f);
            const juce::String inText = formatSignedDb(ctx.value("inputGain"));
            const juce::String outText = formatSignedDb(ctx.value("outputGain"));
            const juce::String parts[] = {"IN ", inText, " " + middot + " OUT ", outText, " dB"};
            float total = 0.0f;
            for (auto &p : parts)
                total += f.getStringWidthFloat(p);
            float tx = x - total;
            g.setFont(f);
            for (int i = 0; i < 5; ++i)
            {
                g.setColour(i == 1 || i == 3 ? Colours::textPrimary : Colours::textSecondary);
                const float pw = f.getStringWidthFloat(parts[i]);
                g.drawText(parts[i], juce::Rectangle<float>(tx, 0.0f, pw + 1.0f, r.getHeight()), juce::Justification::centredLeft, false);
                tx += pw;
            }
            return;
        }

        if (mode == ViewMode::simple)
        {
            drawLabelValue(g, inputLabelArea, "Input", formatSignedDb(ctx.value("inputGain")), false);
            drawLabelValue(g, outputLabelArea, "Output", formatSignedDb(ctx.value("outputGain")), true);
        }
        else
        {
            drawLabel(g, "Input", inputLabelArea.withHeight(12.0f));
            drawLabel(g, "Output", outputLabelArea.withHeight(12.0f), Colours::textSecondary, 10.0f, 0.12f, juce::Justification::centredRight);
        }

        // Channel configuration badge
        g.setColour(Colours::tile);
        g.fillRoundedRectangle(channelBadge, 6.0f);
        g.setColour(Colours::panelBorder);
        g.drawRoundedRectangle(channelBadge.reduced(0.5f), 6.0f, 1.0f);

        float bx = channelBadge.getX() + 10.0f;
        if (showChannelIcon)
        {
            g.setColour(Colours::textSecondary);
            g.drawEllipse(bx + 1.5f, cy - 4.5f, 9.0f, 9.0f, 1.3f);
            if (ctx.numChannels() > 1)
                g.drawEllipse(bx + 11.5f, cy - 4.5f, 9.0f, 9.0f, 1.3f);
            bx += 30.0f;
        }
        g.setColour(Colours::textSoft);
        g.setFont(sans(11.0f, Weight::semibold, 0.06f));
        g.drawText(ctx.channelText(), juce::Rectangle<float>(bx, channelBadge.getY(), channelBadge.getRight() - bx, channelBadge.getHeight()),
                   juce::Justification::centredLeft, false);
    }

    void FooterBar::resized()
    {
        separators.clear();
        const float w = (float)getWidth(), cy = (float)getHeight() * 0.5f;
        const float monitorLabelW = labelFont().getStringWidthFloat("MONITOR") + 2.0f;

        if (mode == ViewMode::compact)
        {
            monitorLabelArea = {12.0f, 0.0f, monitorLabelW, (float)getHeight()};
            monitor.setBounds(centredBox(12.0f + monitorLabelW + 14.0f + monitor.getPreferredWidth() * 0.5f, cy, monitor.getPreferredWidth(), 32.0f).toNearestInt());
            return;
        }

        // The design's footer has almost no slack, so spacing tightens step by step until it fits
        const float valueColumn = mono(12.0f).getStringWidthFloat(utf8("\xe2\x88\x92" "24.0")) + 4.0f + mono(10.0f).getStringWidthFloat("dB") + 2.0f;
        const auto chanFont = sans(11.0f, Weight::semibold, 0.06f);

        for (int level = 0; level < 4; ++level)
        {
            const float gap = level == 0 ? 18.0f : (level == 1 ? 14.0f : 12.0f);
            const float segPad = level >= 2 ? 9.0f : 12.0f;
            showChannelIcon = mode == ViewMode::simple && level < 3;
            monitor.setItemPadding(segPad);
            modeControl.setItemPadding(segPad);
            separators.clear();

            float x = 16.0f;
            if (inputKnob != nullptr)
            {
                inputKnob->setBounds(juce::Rectangle<float>(x, cy - 18.0f, 36.0f, 36.0f).toNearestInt());
                x += 46.0f;
            }
            inputLabelArea = {x, cy - 15.0f, valueColumn, 30.0f};
            if (inputValue != nullptr)
                inputValue->setBounds(juce::Rectangle<float>(x, cy + 0.0f, valueColumn + 10.0f, 16.0f).toNearestInt());
            x += valueColumn + gap;

            separators.push_back(x);
            x += 1.0f + gap;

            monitorLabelArea = {x, 0.0f, monitorLabelW, (float)getHeight()};
            x += monitorLabelW + 10.0f;
            const float monW = monitor.getPreferredWidth();
            monitor.setBounds(juce::Rectangle<float>(x, cy - 16.0f, monW, 32.0f).toNearestInt());
            x += monW + gap;

            separators.push_back(x);
            x += 1.0f + gap;

            const float chipW = limiterChip.getPreferredWidth();
            limiterChip.setBounds(juce::Rectangle<float>(x, cy - 14.0f, chipW, 28.0f).toNearestInt());
            x += chipW + gap;

            const float badgeW = 20.0f + (showChannelIcon ? 30.0f : 0.0f) + chanFont.getStringWidthFloat(ctx.channelText()) + 2.0f;
            channelBadge = {x, cy - 14.0f, badgeW, 28.0f};
            const float leftEnd = x + badgeW;

            float xr = w - 16.0f;
            const float modeW = modeControl.getPreferredWidth();
            modeControl.setBounds(juce::Rectangle<float>(xr - modeW, cy - 16.0f, modeW, 32.0f).toNearestInt());
            xr -= modeW + gap;
            separators.push_back(xr - 1.0f);
            xr -= 1.0f + gap;

            if (outputKnob != nullptr)
            {
                outputKnob->setBounds(juce::Rectangle<float>(xr - 36.0f, cy - 18.0f, 36.0f, 36.0f).toNearestInt());
                xr -= 46.0f;
            }
            outputLabelArea = {xr - valueColumn, cy - 15.0f, valueColumn, 30.0f};
            if (outputValue != nullptr)
                outputValue->setBounds(juce::Rectangle<float>(xr - valueColumn - 10.0f, cy + 0.0f, valueColumn + 10.0f, 16.0f).toNearestInt());

            if (leftEnd + gap <= outputLabelArea.getX())
                break;
        }
    }

    //==============================================================================
    // StatusBar
    //==============================================================================
    StatusBar::StatusBar(EditorContext &c) : ctx(c)
    {
        refresh();
    }

    void StatusBar::refresh()
    {
        juce::StringArray next;
        next.add(ctx.channelText());
        next.add(juce::String(ctx.processor.getCurrentSampleRate() / 1000.0, 1) + " kHz (host)");
        next.add("Latency " + juce::String(ctx.processor.getLatencySamples()) + " smp (reported)");
        next.add("DSP load " + juce::String(juce::roundToInt(ctx.processor.getDspLoad() * 100.0)) + "%");
        next.add("RNNoise engine");

        if (next != items)
        {
            items = next;
            repaint();
        }
    }

    void StatusBar::paint(juce::Graphics &g)
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour(Colours::statusBar);
        g.fillRect(r);
        g.setColour(Colours::statusBorder);
        g.fillRect(r.withHeight(1.0f));

        const auto font = mono(9.5f, Weight::regular, 0.03f);
        g.setFont(font);

        g.setColour(Colours::amber);
        g.drawText("VOICEFRAME PRO " + juce::String(JucePlugin_VersionString) + " " + middot + " LIVE PROCESSING",
                   r.withTrimmedLeft(16.0f), juce::Justification::centredLeft, false);

        g.setColour(Colours::textTertiary);
        float x = r.getRight() - 16.0f;
        for (int i = items.size(); --i >= 0;)
        {
            const float w = font.getStringWidthFloat(items[i]) + 1.0f;
            g.drawText(items[i], juce::Rectangle<float>(x - w, 0.0f, w, r.getHeight()), juce::Justification::centredRight, false);
            x -= w + 14.0f;
        }
    }

    //==============================================================================
    // Shared panels
    //==============================================================================
    namespace
    {
        void updateMeters(EditorContext &ctx, LevelMeter &in, LevelMeter &out)
        {
            const int channels = ctx.numChannels();
            const int mon = juce::roundToInt(ctx.value("monitor"));
            in.setState(ctx.model.inputMeter, channels, ctx.processor.hasInputClipped(), "Input", Colours::textSecondary, false);
            out.setState(ctx.model.outputMeter, channels, ctx.processor.getOutputClipCount() > 0,
                         mon == 2 ? utf8("Out \xce\x94") : (mon == 0 ? "Out (orig)" : "Output"),
                         mon == 2 ? Colours::amber : Colours::textSecondary, mon == 2);
        }

        SpectrumView::State spectrumState(EditorContext &ctx)
        {
            SpectrumView::State s;
            s.monitor = juce::roundToInt(ctx.value("monitor"));
            s.deltaBand = juce::roundToInt(ctx.value("bandDelta")) - 1;
            s.bypassed = ctx.value("bypass") > 0.5f;
            return s;
        }

        juce::String grText(float db)
        {
            return std::abs(db) < 0.05f ? juce::String("0.0") : withMinus(juce::String(-std::abs(db), 1));
        }

        //==============================================================================
        /** Simple: voice-activity panel. */
        class VoicePanel : public juce::Component, public juce::TooltipClient
        {
        public:
            explicit VoicePanel(EditorContext &c) : ctx(c) {}

            juce::String getTooltip() override { return tips.find(*this); }

            void paint(juce::Graphics &g) override
            {
                const auto r = getLocalBounds().toFloat();
                drawPanel(g, r);
                tips.clear();

                const float x = 14.0f, w = r.getWidth() - 28.0f;
                const float vad = ctx.model.voiceProbability;
                const bool speech = vad > 0.5f;

                auto header = juce::Rectangle<float>(x, 12.0f, w, 14.0f);
                drawLabel(g, "Voice Activity", header);
                tips.add(header.withWidth(110.0f), utf8("Voice activity (estimate)\nRNNoise\xe2\x80\x99s per-frame speech probability. It is an estimate, not a measurement, "
                                                        "and is not used as an accuracy figure."));
                const float tw = tagWidth("Est.");
                drawTag(g, "Est.", {header.getRight() - tw, 12.5f, tw, 13.0f}, TagStyle::estimate);

                // LED · word · percentage
                const float rowY = 36.0f;
                drawLed(g, {x + 4.5f, rowY + 11.0f}, speech ? LedState::on : LedState::off);
                g.setColour(Colours::textPrimary);
                g.setFont(sans(18.0f, Weight::semibold, 0.01f));
                g.drawText(speech ? "Speech" : "Pause", juce::Rectangle<float>(x + 19.0f, rowY, 100.0f, 22.0f), juce::Justification::centredLeft, false);

                const auto pctFont = mono(18.0f), unitFont = mono(11.0f);
                const auto pct = juce::String(juce::roundToInt(vad * 100.0f));
                const float uw = unitFont.getStringWidthFloat("%");
                const float baseline = rowY + 17.0f;
                g.setColour(Colours::textSoft);
                g.setFont(pctFont);
                g.drawSingleLineText(pct, juce::roundToInt(x + w - uw), juce::roundToInt(baseline), juce::Justification::right);
                g.setColour(Colours::textTertiary);
                g.setFont(unitFont);
                g.drawSingleLineText("%", juce::roundToInt(x + w - uw), juce::roundToInt(baseline));

                // Probability bar
                const auto bar = juce::Rectangle<float>(x, 68.0f, w, 4.0f);
                g.setColour(Colours::well);
                g.fillRoundedRectangle(bar, 2.0f);
                g.setGradientFill(juce::ColourGradient(Colours::blueDark, bar.getX(), 0.0f, Colours::blue, bar.getRight(), 0.0f, false));
                g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * vad), 2.0f);

                g.setFont(sans(10.0f));
                g.setColour(Colours::textTertiary);
                g.drawText("Speech probability", juce::Rectangle<float>(x, 76.0f, w, 12.0f), juce::Justification::centredLeft, false);
                g.setFont(mono(10.0f));
                g.drawText(utf8("0\xe2\x80\x93" "100"), juce::Rectangle<float>(x, 76.0f, w, 12.0f), juce::Justification::centredRight, false);

                // 5-second history
                const auto graph = juce::Rectangle<float>(x, 98.0f, w, 54.0f);
                g.setColour(Colours::well);
                g.fillRoundedRectangle(graph, 5.0f);
                g.setColour(Colours::tileBorder);
                g.drawRoundedRectangle(graph.reduced(0.5f), 5.0f, 1.0f);

                juce::Path mid;
                mid.startNewSubPath(graph.getX(), graph.getCentreY());
                mid.lineTo(graph.getRight(), graph.getCentreY());
                juce::Path midDashed;
                const float dashes[] = {2.0f, 3.0f};
                juce::PathStrokeType(1.0f).createDashedStroke(midDashed, mid, dashes, 2);
                g.setColour(Colours::wellBorder);
                g.fillPath(midDashed);

                juce::Path line;
                const int n = EditorModel::historySize;
                for (int i = 0; i < n; ++i)
                {
                    const float px = graph.getX() + graph.getWidth() * (float)i / (float)(n - 1);
                    const float py = graph.getY() + (50.0f - ctx.model.getVadHistory(i) * 46.0f) * graph.getHeight() / 54.0f;
                    if (i == 0)
                        line.startNewSubPath(px, py);
                    else
                        line.lineTo(px, py);
                }
                juce::Path fill(line);
                fill.lineTo(graph.getRight(), graph.getBottom());
                fill.lineTo(graph.getX(), graph.getBottom());
                fill.closeSubPath();
                g.setColour(Colours::blue.withAlpha(0.16f));
                g.fillPath(fill);
                g.setColour(Colours::blue);
                g.strokePath(line, juce::PathStrokeType(1.3f));

                g.setFont(sans(10.0f));
                g.setColour(Colours::textTertiary);
                g.drawText("Last 5 s", juce::Rectangle<float>(x, 162.0f, w, 12.0f), juce::Justification::centredLeft, false);
                g.drawText("dashed = 50%", juce::Rectangle<float>(x, 162.0f, w, 12.0f), juce::Justification::centredRight, false);
            }

        private:
            EditorContext &ctx;
            TooltipRegions tips;
        };

        //==============================================================================
        /** Simple: gain reduction + status. */
        class ReductionPanel : public juce::Component, public juce::TooltipClient
        {
        public:
            explicit ReductionPanel(EditorContext &c) : ctx(c) {}

            juce::String getTooltip() override { return tips.find(*this); }

            void paint(juce::Graphics &g) override
            {
                const auto r = getLocalBounds().toFloat();
                drawPanel(g, r);
                tips.clear();

                const float x = 14.0f, w = r.getWidth() - 28.0f;
                const bool bypassed = ctx.value("bypass") > 0.5f;
                const float gr = bypassed ? 0.0f : ctx.model.gainReductionDb;

                auto header = juce::Rectangle<float>(x, 12.0f, w, 14.0f);
                drawLabel(g, "Gain Reduction", header);
                tips.add(header.withWidth(110.0f), utf8("Gain reduction (measured)\nOutput level minus input level, averaged over ~300 ms. "
                                                        "Larger in pauses, smaller while speech is present."));
                const float tw = tagWidth("Meas.");
                drawTag(g, "Meas.", {header.getRight() - tw, 12.5f, tw, 13.0f}, TagStyle::measured);

                const auto big = mono(30.0f, Weight::medium);
                const auto text = grText(gr);
                const float bw = big.getStringWidthFloat(text);
                g.setColour(Colours::amberLight);
                g.setFont(big);
                g.drawSingleLineText(text, juce::roundToInt(x), 62);
                g.setColour(Colours::textSecondary);
                g.setFont(mono(12.0f));
                g.drawSingleLineText("dB", juce::roundToInt(x + bw + 4.0f), 62);

                const auto bar = juce::Rectangle<float>(x, 78.0f, w, 6.0f);
                g.setColour(Colours::well);
                g.fillRoundedRectangle(bar, 3.0f);
                const float pct = juce::jlimit(0.0f, 1.0f, -gr / 30.0f);
                const auto fill = bar.withLeft(bar.getRight() - bar.getWidth() * pct);
                g.setGradientFill(juce::ColourGradient(Colours::amber, fill.getRight(), 0.0f, juce::Colour(0xff8c6a3e), fill.getX(), 0.0f, false));
                g.fillRoundedRectangle(fill, 3.0f);

                g.setColour(Colours::textTertiary);
                g.setFont(mono(9.0f));
                const juce::String scale[] = {withMinus("-30"), withMinus("-20"), withMinus("-10"), "0"};
                for (int i = 0; i < 4; ++i)
                    g.drawText(scale[i], juce::Rectangle<float>(x + w * (float)i / 3.0f - 15.0f, 88.0f, 30.0f, 11.0f),
                               i == 0 ? juce::Justification::centredLeft : (i == 3 ? juce::Justification::centredRight : juce::Justification::centred), false);
                // keep first/last labels flush with the bar ends
                g.setColour(Colours::panelBorder);
                g.fillRect(juce::Rectangle<float>(x, 111.0f, w, 1.0f));

                // Status rows
                const bool limiterOn = ctx.value("limiter") > 0.5f;
                const float limGr = ctx.processor.getLimiterReductionDb();
                const int clips = ctx.processor.getOutputClipCount();
                struct Row
                {
                    LedState led;
                    juce::String label, value;
                    juce::Colour colour;
                    bool monoValue;
                };
                const Row rows[] = {
                    {bypassed ? LedState::amber : LedState::on, "Processing", bypassed ? "Bypassed" : "Active", bypassed ? Colours::amberLight : Colours::tealText, false},
                    {LedState::dim, "Limiter", limiterOn ? (limGr < -0.1f ? "On " + middot + " " + withMinus(juce::String(limGr, 1)) + " dB" : "On " + middot + " idle") : "Off", Colours::textSoft, false},
                    {clips > 0 ? LedState::warn : LedState::off, "Output clips", juce::String(clips), Colours::textSoft, true},
                };

                float y = 122.0f;
                for (const auto &row : rows)
                {
                    drawLed(g, {x + 4.5f, y + 7.0f}, row.led, 9.0f, false);
                    g.setFont(sans(11.0f));
                    g.setColour(Colours::textSecondary);
                    g.drawText(row.label, juce::Rectangle<float>(x + 17.0f, y, 100.0f, 14.0f), juce::Justification::centredLeft, false);
                    g.setFont(row.monoValue ? mono(11.0f) : sans(11.0f, Weight::semibold));
                    g.setColour(row.colour);
                    g.drawText(row.value, juce::Rectangle<float>(x, y, w, 14.0f), juce::Justification::centredRight, false);
                    y += 21.0f;
                }
            }

        private:
            EditorContext &ctx;
            TooltipRegions tips;
        };

        //==============================================================================
        /** The main amount knob with its title and Natural / Strong ends. */
        class AmountPanel : public juce::Component, public juce::TooltipClient
        {
        public:
            AmountPanel(EditorContext &c, Knob::Style s) : ctx(c), style(s), knob(c.param("amount"), &c.undo, s)
            {
                knob.setTooltip("Noise reduction amount\nDrag " + middot + " Shift = fine " + middot + " Double-click = 50%");
                addAndMakeVisible(knob);
            }

            juce::String getTooltip() override { return tips.find(*this); }

            static juce::String titleTip()
            {
                return utf8("Noise Reduction \xe2\x80\x94 processing intensity\nHow strongly the RNNoise result replaces the original signal "
                            "(0% = original, 100% = full processing). It is not a measured percentage of noise removed.");
            }

            void resized() override
            {
                const float w = (float)getWidth(), h = (float)getHeight();
                if (style == Knob::Style::hero)
                    knob.setBounds(centredBox(w * 0.5f, h - 8.0f - 14.0f - 102.0f, 204.0f, 204.0f).toNearestInt());
                else
                    knob.setBounds(centredBox(w * 0.5f, 35.0f + 88.0f, 176.0f, 176.0f).toNearestInt());
            }

            void paint(juce::Graphics &g) override
            {
                const auto r = getLocalBounds().toFloat();
                tips.clear();

                if (style == Knob::Style::hero)
                {
                    drawGlowPanel(g, r, {r.getCentreX(), r.getHeight() * 0.55f}, r.getWidth() * 0.7f);
                    const auto title = juce::Rectangle<float>(0.0f, 10.0f, r.getWidth(), 14.0f);
                    drawLabel(g, "Noise Reduction", title, Colours::textSoft, 11.0f, 0.16f, juce::Justification::centred);
                    tips.add(title.withSizeKeepingCentre(160.0f, 14.0f), titleTip());

                    const auto k = knob.getBounds().toFloat();
                    g.setFont(sans(10.5f));
                    g.setColour(Colours::textSecondary);
                    g.drawText("Natural", juce::Rectangle<float>(k.getX() - 6.0f - 52.0f, k.getBottom() - 22.0f - 14.0f, 52.0f, 14.0f), juce::Justification::centredRight, false);
                    g.drawText("Strong", juce::Rectangle<float>(k.getRight() + 6.0f, k.getBottom() - 22.0f - 14.0f, 52.0f, 14.0f), juce::Justification::centredLeft, false);

                    g.setFont(sans(10.0f));
                    g.setColour(Colours::textHint);
                    g.drawText("Drag " + middot + " Shift = fine " + middot + " Double-click = 50%",
                               juce::Rectangle<float>(0.0f, r.getBottom() - 8.0f - 12.0f, r.getWidth(), 12.0f), juce::Justification::centred, false);
                    return;
                }

                // Compact: knob, Natural / Strong, then voice + gain-reduction tiles
                drawGlowPanel(g, r, {r.getCentreX(), r.getHeight() * 0.4f}, r.getWidth() * 0.8f);
                const auto title = juce::Rectangle<float>(0.0f, 12.0f, r.getWidth(), 13.0f);
                drawLabel(g, "Noise Reduction", title, Colours::textSoft, 10.5f, 0.16f, juce::Justification::centred);
                tips.add(title.withSizeKeepingCentre(160.0f, 13.0f), titleTip());

                const auto k = knob.getBounds().toFloat();
                g.setFont(sans(10.0f));
                g.setColour(Colours::textSecondary);
                const auto ends = centredBox(r.getCentreX(), k.getBottom() - 10.0f + 7.0f, 196.0f, 14.0f);
                g.drawText("Natural", ends, juce::Justification::centredLeft, false);
                g.drawText("Strong", ends, juce::Justification::centredRight, false);

                const float tileW = (r.getWidth() - 28.0f - 8.0f) * 0.5f;
                const auto voiceTile = juce::Rectangle<float>(14.0f, r.getBottom() - 12.0f - 50.0f, tileW, 50.0f);
                const auto grTile = voiceTile.translated(tileW + 8.0f, 0.0f);
                drawTile(g, voiceTile);
                drawTile(g, grTile);

                auto tileHeader = [&](juce::Rectangle<float> tile, const juce::String &label, const juce::String &tag, TagStyle style_)
                {
                    const auto f = sans(9.5f, Weight::semibold, 0.08f);
                    const float lw = f.getStringWidthFloat(label);
                    g.setFont(f);
                    g.setColour(Colours::textSecondary);
                    g.drawText(label, juce::Rectangle<float>(tile.getX() + 9.0f, tile.getY() + 8.0f, lw + 2.0f, 13.0f), juce::Justification::centredLeft, false);
                    const float tw = tagWidth(tag);
                    drawTag(g, tag, {tile.getX() + 9.0f + lw + 6.0f, tile.getY() + 8.0f, tw, 13.0f}, style_);
                };

                const float vad = ctx.model.voiceProbability;
                tileHeader(voiceTile, "VOICE", "Est", TagStyle::estimate);
                const float rowY = voiceTile.getY() + 26.0f;
                drawLed(g, {voiceTile.getX() + 13.5f, rowY + 8.0f}, vad > 0.5f ? LedState::on : LedState::off);
                g.setColour(Colours::textPrimary);
                g.setFont(sans(12.0f, Weight::semibold));
                g.drawText(vad > 0.5f ? "Speech" : "Pause", juce::Rectangle<float>(voiceTile.getX() + 25.0f, rowY, 60.0f, 16.0f), juce::Justification::centredLeft, false);
                g.setColour(Colours::textSecondary);
                g.setFont(mono(12.0f));
                g.drawText(juce::String(juce::roundToInt(vad * 100.0f)) + "%", juce::Rectangle<float>(voiceTile.getX(), rowY, voiceTile.getWidth() - 9.0f, 16.0f),
                           juce::Justification::centredRight, false);

                tileHeader(grTile, "GAIN RED.", "Meas", TagStyle::measured);
                const bool bypassed = ctx.value("bypass") > 0.5f;
                const auto text = grText(bypassed ? 0.0f : ctx.model.gainReductionDb);
                const auto vf = mono(14.0f);
                g.setColour(Colours::amberLight);
                g.setFont(vf);
                g.drawText(text, juce::Rectangle<float>(grTile.getX() + 9.0f, rowY, 80.0f, 16.0f), juce::Justification::centredLeft, false);
                g.setColour(Colours::textSecondary);
                g.setFont(mono(10.0f));
                g.drawText("dB", juce::Rectangle<float>(grTile.getX() + 9.0f + vf.getStringWidthFloat(text) + 4.0f, rowY + 1.0f, 20.0f, 16.0f),
                           juce::Justification::centredLeft, false);
            }

        private:
            EditorContext &ctx;
            Knob::Style style;
            Knob knob;
            TooltipRegions tips;
        };

        //==============================================================================
        class SimpleBody : public RefreshingComponent
        {
        public:
            explicit SimpleBody(EditorContext &c)
                : ctx(c), spectrum(c.model, SpectrumView::Layout::simple), voice(c), amount(c, Knob::Style::hero), reduction(c)
            {
                for (juce::Component *comp : {(juce::Component *)&inputMeter, (juce::Component *)&outputMeter, (juce::Component *)&spectrum,
                                              (juce::Component *)&voice, (juce::Component *)&amount, (juce::Component *)&reduction})
                    addAndMakeVisible(comp);

                inputMeter.onClipClicked = outputMeter.onClipClicked = [this]
                { ctx.processor.resetClipIndicators(); };
            }

            void refresh() override
            {
                updateMeters(ctx, inputMeter, outputMeter);
                spectrum.setState(spectrumState(ctx));
                voice.repaint();
                reduction.repaint();
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced(14);
                inputMeter.setBounds(r.removeFromLeft(64));
                outputMeter.setBounds(r.removeFromRight(64));
                r.removeFromLeft(12);
                r.removeFromRight(12);

                spectrum.setBounds(r.removeFromTop(262));
                r.removeFromTop(12);
                voice.setBounds(r.removeFromLeft(236));
                reduction.setBounds(r.removeFromRight(236));
                r.removeFromLeft(12);
                r.removeFromRight(12);
                amount.setBounds(r);
            }

        private:
            EditorContext &ctx;
            LevelMeter inputMeter{LevelMeter::Side::input}, outputMeter{LevelMeter::Side::output};
            SpectrumView spectrum;
            VoicePanel voice;
            AmountPanel amount;
            ReductionPanel reduction;
        };

        //==============================================================================
        /** Advanced: global amount knob + voice / GR tiles. */
        class GlobalPanel : public juce::Component, public juce::TooltipClient
        {
        public:
            explicit GlobalPanel(EditorContext &c) : ctx(c), knob(c.param("amount"), &c.undo, Knob::Style::global)
            {
                knob.setTooltip("Noise reduction amount\nDrag " + middot + " Shift = fine " + middot + " Double-click = 50%");
                addAndMakeVisible(knob);
            }

            juce::String getTooltip() override
            {
                const auto pos = getMouseXYRelative();
                if (pos.y < 24)
                    return utf8("Global processing intensity\nScales every band\xe2\x80\x99s amount. Not a measured % of noise removed.");
                return {};
            }

            void resized() override
            {
                knob.setBounds(juce::Rectangle<int>(128, 128).withCentre({getWidth() / 2, 26 + 64}));
            }

            void paint(juce::Graphics &g) override
            {
                const auto r = getLocalBounds().toFloat();
                drawGlowPanel(g, r, {r.getCentreX(), r.getHeight() * 0.42f}, r.getWidth() * 0.8f);
                drawLabel(g, "Noise Reduction", {0.0f, 10.0f, r.getWidth(), 12.0f}, Colours::textSoft, 10.0f, 0.16f, juce::Justification::centred);

                const auto k = knob.getBounds().toFloat();
                g.setFont(sans(10.0f));
                g.setColour(Colours::textSecondary);
                g.drawText("Natural", juce::Rectangle<float>(k.getX() - 4.0f - 50.0f, k.getBottom() - 12.0f - 13.0f, 50.0f, 13.0f), juce::Justification::centredRight, false);
                g.drawText("Strong", juce::Rectangle<float>(k.getRight() + 4.0f, k.getBottom() - 12.0f - 13.0f, 50.0f, 13.0f), juce::Justification::centredLeft, false);

                const float tileW = (r.getWidth() - 24.0f - 8.0f) * 0.5f;
                const auto voiceTile = juce::Rectangle<float>(12.0f, k.getBottom() + 10.0f, tileW, 48.0f);
                const auto grTile = voiceTile.translated(tileW + 8.0f, 0.0f);
                drawTile(g, voiceTile);
                drawTile(g, grTile);

                auto header = [&](juce::Rectangle<float> tile, const juce::String &label, const juce::String &tag, TagStyle style)
                {
                    const auto f = sans(9.5f, Weight::semibold, 0.08f);
                    const float lw = f.getStringWidthFloat(label);
                    g.setFont(f);
                    g.setColour(Colours::textSecondary);
                    g.drawText(label, juce::Rectangle<float>(tile.getX() + 8.0f, tile.getY() + 7.0f, lw + 2.0f, 13.0f), juce::Justification::centredLeft, false);
                    const float tw = tagWidth(tag, 9.0f, 3.0f);
                    drawTag(g, tag, {tile.getX() + 8.0f + lw + 6.0f, tile.getY() + 7.0f, tw, 13.0f}, style);
                };

                const float vad = ctx.model.voiceProbability;
                header(voiceTile, "VOICE", "Est", TagStyle::estimate);
                drawLed(g, {voiceTile.getX() + 12.5f, voiceTile.getY() + 32.0f}, vad > 0.5f ? LedState::on : LedState::off);
                g.setColour(Colours::textPrimary);
                g.setFont(mono(14.0f));
                g.drawText(juce::String(juce::roundToInt(vad * 100.0f)) + "%", juce::Rectangle<float>(voiceTile.getX() + 23.0f, voiceTile.getY() + 24.0f, 60.0f, 16.0f),
                           juce::Justification::centredLeft, false);

                header(grTile, "GR", "Meas", TagStyle::measured);
                const bool bypassed = ctx.value("bypass") > 0.5f;
                const auto text = grText(bypassed ? 0.0f : ctx.model.gainReductionDb);
                const auto vf = mono(14.0f);
                g.setColour(Colours::amberLight);
                g.setFont(vf);
                g.drawText(text, juce::Rectangle<float>(grTile.getX() + 8.0f, grTile.getY() + 24.0f, 70.0f, 16.0f), juce::Justification::centredLeft, false);
                g.setColour(Colours::textSecondary);
                g.setFont(mono(10.0f));
                g.drawText("dB", juce::Rectangle<float>(grTile.getX() + 8.0f + vf.getStringWidthFloat(text) + 4.0f, grTile.getY() + 25.0f, 20.0f, 16.0f),
                           juce::Justification::centredLeft, false);
            }

        private:
            EditorContext &ctx;
            Knob knob;
        };

        //==============================================================================
        const char *const bandNames[numBands] = {"Rumble", "Warmth", "Body", "Clarity", "Presence", "Air"};

        /** Advanced: selected-band editor. */
        class BandEditorPanel : public juce::Component, public juce::TooltipClient
        {
        public:
            BandEditorPanel(EditorContext &c, BandOverlay &o) : ctx(c), overlay(o)
            {
                prevBand.onClick = [this]
                { select((band + numBands - 1) % numBands); };
                nextBand.onClick = [this]
                { select((band + 1) % numBands); };
                prevBand.setTooltip("Previous band");
                nextBand.setTooltip("Next band");

                soloChip.setGlyph("S");
                soloChip.onClick = [this]
                { ctx.setParam("bandSolo", soloing() ? 0.0f : (float)(band + 1)); };
                bypassChip.setPowerIcon(true);
                bypassChip.setClickingTogglesState(true);
                listenChip.setGlyph(utf8("\xce\x94"));
                listenChip.setTooltip("Band removed-signal monitor\nHear only what this band takes away (difference signal).");
                listenChip.onClick = [this]
                { ctx.setParam("bandDelta", listening() ? 0.0f : (float)(band + 1)); };

                for (juce::Component *comp : {(juce::Component *)&prevBand, (juce::Component *)&nextBand, (juce::Component *)&soloChip,
                                              (juce::Component *)&bypassChip, (juce::Component *)&listenChip})
                    addAndMakeVisible(comp);

                select(juce::jlimit(0, numBands - 1, (int)ctx.processor.getUIState().getProperty("selectedBand", 2)));
            }

            void select(int newBand)
            {
                band = newBand;
                ctx.processor.getUIState().setProperty("selectedBand", band, nullptr);
                overlay.setSelectedBand(band);

                amountKnob = std::make_unique<Knob>(ctx.param("bandAmount" + juce::String(band + 1)), &ctx.undo, Knob::Style::band);
                amountKnob->setArcColour(Colours::bands[band]);
                amountKnob->setTooltip(utf8("Band amount \xe2\x80\x94 frequency-dependent blend\nMix of RNNoise output vs. original inside this crossover band. "
                                            "0% = original, 100% = processed (\xc3\x97 global amount)."));
                addAndMakeVisible(*amountKnob);

                bypassAttachment.reset();
                bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ctx.apvts, "bandBypass" + juce::String(band + 1), bypassChip);

                resized();
                refresh();
            }

            int getBand() const { return band; }

            void refresh()
            {
                soloChip.setToggleState(soloing(), juce::dontSendNotification);
                listenChip.setToggleState(listening(), juce::dontSendNotification);
                repaint();
            }

            juce::String getTooltip() override
            {
                const auto pos = getMouseXYRelative();
                if (amountKnob != nullptr && pos.y > amountKnob->getBottom() && pos.x < amountKnob->getRight())
                    return amountKnob->getTooltip();
                return {};
            }

            void resized() override
            {
                const int w = getWidth();
                nextBand.setBounds(w - 14 - 24, 10, 24, 24);
                prevBand.setBounds(w - 14 - 24 - 4 - 24, 10, 24, 24);

                if (amountKnob != nullptr)
                    amountKnob->setBounds(14, 44, 92, 92);

                const int chipX = 14 + 92 + 14, chipW = w - 14 - chipX;
                soloChip.setBounds(chipX, 49, chipW, 28);
                bypassChip.setBounds(chipX, 49 + 34, chipW, 28);
                listenChip.setBounds(chipX, 49 + 68, chipW, 28);
            }

            void paint(juce::Graphics &g) override
            {
                const auto r = getLocalBounds().toFloat();
                drawPanel(g, r);

                const auto colour = Colours::bands[band];
                g.setColour(colour);
                g.fillRoundedRectangle(14.0f, 17.0f, 10.0f, 10.0f, 3.0f);

                const juce::String title = "Band " + juce::String(band + 1);
                drawLabel(g, title, {32.0f, 10.0f, 80.0f, 24.0f}, Colours::textPrimary);
                g.setFont(sans(11.0f));
                g.setColour(Colours::textSecondary);
                g.drawText(bandNames[band], juce::Rectangle<float>(32.0f + labelFont().getStringWidthFloat(title.toUpperCase()) + 8.0f, 10.0f, 100.0f, 24.0f),
                           juce::Justification::centredLeft, false);

                drawLabel(g, "Band Amount", {14.0f, 138.0f, 92.0f, 12.0f}, Colours::textSecondary, 9.5f, 0.12f, juce::Justification::centred);

                // Edge fields
                const float lo = band == 0 ? 20.0f : ctx.value("xover" + juce::String(band));
                const float hi = band == numBands - 1 ? 20000.0f : ctx.value("xover" + juce::String(band + 1));
                const float colW = (r.getWidth() - 28.0f - 8.0f) * 0.5f;
                auto field = [&](float fx, const juce::String &label, float hz)
                {
                    g.setFont(sans(9.5f, Weight::semibold, 0.08f));
                    g.setColour(Colours::textSecondary);
                    g.drawText(label, juce::Rectangle<float>(fx, 158.0f, colW, 12.0f), juce::Justification::centredLeft, false);
                    const auto box = juce::Rectangle<float>(fx, 173.0f, colW, 24.0f);
                    g.setColour(Colours::well);
                    g.fillRoundedRectangle(box, 5.0f);
                    g.setColour(Colours::wellBorder);
                    g.drawRoundedRectangle(box.reduced(0.5f), 5.0f, 1.0f);
                    const auto valueText = hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) : juce::String(juce::roundToInt(hz));
                    const auto vf = mono(11.5f);
                    g.setFont(vf);
                    g.setColour(Colours::textPrimary);
                    g.drawText(valueText, box.withTrimmedLeft(8.0f), juce::Justification::centredLeft, false);
                    g.setColour(Colours::textTertiary);
                    g.drawText(hz >= 1000.0f ? "kHz" : "Hz", box.withTrimmedLeft(8.0f + vf.getStringWidthFloat(valueText) + 6.0f), juce::Justification::centredLeft, false);
                };
                field(14.0f, "LOW EDGE", lo);
                field(14.0f + colW + 8.0f, "HIGH EDGE", hi);

                g.setFont(sans(10.0f));
                g.setColour(Colours::textTertiary);
                g.drawFittedText("Crossover filters + per-band blend of original and RNNoise output. RNNoise itself runs once, full-band.",
                                 juce::Rectangle<int>(14, 203, getWidth() - 28, 28), juce::Justification::topLeft, 2, 0.9f);
            }

        private:
            bool soloing() const { return juce::roundToInt(ctx.value("bandSolo")) == band + 1; }
            bool listening() const { return juce::roundToInt(ctx.value("bandDelta")) == band + 1; }

            EditorContext &ctx;
            BandOverlay &overlay;
            int band = 2;
            IconButton prevBand{"Previous band", Icon::chevronLeft, 24.0f};
            IconButton nextBand{"Next band", Icon::chevronRight, 24.0f};
            std::unique_ptr<Knob> amountKnob;
            Chip soloChip{"Solo band", Chip::Variant::solo};
            Chip bypassChip{"Bypass band", Chip::Variant::amber};
            Chip listenChip{"Listen removed", Chip::Variant::amberStrong};
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
        };

        //==============================================================================
        /** Advanced: Voice Gate / Stereo & Output tabs. */
        class AdvancedTabs : public juce::Component, public juce::TooltipClient
        {
        public:
            explicit AdvancedTabs(EditorContext &c)
                : ctx(c),
                  gateAttachment(c.apvts, "gateOn", gateSwitch),
                  limiterAttachment(c.apvts, "limiter", limiterSwitch),
                  widthFader(c.param("stereoWidth"), &c.undo, false),
                  gainFader(c.param("outputGain"), &c.undo, true),
                  ceiling(
                      c.param("limiterCeiling"), &c.undo, [](float v)
                      { return withMinus(juce::String(v, 1)); },
                      "dBFS", true)
            {
                gateTab.setLedOn(true);
                gateTab.onClick = [this]
                { showTab(0); };
                outTab.onClick = [this]
                { showTab(1); };
                addAndMakeVisible(gateTab);
                addAndMakeVisible(outTab);

                // Gate page
                struct GateKnob
                {
                    const char *id, *label, *shortLabel, *tip;
                };
                const GateKnob gk[] = {
                    {"gateSensitivity", "Speech sensitivity", "SENS", "Speech-probability threshold. Higher = gate opens on fainter speech."},
                    {"gateRange", "Gate range", "RANGE", "Maximum extra attenuation applied in pauses."},
                    {"gateAttack", "Attack", "ATK", "How fast the gate opens when speech is detected."},
                    {"gateHold", "Hold", "HOLD", "Time the gate stays open after speech ends."},
                    {"gateRelease", "Release", "REL", "How smoothly the gate closes into the pause."},
                };
                for (const auto &k : gk)
                {
                    auto knob = std::make_unique<Knob>(ctx.param(k.id), &ctx.undo, Knob::Style::gate);
                    knob->setTooltip(juce::String(k.label) + "\n" + k.tip);
                    knob->onValueChange = [this]
                    { repaint(); };
                    addChildComponent(*knob);
                    gateKnobs.push_back({std::move(knob), k.shortLabel});
                }
                gateSwitch.setTooltip("Voice gate\nExtra attenuation in pauses, driven by RNNoise's voice-activity estimate.");
                addChildComponent(gateSwitch);

                // Stereo & output page
                stereoLink.addItem("LINKED");
                stereoLink.addItem("DUAL MONO");
                stereoLink.setTitle("Stereo link");
                stereoLink.attachToParameter(
                    ctx.param("stereoLink"), &ctx.undo, [](int i)
                    { return i == 0 ? 1.0f : 0.0f; },
                    [](float v)
                    { return v > 0.5f ? 0 : 1; });
                stereoLinkTip = "Stereo link\nLinked applies one shared gain to both channels to keep the image steady. Dual mono processes L and R independently.";
                widthFader.setTooltip("Stereo width\nMid/side width of the output. 100% = unchanged. Double-click = 100%.");
                gainFader.setTooltip("Output gain\nDouble-click = 0 dB.");
                ceiling.setTooltip("Limiter ceiling\nDrag vertically. Double-click = " + withMinus("-1.0") + " dBFS.");
                limiterSwitch.setTooltip("Output limiter");
                widthFader.onValueChange = gainFader.onValueChange = [this]
                { repaint(); };

                for (auto *comp : {(juce::Component *)&stereoLink, (juce::Component *)&widthFader, (juce::Component *)&gainFader,
                                   (juce::Component *)&limiterSwitch, (juce::Component *)&ceiling})
                    addChildComponent(comp);

                showTab((int)ctx.processor.getUIState().getProperty("advancedTab", 0));
            }

            void showTab(int tab)
            {
                currentTab = juce::jlimit(0, 1, tab);
                ctx.processor.getUIState().setProperty("advancedTab", currentTab, nullptr);
                gateTab.setToggleState(currentTab == 0, juce::dontSendNotification);
                outTab.setToggleState(currentTab == 1, juce::dontSendNotification);

                gateSwitch.setVisible(currentTab == 0);
                for (auto &k : gateKnobs)
                    k.first->setVisible(currentTab == 0);
                for (auto *comp : {(juce::Component *)&stereoLink, (juce::Component *)&widthFader, (juce::Component *)&gainFader,
                                   (juce::Component *)&limiterSwitch, (juce::Component *)&ceiling})
                    comp->setVisible(currentTab == 1);
                refresh();
            }

            void refresh()
            {
                const bool gateOn = ctx.value("gateOn") > 0.5f;
                gateTab.setLedOn(gateOn);
                for (auto &k : gateKnobs)
                {
                    k.first->setEnabled(gateOn);
                }

                const bool mono = ctx.numChannels() < 2;
                stereoLink.setItemsEnabled(!mono);
                widthFader.setEnabled(!mono);
                repaint();
            }

            juce::String getTooltip() override
            {
                const auto pos = getMouseXYRelative().toFloat();
                if (currentTab == 1 && stereoLinkArea.contains(pos))
                    return stereoLinkTip;
                if (currentTab == 0 && tagArea.contains(pos))
                    return "Voice gate\nExtra DSP after the blend. It uses RNNoise's voice-activity estimate, not a level threshold.";
                return {};
            }

            void resized() override
            {
                const int w = getWidth();
                gateTab.setBounds(8, 0, juce::roundToInt(sans(10.5f, Weight::semibold, 0.1f).getStringWidthFloat("VOICE GATE")) + 40, 34);
                outTab.setBounds(gateTab.getRight() + 2, 0, juce::roundToInt(sans(10.5f, Weight::semibold, 0.1f).getStringWidthFloat("STEREO & OUTPUT")) + 26, 34);

                const float x = 12.0f, cw = (float)w - 24.0f;
                gateSwitch.setBounds(juce::roundToInt(x), 44, 30, 16);

                const float colW = (cw - 16.0f) / 5.0f;
                for (size_t i = 0; i < gateKnobs.size(); ++i)
                    gateKnobs[i].first->setBounds(juce::Rectangle<float>(34.0f, 34.0f).withCentre({x + colW * 0.5f + (colW + 4.0f) * (float)i, 162.0f}).toNearestInt());

                const float half = (cw - 14.0f) * 0.5f;
                stereoLink.setBounds(juce::Rectangle<float>(x, 63.0f, half, 28.0f).toNearestInt());
                stereoLinkArea = stereoLink.getBounds().toFloat();
                widthFader.setBounds(juce::Rectangle<float>(x - Fader::thumbOverhang, 122.0f, half + Fader::thumbOverhang * 2.0f, 18.0f).toNearestInt());

                const float ox = x + half + 14.0f;
                gainFader.setBounds(juce::Rectangle<float>(ox - Fader::thumbOverhang, 85.0f, half + Fader::thumbOverhang * 2.0f, 18.0f).toNearestInt());
                limiterSwitch.setBounds(juce::roundToInt(ox), 120, 30, 16);
                ceiling.setBounds(juce::Rectangle<float>(ox + half - 78.0f, 117.0f, 78.0f, 22.0f).toNearestInt());
                ceiling.setFontSize(10.5f);
            }

            void paint(juce::Graphics &g) override
            {
                const auto r = getLocalBounds().toFloat();
                drawPanel(g, r);
                g.setColour(Colours::panelBorder);
                g.fillRect(juce::Rectangle<float>(1.0f, 34.0f, r.getWidth() - 2.0f, 1.0f));

                const float x = 12.0f, cw = r.getWidth() - 24.0f;

                if (currentTab == 0)
                {
                    const bool gateOn = ctx.value("gateOn") > 0.5f;
                    const float alpha = gateOn ? 1.0f : 0.55f;

                    g.setFont(sans(11.0f, Weight::semibold));
                    g.setColour(Colours::textPrimary.withMultipliedAlpha(alpha));
                    g.drawText(gateOn ? "Gate enabled" : "Gate disabled", juce::Rectangle<float>(x + 38.0f, 44.0f, 120.0f, 16.0f), juce::Justification::centredLeft, false);

                    const juce::String tag = utf8("Extra DSP \xc2\xb7 uses VAD");
                    const float tw = tagWidth(tag);
                    tagArea = {x + cw - tw, 45.5f, tw, 13.0f};
                    drawTag(g, tag, tagArea, TagStyle::plain);

                    // Trace: speech probability, sensitivity threshold, gate gain
                    const auto trace = juce::Rectangle<float>(x, 68.0f, cw, 50.0f);
                    g.beginTransparencyLayer(alpha);
                    g.setColour(Colours::well);
                    g.fillRoundedRectangle(trace, 5.0f);
                    g.setColour(Colours::tileBorder);
                    g.drawRoundedRectangle(trace.reduced(0.5f), 5.0f, 1.0f);

                    const int n = EditorModel::historySize;
                    const float sens = ctx.value("gateSensitivity") / 100.0f;
                    const float rangeDb = ctx.value("gateRange");
                    const float rangeGain = juce::Decibels::decibelsToGain(rangeDb, -100.0f);
                    auto px = [&](int i)
                    { return trace.getX() + trace.getWidth() * (float)i / (float)(n - 1); };
                    auto py = [&](float units)
                    { return trace.getY() + units * trace.getHeight() / 50.0f; };

                    juce::Path vadLine, gainLine;
                    for (int i = 0; i < n; ++i)
                    {
                        const float v = ctx.model.getVadHistory(i);
                        const float closed = rangeGain < 0.999f ? juce::jlimit(0.0f, 1.0f, (1.0f - ctx.model.getGateHistory(i)) / (1.0f - rangeGain)) : 0.0f;
                        const juce::Point<float> pv(px(i), py(47.0f - v * 43.0f));
                        const juce::Point<float> pg(px(i), py(4.0f + closed * 42.0f * (-rangeDb / 60.0f)));
                        if (i == 0)
                        {
                            vadLine.startNewSubPath(pv);
                            gainLine.startNewSubPath(pg);
                        }
                        else
                        {
                            vadLine.lineTo(pv);
                            gainLine.lineTo(pg);
                        }
                    }
                    juce::Path vadFill(vadLine);
                    vadFill.lineTo(trace.getRight(), trace.getBottom());
                    vadFill.lineTo(trace.getX(), trace.getBottom());
                    vadFill.closeSubPath();
                    g.setColour(Colours::blue.withAlpha(0.16f));
                    g.fillPath(vadFill);
                    g.setColour(Colours::blue);
                    g.strokePath(vadLine, juce::PathStrokeType(1.1f));

                    juce::Path thr, thrDashed;
                    const float ty = py(47.0f - (1.0f - sens) * 43.0f);
                    thr.startNewSubPath(trace.getX(), ty);
                    thr.lineTo(trace.getRight(), ty);
                    const float dashes[] = {3.0f, 3.0f};
                    juce::PathStrokeType(1.0f).createDashedStroke(thrDashed, thr, dashes, 2);
                    g.setColour(Colours::teal);
                    g.fillPath(thrDashed);

                    g.setColour(Colours::amber);
                    g.strokePath(gainLine, juce::PathStrokeType(1.5f));
                    g.endTransparencyLayer();

                    // Legend
                    g.setFont(sans(9.5f));
                    float lx = x;
                    const std::pair<juce::String, juce::Colour> legend[] = {
                        {utf8("\xe2\x80\x94 Speech prob."), Colours::blue},
                        {"- - Sensitivity", Colours::teal},
                        {utf8("\xe2\x80\x94 Gate gain (0 to range)"), Colours::amber}};
                    for (const auto &[text, colour] : legend)
                    {
                        g.setColour(colour.withMultipliedAlpha(alpha));
                        const float lw = sans(9.5f).getStringWidthFloat(text);
                        g.drawText(text, juce::Rectangle<float>(lx, 124.0f, lw + 2.0f, 12.0f), juce::Justification::centredLeft, false);
                        lx += lw + 10.0f;
                    }

                    // Knob captions
                    const float colW = (cw - 16.0f) / 5.0f;
                    for (size_t i = 0; i < gateKnobs.size(); ++i)
                    {
                        auto &[knob, shortLabel] = gateKnobs[i];
                        const float cx = x + colW * 0.5f + (colW + 4.0f) * (float)i;
                        g.setColour(Colours::textSecondary.withMultipliedAlpha(alpha));
                        g.setFont(sans(9.0f, Weight::semibold, 0.08f));
                        g.drawText(shortLabel, juce::Rectangle<float>(cx - colW * 0.5f, 181.0f, colW, 11.0f), juce::Justification::centred, false);

                        const float v = knob->getValue();
                        const auto &id = knob->getParameter().getParameterID();
                        const juce::String unit = id == "gateSensitivity" ? "%" : (id == "gateRange" ? "dB" : "ms");
                        const juce::String valueText = id == "gateRange" ? withMinus(juce::String(juce::roundToInt(v)))
                                                                         : (id == "gateAttack" ? juce::String(v, 1) : juce::String(juce::roundToInt(v)));
                        const auto vf = mono(10.5f), uf = mono(9.0f);
                        const float vw = vf.getStringWidthFloat(valueText), uw = uf.getStringWidthFloat(" " + unit);
                        const float sx = cx - (vw + uw) * 0.5f;
                        g.setColour(Colours::textPrimary.withMultipliedAlpha(alpha));
                        g.setFont(vf);
                        g.drawSingleLineText(valueText, juce::roundToInt(sx), 205);
                        g.setColour(Colours::textTertiary.withMultipliedAlpha(alpha));
                        g.setFont(uf);
                        g.drawSingleLineText(" " + unit, juce::roundToInt(sx + vw), 205);
                    }
                    return;
                }

                // Stereo & output page
                const bool mono = ctx.numChannels() < 2;
                const float half = (cw - 14.0f) * 0.5f;
                const float stereoAlpha = mono ? 0.5f : 1.0f;

                drawLabel(g, "Stereo", {x, 44.0f, half, 12.0f}, Colours::textSecondary.withMultipliedAlpha(stereoAlpha));
                const float ledX = x + half;
                g.setFont(mono_(9.0f));
                g.setColour(Colours::textSecondary);
                g.drawText("R", juce::Rectangle<float>(ledX - 8.0f, 44.0f, 8.0f, 12.0f), juce::Justification::centredRight, false);
                drawLed(g, {ledX - 15.0f, 50.0f}, mono ? LedState::off : LedState::on, 7.0f, false);
                g.drawText("L", juce::Rectangle<float>(ledX - 34.0f, 44.0f, 8.0f, 12.0f), juce::Justification::centredRight, false);
                drawLed(g, {ledX - 41.0f, 50.0f}, LedState::on, 7.0f, false);

                g.setFont(sans(10.5f));
                g.setColour(Colours::textSecondary.withMultipliedAlpha(stereoAlpha));
                g.drawText("Width", juce::Rectangle<float>(x, 100.0f, half, 14.0f), juce::Justification::centredLeft, false);
                g.setFont(mono_(10.5f));
                g.setColour(Colours::textPrimary.withMultipliedAlpha(stereoAlpha));
                g.drawText(juce::String(juce::roundToInt(widthFader.getValue())) + "%", juce::Rectangle<float>(x, 100.0f, half, 14.0f), juce::Justification::centredRight, false);
                if (mono)
                {
                    g.setFont(sans(10.0f));
                    g.setColour(Colours::textSecondary);
                    g.drawFittedText(utf8("Mono track \xe2\x80\x94 stereo controls unavailable."), juce::Rectangle<int>((int)x, 148, (int)half, 28), juce::Justification::topLeft, 2);
                }

                const float ox = x + half + 14.0f;
                drawLabel(g, "Output", {ox, 44.0f, half, 12.0f});
                g.setFont(mono_(10.0f));
                g.setColour(Colours::textSecondary);
                g.drawText("clips " + juce::String(ctx.processor.getOutputClipCount()), juce::Rectangle<float>(ox, 44.0f, half, 12.0f), juce::Justification::centredRight, false);

                g.setFont(sans(10.5f));
                g.drawText("Gain", juce::Rectangle<float>(ox, 64.0f, half, 14.0f), juce::Justification::centredLeft, false);
                g.setFont(mono_(10.5f));
                g.setColour(Colours::textPrimary);
                g.drawText(formatSignedDb(gainFader.getValue()) + " dB", juce::Rectangle<float>(ox, 64.0f, half, 14.0f), juce::Justification::centredRight, false);

                g.setFont(sans(11.0f));
                g.drawText("Limiter", juce::Rectangle<float>(ox + 38.0f, 120.0f, 60.0f, 16.0f), juce::Justification::centredLeft, false);
                g.setFont(mono_(10.5f));
                g.setColour(Colours::textSecondary);
                g.drawText("ceiling", juce::Rectangle<float>(ox, 120.0f, half - 84.0f, 16.0f), juce::Justification::centredRight, false);
            }

        private:
            static juce::Font mono_(float px) { return vfp::ui::mono(px); }

            EditorContext &ctx;
            TabButton gateTab{"VOICE GATE", true}, outTab{"STEREO & OUTPUT", false};
            ToggleSwitch gateSwitch{"Voice gate enabled"}, limiterSwitch{"Limiter"};
            juce::AudioProcessorValueTreeState::ButtonAttachment gateAttachment, limiterAttachment;
            std::vector<std::pair<std::unique_ptr<Knob>, juce::String>> gateKnobs;
            SegmentedControl stereoLink{true};
            Fader widthFader, gainFader;
            DragValue ceiling;
            juce::String stereoLinkTip;
            juce::Rectangle<float> stereoLinkArea, tagArea;
            int currentTab = 0;
        };

        //==============================================================================
        class AdvancedBody : public RefreshingComponent
        {
        public:
            explicit AdvancedBody(EditorContext &c)
                : ctx(c), spectrum(c.model, SpectrumView::Layout::advanced), overlay(c.apvts, &c.undo),
                  global(c), bandEditor(c, overlay), tabs(c)
            {
                spectrum.setOverlay(&overlay);
                overlay.onBandSelected = [this](int b)
                { bandEditor.select(b); };

                for (juce::Component *comp : {(juce::Component *)&inputMeter, (juce::Component *)&outputMeter, (juce::Component *)&spectrum,
                                              (juce::Component *)&global, (juce::Component *)&bandEditor, (juce::Component *)&tabs})
                    addAndMakeVisible(comp);

                inputMeter.onClipClicked = outputMeter.onClipClicked = [this]
                { ctx.processor.resetClipIndicators(); };
            }

            void refresh() override
            {
                updateMeters(ctx, inputMeter, outputMeter);
                spectrum.setState(spectrumState(ctx));
                overlay.refresh();
                global.repaint();
                bandEditor.refresh();
                tabs.refresh();
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced(14);
                inputMeter.setBounds(r.removeFromLeft(64));
                outputMeter.setBounds(r.removeFromRight(64));
                r.removeFromLeft(12);
                r.removeFromRight(12);

                spectrum.setBounds(r.removeFromTop(270));
                r.removeFromTop(12);
                global.setBounds(r.removeFromLeft(228));
                r.removeFromLeft(12);
                bandEditor.setBounds(r.removeFromLeft(336));
                r.removeFromLeft(12);
                tabs.setBounds(r);
            }

        private:
            EditorContext &ctx;
            LevelMeter inputMeter{LevelMeter::Side::input}, outputMeter{LevelMeter::Side::output};
            SpectrumView spectrum;
            BandOverlay overlay;
            GlobalPanel global;
            BandEditorPanel bandEditor;
            AdvancedTabs tabs;
        };

        //==============================================================================
        /** Compact: In / Out meters around a shared scale. */
        class CompactMeters : public juce::Component, public juce::SettableTooltipClient
        {
        public:
            explicit CompactMeters(EditorContext &c) : ctx(c)
            {
                setTooltip("Input / output level (dBFS)\nBar: RMS " + middot + " white line: peak hold.");
            }

            void paint(juce::Graphics &g) override
            {
                const auto r = getLocalBounds().toFloat();
                drawPanel(g, r);

                const int channels = ctx.numChannels();
                const int mon = juce::roundToInt(ctx.value("monitor"));
                const float inner = r.getWidth() - 16.0f;
                const float colW = (inner - 18.0f - 12.0f) * 0.5f;
                const float top = 10.0f, bottom = r.getBottom() - 10.0f;
                const float barsTop = top + 11.0f + 6.0f, barsBottom = bottom - 12.0f - 6.0f;

                auto column = [&](float cx, const juce::String &title, juce::Colour titleColour, const EditorModel::Meter &m, bool removed)
                {
                    drawLabel(g, title, {cx - colW * 0.5f, top, colW, 11.0f}, titleColour, 9.0f, 0.12f, juce::Justification::centred);
                    const float barsW = (float)channels * 7.0f + (float)(channels - 1) * 3.0f;
                    for (int ch = 0; ch < channels; ++ch)
                        LevelMeter::drawBar(g, {cx - barsW * 0.5f + (float)ch * 10.0f, barsTop, 7.0f, barsBottom - barsTop}, m.rmsDb[ch], m.holdDb[ch], removed, false);
                    g.setColour(Colours::textPrimary);
                    g.setFont(mono(10.0f));
                    g.drawText(withMinus(juce::String(juce::jmax(-99.9f, m.getPeakDb()), 1)), juce::Rectangle<float>(cx - colW * 0.5f - 4.0f, bottom - 12.0f, colW + 8.0f, 12.0f),
                               juce::Justification::centred, false);
                };

                const float inX = 8.0f + colW * 0.5f;
                const float outX = r.getWidth() - 8.0f - colW * 0.5f;
                column(inX, "In", Colours::textSecondary, ctx.model.inputMeter, false);
                column(outX, mon == 2 ? utf8("Out \xce\x94") : "Out", mon == 2 ? Colours::amber : Colours::textSecondary, ctx.model.outputMeter, mon == 2);

                // Shared scale
                const float scaleTop = barsTop, scaleH = barsBottom - barsTop;
                g.setFont(mono(8.5f));
                g.setColour(Colours::textTertiary);
                for (int db : {0, -12, -24, -36, -60})
                {
                    const float y = scaleTop + scaleH * (float)-db / 60.0f;
                    g.drawText(juce::String(-db), juce::Rectangle<float>(r.getCentreX() - 9.0f, y - 6.0f, 18.0f, 12.0f), juce::Justification::centred, false);
                }
            }

        private:
            EditorContext &ctx;
        };

        class CompactBody : public RefreshingComponent
        {
        public:
            explicit CompactBody(EditorContext &c)
                : ctx(c), amount(c, Knob::Style::heroCompact), spectrum(c.model, SpectrumView::Layout::compact), meters(c)
            {
                addAndMakeVisible(amount);
                addAndMakeVisible(spectrum);
                addAndMakeVisible(meters);
            }

            void refresh() override
            {
                spectrum.setState(spectrumState(ctx));
                amount.repaint();
                meters.repaint();
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced(12);
                amount.setBounds(r.removeFromLeft(292));
                meters.setBounds(r.removeFromRight(104));
                r.removeFromLeft(10);
                r.removeFromRight(10);
                spectrum.setBounds(r);
            }

        private:
            EditorContext &ctx;
            AmountPanel amount;
            SpectrumView spectrum;
            CompactMeters meters;
        };
    }

    std::unique_ptr<RefreshingComponent> createBody(EditorContext &ctx, ViewMode mode)
    {
        switch (mode)
        {
        case ViewMode::advanced:
            return std::make_unique<AdvancedBody>(ctx);
        case ViewMode::compact:
            return std::make_unique<CompactBody>(ctx);
        case ViewMode::simple:
            break;
        }
        return std::make_unique<SimpleBody>(ctx);
    }

    //==============================================================================
    // PresetMenu
    //==============================================================================
    class PresetMenu::Row : public juce::Button
    {
    public:
        Row(const PresetInfo &p, bool isSelected, bool isCompact)
            : juce::Button(p.name), preset(p), selected(isSelected), compact(isCompact)
        {
            setTitle(p.name);
        }

        void paintButton(juce::Graphics &g, bool highlighted, bool) override
        {
            const auto r = getLocalBounds().toFloat();
            if (highlighted || selected)
            {
                g.setColour(highlighted ? Colours::selected : juce::Colour(0xff1c2a35));
                g.fillRoundedRectangle(r, 5.0f);
            }

            if (selected)
                drawIcon(g, Icon::check, {10.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f}, Colours::teal);

            g.setColour(selected ? Colours::textPrimary : Colours::textBody);
            g.setFont(sans(compact ? 11.5f : 12.0f));
            g.drawText(preset.name, r.withTrimmedLeft(10.0f + (compact ? 12.0f : 14.0f) + 8.0f).withTrimmedRight(50.0f), juce::Justification::centredLeft, true);

            if (compact)
            {
                const juce::String kind = preset.factory ? "Factory" : "User";
                const float tw = tagWidth(kind);
                drawTag(g, kind, {r.getRight() - 10.0f - tw, r.getCentreY() - 7.5f, tw, 15.0f}, TagStyle::plain);
            }
            else
            {
                g.setColour(Colours::textTertiary);
                g.setFont(mono(10.5f));
                g.drawText(juce::String(juce::roundToInt(preset.amount)) + "%", r.withTrimmedRight(10.0f), juce::Justification::centredRight, false);
            }
        }

        PresetInfo preset;
        bool selected, compact;
    };

    PresetMenu::PresetMenu(EditorContext &c, bool isCompact, std::function<void()> onClose)
        : ctx(c), compact(isCompact), close(std::move(onClose))
    {
        auto &pm = ctx.processor.getPresetManager();
        pm.refreshUserPresets();
        const auto &presets = pm.getPresets();
        const int current = pm.getCurrentIndex();

        float y = 0.0f;
        auto addRow = [&](int index)
        {
            auto row = std::make_unique<Row>(presets[(size_t)index], index == current, compact);
            row->onClick = [this, index]
            {
                ctx.undo.beginNewTransaction();
                ctx.processor.getPresetManager().loadPreset(index);
                close();
            };
            row->setBounds(0, juce::roundToInt(y), 10, 28);
            list.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            y += 29.0f;
        };

        if (compact)
        {
            for (int i = 0; i < (int)presets.size(); ++i)
                addRow(i);
        }
        else
        {
            sectionLabels.push_back({y, "Factory"});
            y += 22.0f;
            for (int i = 0; i < pm.getNumFactoryPresets(); ++i)
                addRow(i);

            dividers.push_back(y + 4.0f);
            y += 11.0f;
            sectionLabels.push_back({y, "User"});
            y += 20.0f;
            if ((int)presets.size() == pm.getNumFactoryPresets())
            {
                sectionLabels.push_back({y, utf8("No user presets yet \xe2\x80\x94 use Save As\xe2\x80\xa6")});
                y += 26.0f;
            }
            for (int i = pm.getNumFactoryPresets(); i < (int)presets.size(); ++i)
                addRow(i);
        }
        listHeight = y;

        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(6);
        addAndMakeVisible(viewport);

        if (!compact)
        {
            saveAsChip.setCentred(true);
            resetChip.setCentred(true);
            saveAsChip.onClick = [this]
            {
                close();
                ctx.savePresetAs();
            };
            resetChip.onClick = [this]
            {
                ctx.undo.beginNewTransaction();
                ctx.processor.getPresetManager().resetToPreset();
                close();
            };
            addAndMakeVisible(saveAsChip);
            addAndMakeVisible(resetChip);
        }

        setWantsKeyboardFocus(true);
    }

    int PresetMenu::getPreferredHeight() const
    {
        // 16 px shadow margin each side + 6 px padding
        const float footer = compact ? 0.0f : 11.0f + 32.0f + 44.0f;
        return juce::roundToInt(32.0f + 12.0f + listHeight + footer);
    }

    void PresetMenu::resized()
    {
        const auto panel = getLocalBounds().reduced(16);
        auto inner = panel.reduced(6);

        if (!compact)
        {
            auto footer = inner.removeFromBottom(44 + 32 + 11);
            footer.removeFromTop(11);
            auto chips = footer.removeFromTop(32).reduced(4, 2);
            saveAsChip.setBounds(chips.removeFromLeft((chips.getWidth() - 6) / 2));
            chips.removeFromLeft(6);
            resetChip.setBounds(chips);
        }

        viewport.setBounds(inner);
        const int listW = inner.getWidth() - (juce::roundToInt(listHeight) > inner.getHeight() ? 8 : 0);
        list.setSize(listW, juce::roundToInt(listHeight));
        for (auto &row : rows)
            row->setSize(listW, 28);
    }

    void PresetMenu::paint(juce::Graphics &g)
    {
        const auto panel = getLocalBounds().reduced(16).toFloat();

        juce::Path shape;
        shape.addRoundedRectangle(panel, 9.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 24, {0, 12}).drawForPath(g, shape);

        g.setColour(Colours::popup);
        g.fillRoundedRectangle(panel, 9.0f);
        g.setColour(Colours::popupBorder);
        g.drawRoundedRectangle(panel.reduced(0.5f), 9.0f, 1.0f);

        const auto origin = viewport.getPosition().toFloat() - juce::Point<float>(0.0f, (float)viewport.getViewPositionY());
        for (const auto &[y, text] : sectionLabels)
        {
            const bool hint = text.startsWith("No user");
            const auto area = juce::Rectangle<float>(origin.x + 10.0f, origin.y + y + (hint ? 0.0f : 6.0f), panel.getWidth() - 32.0f, hint ? 26.0f : 14.0f);
            if (!viewport.getBounds().toFloat().intersects(area))
                continue;
            if (hint)
            {
                g.setFont(sans(11.0f));
                g.setColour(Colours::textTertiary);
                g.drawText(text, area, juce::Justification::centredLeft, false);
            }
            else
            {
                drawLabel(g, text, area, Colours::textSecondary, 9.5f);
            }
        }
        for (float y : dividers)
        {
            g.setColour(Colours::wellBorder);
            g.fillRect(juce::Rectangle<float>(origin.x + 4.0f, origin.y + y, panel.getWidth() - 20.0f, 1.0f));
        }

        if (!compact)
        {
            const float footerTop = (float)saveAsChip.getY() - 2.0f - 6.0f;
            g.setColour(Colours::wellBorder);
            g.fillRect(juce::Rectangle<float>(panel.getX() + 10.0f, footerTop, panel.getWidth() - 20.0f, 1.0f));

            g.setFont(sans(10.5f));
            g.setColour(Colours::textTertiary);
            g.drawFittedText(utf8("Presets store parameter values only \xe2\x80\x94 all use the same RNNoise model."),
                             juce::Rectangle<int>((int)panel.getX() + 14, saveAsChip.getBottom() + 6, (int)panel.getWidth() - 28, 34),
                             juce::Justification::topLeft, 2, 1.0f);
        }
    }

    //==============================================================================
    // Dialog
    //==============================================================================
    Dialog::Dialog(const juce::String &t, const juce::String &b, bool withTextField, const juce::String &confirmLabel,
                   std::function<void(const juce::String &)> confirmed, std::function<void()> dismissed)
        : title(t), body(b), confirmChip(confirmLabel, Chip::Variant::teal), onConfirm(std::move(confirmed)), onDismiss(std::move(dismissed))
    {
        if (withTextField)
        {
            field = std::make_unique<juce::TextEditor>("Preset name");
            field->setFont(sans(12.5f));
            field->setTextToShowWhenEmpty("Preset name", Colours::textHint);
            field->setColour(juce::TextEditor::backgroundColourId, Colours::well);
            field->setColour(juce::TextEditor::outlineColourId, Colours::wellBorder);
            field->setColour(juce::TextEditor::focusedOutlineColourId, Colours::teal);
            field->setColour(juce::TextEditor::textColourId, Colours::textPrimary);
            field->setColour(juce::TextEditor::highlightColourId, Colours::teal.withAlpha(0.3f));
            field->setColour(juce::CaretComponent::caretColourId, Colours::teal);
            field->setIndents(10, 7);
            field->onReturnKey = [this]
            { confirm(); };
            field->onEscapeKey = [this]
            { onDismiss(); };
            addAndMakeVisible(*field);

            confirmChip.setToggleState(true, juce::dontSendNotification);
            cancelChip.onClick = [this]
            { onDismiss(); };
            cancelChip.setCentred(true);
            addAndMakeVisible(cancelChip);
        }

        confirmChip.setCentred(true);
        confirmChip.onClick = [this]
        { confirm(); };
        addAndMakeVisible(confirmChip);
        setWantsKeyboardFocus(true);
    }

    void Dialog::parentHierarchyChanged()
    {
        if (isShowing())
        {
            if (field != nullptr)
                field->grabKeyboardFocus();
            else
                grabKeyboardFocus();
        }
    }

    void Dialog::confirm()
    {
        onConfirm(field != nullptr ? field->getText() : juce::String());
    }

    juce::Rectangle<float> Dialog::panelBounds() const
    {
        juce::AttributedString text;
        text.append(body, sans(11.5f), Colours::textBody);
        juce::TextLayout layout;
        layout.createLayout(text, 300.0f);
        const float h = 20.0f + 18.0f + 10.0f + layout.getHeight() + (field != nullptr ? 14.0f + 32.0f : 0.0f) + 18.0f + 28.0f + 20.0f;
        return juce::Rectangle<float>(340.0f, h).withCentre(getLocalBounds().toFloat().getCentre());
    }

    void Dialog::resized()
    {
        const auto panel = panelBounds();
        auto inner = panel.reduced(20.0f);
        auto buttons = inner.removeFromBottom(28.0f);
        confirmChip.setBounds(buttons.removeFromRight(juce::jmax(80.0f, confirmChip.getPreferredWidth())).toNearestInt());
        buttons.removeFromRight(8.0f);
        cancelChip.setBounds(buttons.removeFromRight(80.0f).toNearestInt());

        if (field != nullptr)
            field->setBounds(inner.removeFromBottom(18.0f + 32.0f).removeFromTop(32.0f).toNearestInt());
    }

    void Dialog::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colours::black.withAlpha(0.5f));

        const auto panel = panelBounds();
        juce::Path shape;
        shape.addRoundedRectangle(panel, 10.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 30, {0, 14}).drawForPath(g, shape);
        g.setColour(Colours::popup);
        g.fillPath(shape);
        g.setColour(Colours::popupBorder);
        g.strokePath(shape, juce::PathStrokeType(1.0f));

        auto inner = panel.reduced(20.0f);
        g.setColour(Colours::textPrimary);
        g.setFont(sans(13.0f, Weight::semibold));
        g.drawText(title, inner.removeFromTop(18.0f), juce::Justification::centredLeft, false);
        inner.removeFromTop(10.0f);

        juce::AttributedString text;
        text.append(body, sans(11.5f), Colours::textBody);
        text.setLineSpacing(3.0f);
        juce::TextLayout layout;
        layout.createLayout(text, inner.getWidth());
        layout.draw(g, inner.withHeight(layout.getHeight() + 4.0f));
    }

    void Dialog::mouseDown(const juce::MouseEvent &e)
    {
        if (!panelBounds().contains(e.position))
            onDismiss();
    }

    bool Dialog::keyPressed(const juce::KeyPress &key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            onDismiss();
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            confirm();
            return true;
        }
        return false;
    }
}
