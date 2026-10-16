#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DenoiseEngine.h"
#include "PresetManager.h"

//==============================================================================
/**
 * VoiceFrame Pro AudioProcessor — real-time RNNoise voice denoiser.
 */
class VoiceFrameProProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    VoiceFrameProProcessor();
    ~VoiceFrameProProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    //==============================================================================
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String &newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    juce::AudioProcessorParameter *getBypassParameter() const override;

    //==============================================================================
    juce::AudioProcessorValueTreeState &getAPVTS() noexcept { return parameters; }
    juce::UndoManager &getUndoManager() noexcept { return undoManager; }
    vfp::PresetManager &getPresetManager() noexcept { return presetManager; }
    vfp::DenoiseEngine &getEngine() noexcept { return engine; }

    /** Editor-only state (view mode, scale, selected band…) that is saved with the session. */
    juce::ValueTree &getUIState() noexcept { return uiState; }

    /** True until the first editor has discarded the undo entries made while loading the start-up preset. */
    bool needsInitialUndoReset = true;

    // Metering values for GUI (linear gain)
    float getInputPeak(int ch) const noexcept { return inputPeak[ch & 1].load(std::memory_order_relaxed); }
    float getInputRms(int ch) const noexcept { return inputRms[ch & 1].load(std::memory_order_relaxed); }
    float getOutputPeak(int ch) const noexcept { return outputPeak[ch & 1].load(std::memory_order_relaxed); }
    float getOutputRms(int ch) const noexcept { return outputRms[ch & 1].load(std::memory_order_relaxed); }

    bool hasInputClipped() const noexcept { return inputClipped.load(std::memory_order_relaxed); }
    int getOutputClipCount() const noexcept { return outputClips.load(std::memory_order_relaxed); }
    void resetClipIndicators() noexcept;

    float getLimiterReductionDb() const noexcept { return limiter.getGainReductionDb(); }
    double getDspLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    int getNumActiveChannels() const noexcept;

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    vfp::EngineSettings readEngineSettings() const noexcept;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState parameters;
    vfp::PresetManager presetManager;
    juce::ValueTree uiState{"UIState"};

    // Cached raw parameter pointers for audio thread performance
    std::atomic<float> *bypassParam = nullptr;
    std::atomic<float> *amountParam = nullptr;
    std::atomic<float> *inputGainParam = nullptr;
    std::atomic<float> *outputGainParam = nullptr;
    std::atomic<float> *monitorParam = nullptr;
    std::atomic<float> *limiterParam = nullptr;
    std::atomic<float> *limiterCeilingParam = nullptr;
    std::array<std::atomic<float> *, vfp::numCrossovers> crossoverParams{};
    std::array<std::atomic<float> *, vfp::numBands> bandAmountParams{};
    std::array<std::atomic<float> *, vfp::numBands> bandBypassParams{};
    std::atomic<float> *bandSoloParam = nullptr;
    std::atomic<float> *bandDeltaParam = nullptr;
    std::atomic<float> *gateOnParam = nullptr;
    std::atomic<float> *gateSensitivityParam = nullptr;
    std::atomic<float> *gateRangeParam = nullptr;
    std::atomic<float> *gateAttackParam = nullptr;
    std::atomic<float> *gateHoldParam = nullptr;
    std::atomic<float> *gateReleaseParam = nullptr;
    std::atomic<float> *stereoLinkParam = nullptr;
    std::atomic<float> *stereoWidthParam = nullptr;

    // Parameter smoothers
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedInputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWidth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedBypass;

    // DSP
    vfp::DenoiseEngine engine;
    vfp::SafetyLimiter limiter;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    double currentSampleRate = 48000.0;

    // Metering atomics
    std::atomic<float> inputPeak[2]{}, inputRms[2]{}, outputPeak[2]{}, outputRms[2]{};
    std::atomic<bool> inputClipped{false};
    std::atomic<int> outputClips{0};

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceFrameProProcessor)
};
