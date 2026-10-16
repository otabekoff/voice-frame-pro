#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
VoiceFrameProProcessor::VoiceFrameProProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, &undoManager, juce::Identifier("VoiceFrameProParams"), createParameterLayout()),
      presetManager(parameters)
{
    // Cache raw parameter pointers for lock-free audio thread access
    bypassParam = parameters.getRawParameterValue("bypass");
    amountParam = parameters.getRawParameterValue("amount");
    inputGainParam = parameters.getRawParameterValue("inputGain");
    outputGainParam = parameters.getRawParameterValue("outputGain");
    monitorParam = parameters.getRawParameterValue("monitor");
    limiterParam = parameters.getRawParameterValue("limiter");
    limiterCeilingParam = parameters.getRawParameterValue("limiterCeiling");

    for (int i = 0; i < vfp::numCrossovers; ++i)
        crossoverParams[(size_t)i] = parameters.getRawParameterValue("xover" + juce::String(i + 1));

    for (int b = 0; b < vfp::numBands; ++b)
    {
        bandAmountParams[(size_t)b] = parameters.getRawParameterValue("bandAmount" + juce::String(b + 1));
        bandBypassParams[(size_t)b] = parameters.getRawParameterValue("bandBypass" + juce::String(b + 1));
    }

    bandSoloParam = parameters.getRawParameterValue("bandSolo");
    bandDeltaParam = parameters.getRawParameterValue("bandDelta");

    gateOnParam = parameters.getRawParameterValue("gateOn");
    gateSensitivityParam = parameters.getRawParameterValue("gateSensitivity");
    gateRangeParam = parameters.getRawParameterValue("gateRange");
    gateAttackParam = parameters.getRawParameterValue("gateAttack");
    gateHoldParam = parameters.getRawParameterValue("gateHold");
    gateReleaseParam = parameters.getRawParameterValue("gateRelease");

    stereoLinkParam = parameters.getRawParameterValue("stereoLink");
    stereoWidthParam = parameters.getRawParameterValue("stereoWidth");

    // Start on "Podcast Dialogue", the general-purpose factory preset
    presetManager.loadPreset(2);
}

VoiceFrameProProcessor::~VoiceFrameProProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VoiceFrameProProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout](const juce::String &id, const juce::String &name,
                              juce::NormalisableRange<float> range, float defaultValue,
                              std::function<juce::String(float, int)> toText)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name, range, defaultValue,
            juce::String(), juce::AudioProcessorParameter::genericParameter, std::move(toText)));
    };

    auto addBool = [&layout](const juce::String &id, const juce::String &name, bool defaultValue)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, defaultValue));
    };

    auto percent = [](float val, int)
    { return juce::String(juce::roundToInt(val)) + " %"; };
    auto decibels = [](float val, int)
    { return juce::String(val, 1) + " dB"; };
    auto millis = [](float val, int)
    { return juce::String(val, val < 10.0f ? 1 : 0) + " ms"; };
    auto hertz = [](float val, int)
    { return val >= 1000.0f ? juce::String(val / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(val)) + " Hz"; };

    // Global
    addBool("bypass", "Bypass", false);

    addFloat("amount", "Noise Reduction",
             juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, percent);

    addFloat("inputGain", "Input Gain",
             juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, decibels);

    addFloat("outputGain", "Output Gain",
             juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, decibels);

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"monitor", 1}, "Monitor",
        juce::StringArray{"Original", "Processed", "Removed"}, 1));

    addBool("limiter", "Limiter", true);

    addFloat("limiterCeiling", "Limiter Ceiling",
             juce::NormalisableRange<float>(-12.0f, 0.0f, 0.1f), -1.0f, decibels);

    // Crossover bands (log frequency mapping)
    const juce::NormalisableRange<float> freqRange(
        30.0f, 16000.0f,
        [](float start, float end, float t)
        { return start * std::pow(end / start, t); },
        [](float start, float end, float v)
        { return std::log(v / start) / std::log(end / start); },
        [](float start, float end, float v)
        { return juce::jlimit(start, end, (float)juce::roundToInt(v)); });

    const float crossoverDefaults[vfp::numCrossovers] = {120.0f, 400.0f, 1500.0f, 4000.0f, 9000.0f};
    for (int i = 0; i < vfp::numCrossovers; ++i)
        addFloat("xover" + juce::String(i + 1), "Crossover " + juce::String(i + 1),
                 freqRange, crossoverDefaults[i], hertz);

    for (int b = 0; b < vfp::numBands; ++b)
    {
        addFloat("bandAmount" + juce::String(b + 1), "Band " + juce::String(b + 1) + " Amount",
                 juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f, percent);
        addBool("bandBypass" + juce::String(b + 1), "Band " + juce::String(b + 1) + " Bypass", false);
    }

    const juce::StringArray bandChoices{"Off", "Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6"};
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"bandSolo", 1}, "Band Solo", bandChoices, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"bandDelta", 1}, "Band Listen Removed", bandChoices, 0));

    // Voice gate
    addBool("gateOn", "Voice Gate", false);

    addFloat("gateSensitivity", "Gate Sensitivity",
             juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, percent);

    addFloat("gateRange", "Gate Range",
             juce::NormalisableRange<float>(-60.0f, 0.0f, 1.0f), -18.0f, decibels);

    addFloat("gateAttack", "Gate Attack",
             juce::NormalisableRange<float>(0.5f, 50.0f, 0.1f, 0.5f), 5.0f, millis);

    addFloat("gateHold", "Gate Hold",
             juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 120.0f, millis);

    addFloat("gateRelease", "Gate Release",
             juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.5f), 180.0f, millis);

    // Stereo
    addBool("stereoLink", "Stereo Link", true);

    addFloat("stereoWidth", "Stereo Width",
             juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f, percent);

    return layout;
}

//==============================================================================
const juce::String VoiceFrameProProcessor::getName() const
{
    return JucePlugin_Name;
}

bool VoiceFrameProProcessor::acceptsMidi() const { return false; }
bool VoiceFrameProProcessor::producesMidi() const { return false; }
bool VoiceFrameProProcessor::isMidiEffect() const { return false; }
double VoiceFrameProProcessor::getTailLengthSeconds() const { return 0.0; }

int VoiceFrameProProcessor::getNumPrograms() { return 1; }
int VoiceFrameProProcessor::getCurrentProgram() { return 0; }
void VoiceFrameProProcessor::setCurrentProgram(int) {}
const juce::String VoiceFrameProProcessor::getProgramName(int) { return {}; }
void VoiceFrameProProcessor::changeProgramName(int, const juce::String &) {}

juce::AudioProcessorParameter *VoiceFrameProProcessor::getBypassParameter() const
{
    return parameters.getParameter("bypass");
}

int VoiceFrameProProcessor::getNumActiveChannels() const noexcept
{
    return juce::jlimit(1, 2, getMainBusNumInputChannels());
}

//==============================================================================
void VoiceFrameProProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const double validSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int validBlockSize = juce::jmax(32, samplesPerBlock);
    const int numChannels = getNumActiveChannels();
    currentSampleRate = validSampleRate;

    smoothedInputGain.reset(validSampleRate, 0.02);
    smoothedOutputGain.reset(validSampleRate, 0.02);
    smoothedWidth.reset(validSampleRate, 0.05);
    smoothedBypass.reset(validSampleRate, 0.015);
    smoothedBypass.setCurrentAndTargetValue(bypassParam->load() > 0.5f ? 1.0f : 0.0f);

    engine.prepare(validSampleRate, validBlockSize, numChannels);
    limiter.prepare(validSampleRate, numChannels);

    const int latency = engine.getLatencySamples() + limiter.getLatencySamples();
    setLatencySamples(latency);

    dryDelay.setMaximumDelayInSamples(latency + 1);
    dryDelay.prepare({validSampleRate, (juce::uint32)validBlockSize, (juce::uint32)numChannels});
    dryDelay.setDelay((float)latency);

    dryBuffer.setSize(numChannels, validBlockSize);
    dryBuffer.clear();

    loadMeasurer.reset(validSampleRate, validBlockSize);

    for (int ch = 0; ch < 2; ++ch)
    {
        inputPeak[ch].store(0.0f);
        inputRms[ch].store(0.0f);
        outputPeak[ch].store(0.0f);
        outputRms[ch].store(0.0f);
    }
}

void VoiceFrameProProcessor::releaseResources()
{
    engine.reset();
    limiter.reset();
    dryDelay.reset();
    dryBuffer.setSize(0, 0);
}

bool VoiceFrameProProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    const auto &mainIn = layouts.getMainInputChannelSet();
    const auto &mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    return true;
}

void VoiceFrameProProcessor::resetClipIndicators() noexcept
{
    inputClipped.store(false);
    outputClips.store(0);
}

vfp::EngineSettings VoiceFrameProProcessor::readEngineSettings() const noexcept
{
    vfp::EngineSettings s;
    s.amount = amountParam->load(std::memory_order_relaxed) * 0.01f;

    for (size_t i = 0; i < crossoverParams.size(); ++i)
        s.crossovers[i] = crossoverParams[i]->load(std::memory_order_relaxed);

    for (size_t b = 0; b < bandAmountParams.size(); ++b)
    {
        s.bandAmounts[b] = bandAmountParams[b]->load(std::memory_order_relaxed) * 0.01f;
        s.bandBypass[b] = bandBypassParams[b]->load(std::memory_order_relaxed) > 0.5f;
    }

    s.soloBand = juce::roundToInt(bandSoloParam->load(std::memory_order_relaxed)) - 1;
    s.deltaBand = juce::roundToInt(bandDeltaParam->load(std::memory_order_relaxed)) - 1;
    s.monitor = static_cast<vfp::MonitorMode>(juce::jlimit(0, 2, juce::roundToInt(monitorParam->load(std::memory_order_relaxed))));
    s.linked = stereoLinkParam->load(std::memory_order_relaxed) > 0.5f;

    s.gateOn = gateOnParam->load(std::memory_order_relaxed) > 0.5f;
    s.gateSensitivity = gateSensitivityParam->load(std::memory_order_relaxed) * 0.01f;
    s.gateRangeDb = gateRangeParam->load(std::memory_order_relaxed);
    s.gateAttackMs = gateAttackParam->load(std::memory_order_relaxed);
    s.gateHoldMs = gateHoldParam->load(std::memory_order_relaxed);
    s.gateReleaseMs = gateReleaseParam->load(std::memory_order_relaxed);
    return s;
}

//==============================================================================
void VoiceFrameProProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer(loadMeasurer, buffer.getNumSamples());

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(totalNumInputChannels, 2);

    // Clear unused output channels
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    if (numSamples == 0 || numChannels == 0)
        return;

    // 1. Keep the untouched input for the latency-matched bypass path
    if (dryBuffer.getNumSamples() < numSamples || dryBuffer.getNumChannels() < numChannels)
        dryBuffer.setSize(numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // 2. Input gain + input metering
    smoothedInputGain.setTargetValue(juce::Decibels::decibelsToGain(inputGainParam->load(std::memory_order_relaxed)));
    smoothedInputGain.applyGain(buffer, numSamples);

    for (int ch = 0; ch < 2; ++ch)
    {
        const int src = juce::jmin(ch, numChannels - 1);
        const float peak = buffer.getMagnitude(src, 0, numSamples);
        inputPeak[ch].store(peak, std::memory_order_relaxed);
        inputRms[ch].store(buffer.getRMSLevel(src, 0, numSamples), std::memory_order_relaxed);
        if (peak >= 1.0f)
            inputClipped.store(true, std::memory_order_relaxed);
    }

    // 3. Denoise (RNNoise + band blend + gate + monitor routing)
    engine.process(buffer, readEngineSettings());

    // 4. Stereo width (mid/side)
    smoothedWidth.setTargetValue(stereoWidthParam->load(std::memory_order_relaxed) * 0.01f);
    if (numChannels == 2)
    {
        auto *left = buffer.getWritePointer(0);
        auto *right = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float width = smoothedWidth.getNextValue();
            const float mid = 0.5f * (left[i] + right[i]);
            const float side = 0.5f * (left[i] - right[i]) * width;
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }
    else
    {
        smoothedWidth.skip(numSamples);
    }

    // 5. Output gain
    smoothedOutputGain.setTargetValue(juce::Decibels::decibelsToGain(outputGainParam->load(std::memory_order_relaxed)));
    smoothedOutputGain.applyGain(buffer, numSamples);

    // 6. Safety limiter (its look-ahead delay is always in the path)
    limiter.process(buffer, limiterParam->load(std::memory_order_relaxed) > 0.5f,
                    limiterCeilingParam->load(std::memory_order_relaxed));

    // 7. Bypass: crossfade to the latency-matched dry signal
    smoothedBypass.setTargetValue(bypassParam->load(std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const float dryMix = smoothedBypass.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            dryDelay.pushSample(ch, dryBuffer.getSample(ch, i));
            const float dry = dryDelay.popSample(ch);
            if (dryMix > 0.0f)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * (1.0f - dryMix) + dry * dryMix);
        }
    }

    // 8. Output metering
    bool clipped = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        const int src = juce::jmin(ch, numChannels - 1);
        const float peak = buffer.getMagnitude(src, 0, numSamples);
        outputPeak[ch].store(peak, std::memory_order_relaxed);
        outputRms[ch].store(buffer.getRMSLevel(src, 0, numSamples), std::memory_order_relaxed);
        clipped = clipped || peak > 1.0f;
    }

    if (clipped)
        outputClips.fetch_add(1, std::memory_order_relaxed);
}

//==============================================================================
bool VoiceFrameProProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor *VoiceFrameProProcessor::createEditor()
{
    return new VoiceFrameProEditor(*this);
}

//==============================================================================
void VoiceFrameProProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    juce::ValueTree root("VoiceFrameProState");
    root.appendChild(parameters.copyState(), nullptr);
    root.appendChild(presetManager.toValueTree(), nullptr);
    root.appendChild(uiState.createCopy(), nullptr);

    std::unique_ptr<juce::XmlElement> xml(root.createXml());
    copyXmlToBinary(*xml, destData);
}

void VoiceFrameProProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr || !xmlState->hasTagName("VoiceFrameProState"))
        return;

    const auto root = juce::ValueTree::fromXml(*xmlState);
    const auto params = root.getChildWithName(parameters.state.getType());
    if (params.isValid())
        parameters.replaceState(params);

    presetManager.fromValueTree(root.getChildWithName("PresetSession"));

    const auto ui = root.getChildWithName(uiState.getType());
    if (ui.isValid())
        uiState.copyPropertiesFrom(ui, nullptr);
}

//==============================================================================
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new VoiceFrameProProcessor();
}
