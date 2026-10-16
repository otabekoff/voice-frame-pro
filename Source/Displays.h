#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "Widgets.h"

namespace vfp::ui
{
    //==============================================================================
    /**
     * Editor-side telemetry: spectrum analysis of the engine's analyser feed, voice-activity
     * and gate histories, and meter ballistics. Updated from the editor's timer.
     */
    class EditorModel
    {
    public:
        static constexpr int fftOrder = 12;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int numPoints = 320;
        static constexpr int historySize = 150; // 5 s at 30 Hz

        explicit EditorModel(VoiceFrameProProcessor &processor);

        void update(double nowSeconds);

        enum Curve
        {
            input = 0,
            output,
            removed
        };
        const std::array<float, numPoints> &getSpectrum(Curve c) const noexcept { return spectrum[(size_t)c]; }

        /** History value i (0 = oldest, historySize - 1 = newest). */
        float getVadHistory(int i) const noexcept { return vadHistory[(size_t)((historyPos + i) % historySize)]; }
        float getGateHistory(int i) const noexcept { return gateHistory[(size_t)((historyPos + i) % historySize)]; }

        struct Meter
        {
            float rmsDb[2]{-100.0f, -100.0f};
            float holdDb[2]{-100.0f, -100.0f};
            double holdTime[2]{0.0, 0.0};
            float getPeakDb() const noexcept { return juce::jmax(holdDb[0], holdDb[1]); }
            void reset();
        };

        Meter inputMeter, outputMeter;
        float voiceProbability = 0.0f;
        float gainReductionDb = 0.0f;
        float gateGain = 1.0f;

    private:
        void updateSpectrum();
        static void updateMeter(Meter &m, const float *rms, const float *peak, double now);

        VoiceFrameProProcessor &processor;
        juce::dsp::FFT fft{fftOrder};
        juce::dsp::WindowingFunction<float> window{(size_t)fftSize, juce::dsp::WindowingFunction<float>::hann, false};

        std::array<std::vector<float>, 3> rings;
        int ringPos = 0, newSamples = 0;
        std::vector<float> pullIn, pullOut, pullRem, fftData;
        std::array<std::array<float, numPoints>, 3> spectrum{};
        std::array<std::pair<int, int>, numPoints> binRanges{};
        std::array<float, numPoints> pointBins{};

        std::array<float, historySize> vadHistory{}, gateHistory{};
        int historyPos = 0;
    };

    //==============================================================================
    /** Frequency ↔ x mapping shared by the spectrum and the band overlay (20 Hz – 20 kHz, log). */
    float frequencyToProportion(float hz);
    float proportionToFrequency(float proportion);

    //==============================================================================
    /** Vertical stereo/mono level meter in its own panel (Simple / Advanced side meters). */
    class LevelMeter : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        enum class Side
        {
            input,
            output
        };

        explicit LevelMeter(Side side);

        void setState(const EditorModel::Meter &meter, int numChannels, bool clipLit,
                      const juce::String &title, juce::Colour titleColour, bool removedGradient);

        std::function<void()> onClipClicked;

        void paint(juce::Graphics &) override;
        void mouseUp(const juce::MouseEvent &) override;

        static void drawBar(juce::Graphics &g, juce::Rectangle<float> bar, float levelDb, float peakDb, bool removedGradient, bool stripes);
        static float dbToProportion(float db) { return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); }

    private:
        juce::Rectangle<float> clipBadgeBounds() const;

        Side side;
        EditorModel::Meter state;
        int channels = 2;
        bool clipLit = false, removedGradient = false;
        juce::String title{"Input"};
        juce::Colour titleColour = Colours::textSecondary;
    };

    //==============================================================================
    /** Spectrum panel. Its band overlay (Advanced mode) is a child component. */
    class SpectrumView : public juce::Component
    {
    public:
        enum class Layout
        {
            simple,
            advanced,
            compact
        };

        SpectrumView(EditorModel &model, Layout layout);

        struct State
        {
            int monitor = 1;
            int deltaBand = -1;
            bool bypassed = false;
        };
        void setState(const State &s);

        void setOverlay(juce::Component *overlay);
        juce::Rectangle<float> getPlotBounds() const;

        void paint(juce::Graphics &) override;
        void resized() override;

    private:
        void paintCurves(juce::Graphics &, juce::Rectangle<float> plot);
        juce::Path makeLine(const std::array<float, EditorModel::numPoints> &data, juce::Rectangle<float> plot) const;

        EditorModel &model;
        Layout layout;
        State state;
        float dbTop = -6.0f, dbBottom = -102.0f;
        LegendButton legendInput{"Input", Colours::inputCurve};
        LegendButton legendOutput{"Output", Colours::teal};
        LegendButton legendRemoved{"Removed (difference)", Colours::amber};
        juce::Component *overlay = nullptr;
    };

    //==============================================================================
    /** Advanced mode: six band columns, amount lanes and draggable crossover handles. */
    class BandOverlay : public juce::Component
    {
    public:
        BandOverlay(juce::AudioProcessorValueTreeState &apvts, juce::UndoManager *undoManager);
        ~BandOverlay() override;

        int getSelectedBand() const noexcept { return selectedBand; }
        void setSelectedBand(int band);
        std::function<void(int)> onBandSelected;

        void refresh(); // re-reads bypass / solo / listen flags

        void paint(juce::Graphics &) override;
        void mouseMove(const juce::MouseEvent &) override;
        void mouseExit(const juce::MouseEvent &) override;
        void mouseDown(const juce::MouseEvent &) override;
        void mouseDrag(const juce::MouseEvent &) override;
        void mouseUp(const juce::MouseEvent &) override;

        static constexpr float bandHeight = 158.0f, laneTop = 180.0f, laneHeight = 40.0f, overlayHeight = 222.0f;

    private:
        float edgeX(int edge) const; // 0..6, 0 = 20 Hz, 6 = 20 kHz
        int crossoverAt(float x) const;
        int bandAt(float x) const;

        juce::AudioProcessorValueTreeState &apvts;
        std::array<float, numCrossovers> crossovers{};
        std::array<float, numBands> amounts{};
        std::array<std::unique_ptr<juce::ParameterAttachment>, numCrossovers> crossoverAttachments;
        std::array<std::unique_ptr<juce::ParameterAttachment>, numBands> amountAttachments;

        int selectedBand = 2, hoverCrossover = -1, dragCrossover = -1, dragLane = -1;
        float dragStartY = 0.0f, dragStartAmount = 0.0f;
        std::array<bool, numBands> bypassed{};
        int solo = -1, delta = -1;
    };
}
