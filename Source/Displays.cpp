#include "Displays.h"

namespace vfp::ui
{
    namespace
    {
        const float log20 = std::log10(20.0f);
        const float logRange = std::log10(20000.0f) - std::log10(20.0f);

        float gainToDb(float gain) { return juce::Decibels::gainToDecibels(gain, -100.0f); }
    }

    float frequencyToProportion(float hz)
    {
        return (std::log10(juce::jmax(1.0f, hz)) - log20) / logRange;
    }

    float proportionToFrequency(float proportion)
    {
        return std::pow(10.0f, log20 + proportion * logRange);
    }

    //==============================================================================
    // EditorModel
    //==============================================================================
    EditorModel::EditorModel(VoiceFrameProProcessor &p) : processor(p)
    {
        for (auto &r : rings)
            r.assign((size_t)fftSize, 0.0f);
        pullIn.resize(AnalyserFifo::capacity);
        pullOut.resize(AnalyserFifo::capacity);
        pullRem.resize(AnalyserFifo::capacity);
        fftData.resize((size_t)fftSize * 2);

        for (auto &s : spectrum)
            s.fill(-120.0f);

        // 1/6-octave smoothing windows per display point
        const float binHz = (float)(DenoiseEngine::engineRate / fftSize);
        for (int i = 0; i < numPoints; ++i)
        {
            const float f = proportionToFrequency((float)i / (float)(numPoints - 1));
            const int lo = (int)std::ceil(f * std::pow(2.0f, -1.0f / 12.0f) / binHz);
            const int hi = (int)std::floor(f * std::pow(2.0f, 1.0f / 12.0f) / binHz);
            binRanges[(size_t)i] = {juce::jlimit(1, fftSize / 2 - 1, lo), juce::jlimit(1, fftSize / 2 - 1, hi)};
            pointBins[(size_t)i] = f / binHz;
        }
    }

    void EditorModel::Meter::reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            rmsDb[ch] = holdDb[ch] = -100.0f;
            holdTime[ch] = 0.0;
        }
    }

    void EditorModel::updateMeter(Meter &m, const float *rms, const float *peak, double now)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float r = gainToDb(rms[ch]);
            m.rmsDb[ch] = r > m.rmsDb[ch] ? r : juce::jmax(r, m.rmsDb[ch] - 0.8f); // ~24 dB/s fall

            const float p = gainToDb(peak[ch]);
            if (p >= m.holdDb[ch])
            {
                m.holdDb[ch] = p;
                m.holdTime[ch] = now;
            }
            else if (now - m.holdTime[ch] > 1.5)
            {
                m.holdDb[ch] = juce::jmax(p, m.holdDb[ch] - 0.5f);
            }
        }
    }

    void EditorModel::update(double now)
    {
        const float inRms[2] = {processor.getInputRms(0), processor.getInputRms(1)};
        const float inPeak[2] = {processor.getInputPeak(0), processor.getInputPeak(1)};
        const float outRms[2] = {processor.getOutputRms(0), processor.getOutputRms(1)};
        const float outPeak[2] = {processor.getOutputPeak(0), processor.getOutputPeak(1)};
        updateMeter(inputMeter, inRms, inPeak, now);
        updateMeter(outputMeter, outRms, outPeak, now);

        auto &engine = processor.getEngine();
        voiceProbability = engine.takeVoiceProbabilityPeak();
        gainReductionDb = engine.getGainReductionDb();
        gateGain = engine.getGateGain();

        vadHistory[(size_t)historyPos] = voiceProbability;
        gateHistory[(size_t)historyPos] = gateGain;
        historyPos = (historyPos + 1) % historySize;

        updateSpectrum();
    }

    void EditorModel::updateSpectrum()
    {
        auto &fifo = processor.getEngine().getAnalyserFifo();
        const int n = fifo.pull(pullIn.data(), pullOut.data(), pullRem.data(), (int)pullIn.size());

        const float *sources[3] = {pullIn.data(), pullOut.data(), pullRem.data()};
        for (int i = 0; i < n; ++i)
        {
            for (size_t c = 0; c < 3; ++c)
                rings[c][(size_t)ringPos] = sources[c][i];
            ringPos = (ringPos + 1) % fftSize;
        }
        newSamples += n;

        const bool hasSignal = newSamples >= fftSize / 8;
        if (hasSignal)
            newSamples = 0;

        // amplitude of a full-scale sine reads 0 dB: |X| · 4 / N for a Hann window
        const float norm = 4.0f / (float)fftSize;

        for (size_t c = 0; c < 3; ++c)
        {
            std::array<float, numPoints> target;

            if (hasSignal)
            {
                for (int i = 0; i < fftSize; ++i)
                    fftData[(size_t)i] = rings[c][(size_t)((ringPos + i) % fftSize)];
                std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);
                window.multiplyWithWindowingTable(fftData.data(), (size_t)fftSize);
                fft.performFrequencyOnlyForwardTransform(fftData.data());

                for (int p = 0; p < numPoints; ++p)
                {
                    const auto [lo, hi] = binRanges[(size_t)p];
                    float power = 0.0f;
                    if (hi >= lo)
                    {
                        for (int b = lo; b <= hi; ++b)
                            power += juce::square(fftData[(size_t)b] * norm);
                        power /= (float)(hi - lo + 1);
                    }
                    else
                    {
                        const float pos = juce::jlimit(1.0f, (float)(fftSize / 2 - 2), pointBins[(size_t)p]);
                        const int b = (int)pos;
                        const float frac = pos - (float)b;
                        power = juce::square((fftData[(size_t)b] * (1.0f - frac) + fftData[(size_t)b + 1] * frac) * norm);
                    }
                    target[(size_t)p] = 10.0f * std::log10(power + 1.0e-14f);
                }
            }
            else
            {
                target.fill(-140.0f); // no audio flowing: let the curves fall away
            }

            auto &s = spectrum[c];
            for (int p = 0; p < numPoints; ++p)
            {
                const float t = target[(size_t)p];
                s[(size_t)p] += (t - s[(size_t)p]) * (t > s[(size_t)p] ? 0.6f : (hasSignal ? 0.22f : 0.08f));
            }
        }
    }

    //==============================================================================
    // LevelMeter
    //==============================================================================
    LevelMeter::LevelMeter(Side s) : side(s)
    {
        setTooltip(side == Side::input
                       ? utf8("Input level (dBFS)\nBar: RMS \xc2\xb7 white line: peak hold. Measured before processing.")
                       : "Output level (dBFS)\nAfter output gain and limiter. In Removed monitoring this shows the difference signal.");
    }

    void LevelMeter::setState(const EditorModel::Meter &meter, int numChannels, bool lit,
                              const juce::String &newTitle, juce::Colour newTitleColour, bool removed)
    {
        state = meter;
        channels = numChannels;
        clipLit = lit;
        title = newTitle;
        titleColour = newTitleColour;
        removedGradient = removed;
        repaint();
    }

    juce::Rectangle<float> LevelMeter::clipBadgeBounds() const
    {
        return juce::Rectangle<float>(0.0f, 30.0f, 30.0f, 14.0f).withCentre({(float)getWidth() * 0.5f, 37.0f});
    }

    void LevelMeter::mouseUp(const juce::MouseEvent &e)
    {
        if (clipBadgeBounds().expanded(4.0f).contains(e.position) && onClipClicked)
            onClipClicked();
    }

    void LevelMeter::drawBar(juce::Graphics &g, juce::Rectangle<float> bar, float levelDb, float peakDb, bool removed, bool stripes)
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bar, 2.0f);
        g.reduceClipRegion(clip);

        juce::ColourGradient grad(removed ? juce::Colour(0xff6e5534) : juce::Colour(0xff2a7772), 0.0f, bar.getBottom(),
                                  Colours::red, 0.0f, bar.getY(), false);
        if (removed)
        {
            grad.addColour(0.80, Colours::amber);
        }
        else
        {
            grad.addColour(0.70, Colours::teal);
            grad.addColour(0.795, juce::Colour(0xff7fe3d9));
            grad.addColour(0.805, Colours::amber);
            grad.addColour(0.945, Colours::amber);
        }
        grad.addColour(0.955, Colours::red);
        g.setGradientFill(grad);
        g.fillRect(bar);

        const float level = dbToProportion(levelDb);
        g.setColour(Colours::well);
        g.fillRect(bar.withHeight(bar.getHeight() * (1.0f - level)));

        if (stripes)
        {
            g.setColour(juce::Colour(0xff0d1218).withAlpha(0.6f));
            for (float y = bar.getY() + 3.0f; y < bar.getBottom(); y += 4.0f)
                g.fillRect(bar.getX(), y, bar.getWidth(), 1.0f);
        }

        if (peakDb > -60.0f)
        {
            g.setColour(Colours::textPrimary);
            g.fillRect(bar.getX(), bar.getY() + bar.getHeight() * (1.0f - dbToProportion(peakDb)), bar.getWidth(), 2.0f);
        }
    }

    void LevelMeter::paint(juce::Graphics &g)
    {
        const auto r = getLocalBounds().toFloat();
        drawPanel(g, r);

        // Title
        drawLabel(g, title, {0.0f, 10.0f, r.getWidth(), 12.0f}, titleColour, 10.0f, 0.12f, juce::Justification::centred);

        // CLIP badge
        const auto badge = clipBadgeBounds();
        g.setColour(clipLit ? juce::Colour(0xff3a1e22) : Colours::tile);
        g.fillRoundedRectangle(badge, 3.0f);
        g.setColour(clipLit ? Colours::red.withAlpha(0.7f) : Colours::panelBorder);
        g.drawRoundedRectangle(badge.reduced(0.5f), 3.0f, 1.0f);
        g.setColour(clipLit ? Colours::red : Colours::textDisabled);
        g.setFont(mono(9.0f, Weight::semibold, 0.08f));
        g.drawText("CLIP", badge, juce::Justification::centred, false);

        // Footer: peak readout
        const float footerTop = r.getBottom() - 10.0f - 11.0f - 8.0f - 13.0f;
        g.setColour(Colours::textPrimary);
        g.setFont(mono(11.0f));
        g.drawText(withMinus(juce::String(juce::jmax(-99.9f, state.getPeakDb()), 1)), juce::Rectangle<float>(0.0f, footerTop, r.getWidth(), 13.0f),
                   juce::Justification::centred, false);
        g.setColour(Colours::textTertiary);
        g.setFont(sans(9.0f));
        g.drawText("dBFS pk", juce::Rectangle<float>(0.0f, footerTop + 21.0f, r.getWidth(), 11.0f), juce::Justification::centred, false);

        // Bars + scale
        const float areaTop = badge.getBottom() + 8.0f;
        const float areaBottom = footerTop - 8.0f;
        const float barBottom = areaBottom - 13.0f; // gap 4 + channel label
        const float barH = juce::jmax(10.0f, barBottom - areaTop);

        const float content = (float)channels * 14.0f + 20.0f;
        float x = side == Side::input ? 6.0f + (r.getWidth() - 6.0f - content) * 0.5f
                                      : (r.getWidth() - 6.0f - content) * 0.5f;

        auto drawScale = [&](float sx, bool alignLeft)
        {
            g.setFont(mono(9.0f));
            g.setColour(Colours::textTertiary);
            for (int db : {0, -6, -12, -18, -24, -36, -48, -60})
            {
                const float y = areaTop + barH * (float)-db / 60.0f;
                g.drawText(juce::String(-db), juce::Rectangle<float>(sx + 2.0f, y - 6.0f, 18.0f, 12.0f),
                           alignLeft ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
            }
        };

        if (side == Side::output)
        {
            drawScale(x - 4.0f, false);
            x += 25.0f;
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto bar = juce::Rectangle<float>(x, areaTop, 9.0f, barH);
            drawBar(g, bar, state.rmsDb[ch], state.holdDb[ch], removedGradient, true);

            g.setColour(Colours::textTertiary);
            g.setFont(sans(9.0f, Weight::semibold));
            g.drawText(channels == 1 ? "M" : (ch == 0 ? "L" : "R"), juce::Rectangle<float>(x - 4.0f, barBottom + 4.0f, 17.0f, 10.0f),
                       juce::Justification::centred, false);
            x += 14.0f;
        }

        if (side == Side::input)
            drawScale(x - 5.0f + 5.0f, true);
    }

    //==============================================================================
    // SpectrumView
    //==============================================================================
    SpectrumView::SpectrumView(EditorModel &m, Layout l) : model(m), layout(l)
    {
        if (layout == Layout::advanced)
        {
            dbTop = -6.0f;
            dbBottom = -102.0f;
        }

        if (layout != Layout::compact)
        {
            legendInput.setTooltip("Input\nUnprocessed signal entering the plugin.");
            legendOutput.setTooltip("Output\nSignal after denoising, before output gain.");
            legendRemoved.setTooltip(utf8("Removed \xe2\x80\x94 difference signal\nInput minus output. Mostly noise, but can include speech that was attenuated. "
                                          "It is not a clean ambience stem. Click to show/hide."));
            legendInput.setClickingTogglesState(false);
            legendOutput.setClickingTogglesState(false);
            legendRemoved.setClickingTogglesState(layout == Layout::simple);
            legendRemoved.onClick = [this]
            { repaint(); };

            for (auto *b : {&legendInput, &legendOutput, &legendRemoved})
                addAndMakeVisible(*b);
        }
    }

    void SpectrumView::setState(const State &s)
    {
        state = s;
        repaint();
    }

    void SpectrumView::setOverlay(juce::Component *c)
    {
        overlay = c;
        if (overlay != nullptr)
            addAndMakeVisible(*overlay);
        resized();
    }

    juce::Rectangle<float> SpectrumView::getPlotBounds() const
    {
        const float w = (float)getWidth(), h = (float)getHeight();
        switch (layout)
        {
        case Layout::simple:
            return {36.0f, 34.0f, w - 62.0f, 204.0f};
        case Layout::advanced:
            return {36.0f, 34.0f, w - 62.0f, 158.0f};
        case Layout::compact:
            break;
        }
        return {10.0f, 30.0f, w - 20.0f, h - 52.0f};
    }

    void SpectrumView::resized()
    {
        if (layout != Layout::compact)
        {
            float x = 14.0f + labelFont().getStringWidthFloat(layout == Layout::advanced ? utf8("SPECTRUM \xc2\xb7 6 BANDS") : juce::String("SPECTRUM")) + 12.0f;
            for (auto *b : {&legendInput, &legendOutput, &legendRemoved})
            {
                const float w = b->getPreferredWidth();
                b->setBounds(juce::Rectangle<float>(x, 8.0f, w, 22.0f).toNearestInt());
                x += w + 4.0f;
            }
        }

        if (overlay != nullptr)
        {
            const auto plot = getPlotBounds();
            overlay->setBounds(juce::Rectangle<float>(plot.getX(), plot.getY(), plot.getWidth(), BandOverlay::overlayHeight).toNearestInt());
        }
    }

    juce::Path SpectrumView::makeLine(const std::array<float, EditorModel::numPoints> &data, juce::Rectangle<float> plot) const
    {
        juce::Path p;
        for (int i = 0; i < EditorModel::numPoints; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float)i / (float)(EditorModel::numPoints - 1);
            const float v = juce::jlimit(0.0f, 1.0f, (dbTop - data[(size_t)i]) / (dbTop - dbBottom));
            const float y = plot.getY() + v * plot.getHeight();
            if (i == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }
        return p;
    }

    void SpectrumView::paintCurves(juce::Graphics &g, juce::Rectangle<float> plot)
    {
        const int mon = state.monitor;
        const bool deltaOn = state.deltaBand >= 0;

        float inOp = mon == 0 ? 1.0f : (mon == 2 ? 0.45f : 0.8f);
        float outOp = mon == 2 ? 0.35f : (mon == 0 ? 0.45f : 1.0f);
        float remOp = mon == 2 ? 1.0f : 0.7f;
        bool showRemoved = !state.bypassed && legendRemoved.getToggleState();

        if (layout == Layout::advanced)
        {
            inOp = mon == 0 ? 1.0f : (mon == 2 ? 0.45f : 0.75f);
            remOp = (mon == 2 || deltaOn) ? 1.0f : 0.6f;
            showRemoved = !state.bypassed;
        }
        else if (layout == Layout::compact)
        {
            showRemoved = !state.bypassed && mon == 2;
            remOp = 1.0f;
        }

        const bool solidRemoved = mon == 2 || deltaOn || layout == Layout::compact;

        auto fillUnder = [&](const juce::Path &line, juce::ColourGradient grad, float alpha)
        {
            juce::Path fill(line);
            fill.lineTo(plot.getRight(), plot.getBottom());
            fill.lineTo(plot.getX(), plot.getBottom());
            fill.closeSubPath();
            grad.multiplyOpacity(alpha);
            g.setGradientFill(grad);
            g.fillPath(fill);
        };

        const float top = plot.getY(), bottom = plot.getBottom();
        const juce::PathStrokeType thin(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // Input
        const auto inLine = makeLine(model.getSpectrum(EditorModel::input), plot);
        fillUnder(inLine, juce::ColourGradient(Colours::inputCurve.withAlpha(0.22f), 0.0f, top, Colours::inputCurve.withAlpha(0.02f), 0.0f, bottom, false), inOp);
        g.setColour(Colours::inputCurve.withAlpha(inOp));
        g.strokePath(inLine, thin);

        // Removed
        if (showRemoved)
        {
            const auto remLine = makeLine(model.getSpectrum(EditorModel::removed), plot);
            if (layout != Layout::compact)
                fillUnder(remLine, juce::ColourGradient(Colours::amber.withAlpha(0.28f), 0.0f, top, Colours::amber.withAlpha(0.0f), 0.0f, bottom, false), remOp);
            g.setColour(Colours::amber.withAlpha(remOp));
            if (solidRemoved)
            {
                g.strokePath(remLine, thin);
            }
            else
            {
                juce::Path dashed;
                const float dashes[] = {3.0f, 2.0f};
                thin.createDashedStroke(dashed, remLine, dashes, 2);
                g.fillPath(dashed);
            }
        }

        // Output (with soft glow)
        const auto outLine = makeLine(model.getSpectrum(EditorModel::output), plot);
        juce::ColourGradient outGrad(Colours::teal.withAlpha(0.38f), 0.0f, top, Colours::teal.withAlpha(0.0f), 0.0f, bottom, false);
        outGrad.addColour(0.7, Colours::teal.withAlpha(0.06f));
        fillUnder(outLine, outGrad, outOp);
        if (layout == Layout::simple)
        {
            g.setColour(Colours::teal.withAlpha(0.08f * outOp));
            g.strokePath(outLine, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(Colours::teal.withAlpha(0.12f * outOp));
            g.strokePath(outLine, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(Colours::teal.withAlpha(outOp));
        g.strokePath(outLine, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void SpectrumView::paint(juce::Graphics &g)
    {
        const auto r = getLocalBounds().toFloat();
        drawPlotPanel(g, r);

        const auto plot = getPlotBounds();
        const auto dbToY = [&](float db)
        { return plot.getY() + juce::jlimit(0.0f, 1.0f, (dbTop - db) / (dbTop - dbBottom)) * plot.getHeight(); };
        const auto fToX = [&](float f)
        { return plot.getX() + frequencyToProportion(f) * plot.getWidth(); };

        // Grid
        const float dbStep = layout == Layout::simple ? 12.0f : 18.0f;
        g.setColour(layout == Layout::compact ? juce::Colour(0xff202b37) : Colours::gridMinor);
        if (layout != Layout::compact)
            for (float f : {30.0f, 40.0f, 60.0f, 70.0f, 80.0f, 90.0f, 300.0f, 400.0f, 600.0f, 700.0f, 800.0f, 900.0f,
                            3000.0f, 4000.0f, 6000.0f, 7000.0f, 8000.0f, 9000.0f})
                g.fillRect(juce::Rectangle<float>(std::round(fToX(f)), plot.getY(), 1.0f, plot.getHeight()));

        if (layout != Layout::compact)
            g.setColour(Colours::gridMajor);
        for (float f : {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f})
            g.fillRect(juce::Rectangle<float>(std::round(fToX(f)), plot.getY(), 1.0f, plot.getHeight()));
        for (float db = -dbStep; db >= dbBottom + 1.0f; db -= dbStep)
            g.fillRect(juce::Rectangle<float>(plot.getX(), std::round(dbToY(db)), plot.getWidth(), 1.0f));

        // Axis labels
        g.setFont(mono(9.5f));
        if (layout != Layout::compact)
        {
            g.setColour(Colours::textTertiary);
            for (float db = -dbStep; db >= dbBottom + 1.0f; db -= dbStep)
                g.drawText(withMinus(juce::String((int)db)), juce::Rectangle<float>(0.0f, dbToY(db) - 6.0f, 32.0f, 12.0f),
                           juce::Justification::centredRight, false);

            if (layout == Layout::simple)
            {
                g.setColour(Colours::textAxis);
                g.setFont(mono(9.0f));
                g.drawText("dB", juce::Rectangle<float>(8.0f, 24.0f, 20.0f, 10.0f), juce::Justification::centredLeft, false);
                g.drawText("Hz", juce::Rectangle<float>(plot.getRight() - 20.0f, plot.getBottom() + 3.0f - 14.0f + 3.0f, 26.0f, 10.0f),
                           juce::Justification::centredRight, false);
            }
            else
            {
                g.setColour(Colours::textTertiary);
                g.setFont(mono(8.5f));
                g.drawFittedText("BAND\nAMT", juce::Rectangle<int>(4, 214, 30, 24), juce::Justification::centredRight, 2, 1.0f);
            }
        }

        const float freqY = layout == Layout::simple ? 241.0f : (layout == Layout::advanced ? 197.0f : r.getBottom() - 18.0f);
        g.setFont(mono(9.5f));
        g.setColour(layout == Layout::advanced ? Colours::textHint : Colours::textTertiary);
        const std::vector<std::pair<float, const char *>> freqLabels = layout == Layout::compact
                                                                           ? std::vector<std::pair<float, const char *>>{{100.0f, "100"}, {500.0f, "500"}, {2000.0f, "2k"}, {10000.0f, "10k"}}
                                                                           : std::vector<std::pair<float, const char *>>{{50.0f, "50"}, {100.0f, "100"}, {200.0f, "200"}, {500.0f, "500"}, {1000.0f, "1k"}, {2000.0f, "2k"}, {5000.0f, "5k"}, {10000.0f, "10k"}};
        for (const auto &[f, text] : freqLabels)
            g.drawText(text, juce::Rectangle<float>(fToX(f) - 20.0f, freqY, 40.0f, 12.0f), juce::Justification::centred, false);

        // Curves
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(plot.toNearestInt());
            paintCurves(g, plot);
        }

        // Header
        const juce::String title = layout == Layout::advanced ? juce::String::fromUTF8("Spectrum \xc2\xb7 6 Bands") : juce::String("Spectrum");
        drawLabel(g, title, {layout == Layout::compact ? 12.0f : 14.0f, 8.0f, 200.0f, 22.0f});

        float right = r.getRight() - (layout == Layout::compact ? 10.0f : 12.0f);
        const juce::String liveText = layout == Layout::simple ? juce::String::fromUTF8("LIVE \xc2\xb7 1/6 OCT") : juce::String("LIVE");
        g.setFont(mono(layout == Layout::compact ? 9.0f : 9.5f, Weight::regular, 0.04f));
        const float liveW = g.getCurrentFont().getStringWidthFloat(liveText) + 2.0f;
        g.setColour(Colours::textTertiary);
        g.drawText(liveText, juce::Rectangle<float>(right - liveW, 8.0f, liveW, 22.0f), juce::Justification::centredRight, false);
        right -= liveW + 12.0f;

        if (layout == Layout::compact)
        {
            float x = 12.0f + labelFont().getStringWidthFloat("SPECTRUM") + 10.0f;
            for (auto [label, colour] : {std::pair<const char *, juce::Colour>{"In", Colours::inputCurve}, {"Out", Colours::teal}})
            {
                g.setColour(colour);
                g.fillRoundedRectangle(x, 17.5f, 12.0f, 3.0f, 1.5f);
                g.setColour(Colours::textSecondary);
                g.setFont(sans(10.5f));
                g.drawText(label, juce::Rectangle<float>(x + 17.0f, 8.0f, 30.0f, 22.0f), juce::Justification::centredLeft, false);
                x += 17.0f + sans(10.5f).getStringWidthFloat(label) + 10.0f;
            }
        }

        // Monitoring banner
        const bool deltaOn = state.deltaBand >= 0 && layout == Layout::advanced;
        if (state.monitor != 1 || deltaOn)
        {
            const bool amberBanner = state.monitor == 2 || deltaOn;
            const juce::String text = deltaOn ? "MONITORING BAND " + juce::String(state.deltaBand + 1) + " REMOVED"
                                              : (state.monitor == 2 ? (layout == Layout::compact ? "REMOVED" : "MONITORING REMOVED SIGNAL")
                                                                    : (layout == Layout::compact ? "ORIGINAL" : "MONITORING ORIGINAL"));
            const auto colour = amberBanner ? Colours::amber : Colours::inputCurve;
            const auto font = sans(layout == Layout::compact ? 9.5f : 10.0f, Weight::bold, 0.1f);
            const float w = font.getStringWidthFloat(text) + 2.0f;
            g.setColour(colour);
            g.setFont(font);
            g.drawText(text, juce::Rectangle<float>(right - w, 8.0f, w, 22.0f), juce::Justification::centredRight, false);
            if (layout != Layout::compact)
                drawPip(g, {right - w - 9.0f, 19.0f}, colour);
        }

        // Bypassed: dim the panel and show the badge
        if (state.bypassed)
        {
            g.setColour(Colours::background.withAlpha(0.35f));
            g.fillRoundedRectangle(r, 10.0f);

            if (layout != Layout::advanced)
            {
                const juce::String text = layout == Layout::simple ? juce::String::fromUTF8("BYPASSED \xc2\xb7 OUTPUT = INPUT") : juce::String("BYPASSED");
                const auto font = sans(layout == Layout::simple ? 11.0f : 10.5f, Weight::bold, 0.12f);
                const float w = font.getStringWidthFloat(text) + (layout == Layout::simple ? 28.0f : 24.0f);
                const float h = layout == Layout::simple ? 30.0f : 28.0f;
                const auto badge = juce::Rectangle<float>(w, h).withCentre({r.getCentreX(), r.getHeight() * (layout == Layout::simple ? 0.52f : 0.5f)});
                g.setColour(juce::Colour(0xeb3a2c1a));
                g.fillRoundedRectangle(badge, 8.0f);
                g.setColour(Colours::amberBorder);
                g.drawRoundedRectangle(badge.reduced(0.5f), 8.0f, 1.0f);
                g.setColour(Colours::amberLight);
                g.setFont(font);
                g.drawText(text, badge, juce::Justification::centred, false);
            }
        }
    }

    //==============================================================================
    // BandOverlay
    //==============================================================================
    BandOverlay::BandOverlay(juce::AudioProcessorValueTreeState &state, juce::UndoManager *undoManager)
        : apvts(state)
    {
        for (int i = 0; i < numCrossovers; ++i)
        {
            auto *param = apvts.getParameter("xover" + juce::String(i + 1));
            crossoverAttachments[(size_t)i] = std::make_unique<juce::ParameterAttachment>(
                *param, [this, i](float v)
                { crossovers[(size_t)i] = v; repaint(); },
                undoManager);
            crossoverAttachments[(size_t)i]->sendInitialUpdate();
        }

        for (int b = 0; b < numBands; ++b)
        {
            auto *param = apvts.getParameter("bandAmount" + juce::String(b + 1));
            amountAttachments[(size_t)b] = std::make_unique<juce::ParameterAttachment>(
                *param, [this, b](float v)
                { amounts[(size_t)b] = v; repaint(); },
                undoManager);
            amountAttachments[(size_t)b]->sendInitialUpdate();
        }

        setTitle("Band overlay");
        refresh();
    }

    BandOverlay::~BandOverlay() = default;

    void BandOverlay::setSelectedBand(int band)
    {
        band = juce::jlimit(0, numBands - 1, band);
        if (band != selectedBand)
        {
            selectedBand = band;
            repaint();
        }
    }

    void BandOverlay::refresh()
    {
        bool changed = false;
        for (int b = 0; b < numBands; ++b)
        {
            const bool v = apvts.getRawParameterValue("bandBypass" + juce::String(b + 1))->load() > 0.5f;
            changed |= v != bypassed[(size_t)b];
            bypassed[(size_t)b] = v;
        }
        const int newSolo = juce::roundToInt(apvts.getRawParameterValue("bandSolo")->load()) - 1;
        const int newDelta = juce::roundToInt(apvts.getRawParameterValue("bandDelta")->load()) - 1;
        changed |= newSolo != solo || newDelta != delta;
        solo = newSolo;
        delta = newDelta;
        if (changed)
            repaint();
    }

    float BandOverlay::edgeX(int edge) const
    {
        if (edge <= 0)
            return 0.0f;
        if (edge >= numBands)
            return (float)getWidth();
        return frequencyToProportion(crossovers[(size_t)(edge - 1)]) * (float)getWidth();
    }

    int BandOverlay::crossoverAt(float x) const
    {
        for (int i = 0; i < numCrossovers; ++i)
            if (std::abs(x - edgeX(i + 1)) <= 7.0f)
                return i;
        return -1;
    }

    int BandOverlay::bandAt(float x) const
    {
        for (int b = 0; b < numBands; ++b)
            if (x < edgeX(b + 1))
                return b;
        return numBands - 1;
    }

    void BandOverlay::paint(juce::Graphics &g)
    {
        // Band columns
        for (int b = 0; b < numBands; ++b)
        {
            const float l = edgeX(b), rgt = edgeX(b + 1);
            const bool sel = b == selectedBand;
            const auto colour = Colours::bands[b];
            const auto column = juce::Rectangle<float>(l, 0.0f, rgt - l, bandHeight);

            if (sel)
                g.setColour(colour.withAlpha(0.09f));
            else if (solo >= 0 && solo != b)
                g.setColour(Colours::background.withAlpha(0.45f));
            else
                g.setColour(juce::Colours::transparentBlack);
            g.fillRect(column);

            g.setColour(sel ? colour : colour.withAlpha(0.35f));
            g.fillRect(column.withHeight(2.0f));

            g.setColour(sel ? Colours::textPrimary : Colours::textTertiary);
            g.setFont(mono(10.0f, Weight::semibold));
            g.drawText(juce::String(b + 1), juce::Rectangle<float>(l + 6.0f, 22.0f, 14.0f, 12.0f), juce::Justification::centredLeft, false);

            const juce::String flag = solo == b ? "S" : (bypassed[(size_t)b] ? "BYP" : (delta == b ? juce::String::fromUTF8("\xce\x94") : juce::String()));
            if (flag.isNotEmpty())
            {
                const auto font = mono(9.0f, Weight::semibold);
                const auto flagColour = flag == "S" ? juce::Colour(0xff9ec2ff) : Colours::amber;
                const bool isDelta = flag.length() == 1 && flag[0] == (juce::juce_wchar)0x394;
                const auto box = juce::Rectangle<float>(l + 20.0f, 21.0f, isDelta ? 17.0f : font.getStringWidthFloat(flag) + 10.0f, 13.0f);
                g.setColour(flagColour);
                g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.0f);
                if (isDelta)
                {
                    drawDelta(g, box.withSizeKeepingCentre(7.0f, 7.0f), flagColour);
                }
                else
                {
                    g.setFont(font);
                    g.drawText(flag, box, juce::Justification::centred, false);
                }
            }

            // Amount lane
            const auto lane = juce::Rectangle<float>(l + 2.0f, laneTop, rgt - l - 4.0f, laneHeight);
            g.setColour(Colours::well);
            g.fillRoundedRectangle(lane, 3.0f);
            g.setColour(juce::Colour(0xff1e2934));
            g.drawRoundedRectangle(lane.reduced(0.5f), 3.0f, 1.0f);

            const float amt = amounts[(size_t)b] / 100.0f;
            const auto fill = lane.withTop(lane.getBottom() - lane.getHeight() * amt);
            g.setColour(bypassed[(size_t)b] ? juce::Colour(0x1f94a5b7) : colour.withAlpha(sel ? 0.42f : 0.2f));
            g.fillRoundedRectangle(fill, 3.0f);
            if (amt > 0.0f)
            {
                g.setColour(colour);
                g.fillRect(fill.withHeight(2.0f));
            }

            g.setColour(Colours::textPrimary);
            g.setFont(mono(10.0f, Weight::medium));
            g.drawText(juce::String(juce::roundToInt(amounts[(size_t)b])) + "%", lane.withTrimmedLeft(6.0f), juce::Justification::centredLeft, false);
        }

        // Crossover handles
        for (int i = 0; i < numCrossovers; ++i)
        {
            const float x = edgeX(i + 1);
            const bool active = i == hoverCrossover || i == dragCrossover;
            g.setColour(active ? Colours::blue : Colours::wellBorderHover);
            g.fillRect(juce::Rectangle<float>(std::round(x), 0.0f, 1.0f, overlayHeight));

            const auto text = formatFrequency(crossovers[(size_t)i]);
            const auto font = mono(9.5f);
            const auto pill = juce::Rectangle<float>(font.getStringWidthFloat(text) + 12.0f, 16.0f).withCentre({x, 14.0f});
            g.setColour(active ? juce::Colour(0xff1e2b3d) : Colours::popup);
            g.fillRoundedRectangle(pill, 4.0f);
            g.setColour(active ? Colours::blue : Colours::popupBorder);
            g.drawRoundedRectangle(pill.reduced(0.5f), 4.0f, 1.0f);
            g.setColour(active ? Colours::textPrimary : Colours::textSoft);
            g.setFont(font);
            g.drawText(text, pill, juce::Justification::centred, false);
        }
    }

    void BandOverlay::mouseMove(const juce::MouseEvent &e)
    {
        const int xo = crossoverAt(e.position.x);
        if (xo != hoverCrossover)
        {
            hoverCrossover = xo;
            repaint();
        }

        if (xo >= 0)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else if (e.position.y >= laneTop)
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void BandOverlay::mouseExit(const juce::MouseEvent &)
    {
        hoverCrossover = -1;
        repaint();
    }

    void BandOverlay::mouseDown(const juce::MouseEvent &e)
    {
        dragCrossover = crossoverAt(e.position.x);
        if (dragCrossover >= 0)
        {
            crossoverAttachments[(size_t)dragCrossover]->beginGesture();
            repaint();
            return;
        }

        const int band = bandAt(e.position.x);
        setSelectedBand(band);
        if (onBandSelected)
            onBandSelected(band);

        if (e.position.y >= laneTop - 4.0f)
        {
            dragLane = band;
            dragStartY = e.position.y;
            dragStartAmount = amounts[(size_t)band];
            amountAttachments[(size_t)band]->beginGesture();
        }
    }

    void BandOverlay::mouseDrag(const juce::MouseEvent &e)
    {
        if (dragCrossover >= 0)
        {
            const int i = dragCrossover;
            const float lo = i == 0 ? 30.0f : crossovers[(size_t)(i - 1)] * 1.3f;
            const float hi = i == numCrossovers - 1 ? 16000.0f : crossovers[(size_t)(i + 1)] / 1.3f;
            const float f = proportionToFrequency(juce::jlimit(0.0f, 1.0f, e.position.x / (float)getWidth()));
            crossoverAttachments[(size_t)i]->setValueAsPartOfGesture(juce::jlimit(lo, hi, f));
            return;
        }

        if (dragLane >= 0)
        {
            const float rate = e.mods.isShiftDown() ? 0.2f : 1.2f;
            const float v = juce::jlimit(0.0f, 100.0f, dragStartAmount + (dragStartY - e.position.y) * rate);
            amountAttachments[(size_t)dragLane]->setValueAsPartOfGesture(v);
        }
    }

    void BandOverlay::mouseUp(const juce::MouseEvent &)
    {
        if (dragCrossover >= 0)
            crossoverAttachments[(size_t)dragCrossover]->endGesture();
        if (dragLane >= 0)
            amountAttachments[(size_t)dragLane]->endGesture();
        dragCrossover = dragLane = -1;
        repaint();
    }
}
