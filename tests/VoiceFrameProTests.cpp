// VoiceFrame Pro test runner: DSP checks, state/preset checks and editor snapshots.
//
// Usage: VoiceFrameProTests [snapshotDirectory]
// Returns 0 when every check passes.

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <random>

namespace
{
    int failures = 0;

    void check(bool condition, const juce::String &what)
    {
        std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (!condition)
            ++failures;
    }

    std::unique_ptr<VoiceFrameProProcessor> makeProcessor(int channels, double sampleRate, int blockSize)
    {
        auto p = std::make_unique<VoiceFrameProProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(set);
        layout.outputBuses.add(set);
        p->setBusesLayout(layout);
        p->setRateAndBufferSizeDetails(sampleRate, blockSize);
        p->prepareToPlay(sampleRate, blockSize);
        return p;
    }

    void setParam(VoiceFrameProProcessor &p, const juce::String &id, float value)
    {
        auto *param = p.getAPVTS().getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(value));
    }

    /**
     * Test signal generator: pink-ish noise (RNNoise, like most speech denoisers, barely reacts to
     * synthetic full-band white noise) plus an optional pulsed harmonic "voice", or a pure tone.
     */
    struct Source
    {
        double sampleRate;
        float noiseDb, voiceDb;
        float toneHz = 0.0f;
        bool white = false; // broadband, for delay measurement
        std::mt19937 rng{1234};
        std::normal_distribution<float> gauss{0.0f, 1.0f};
        double phase = 0.0, tonePhase = 0.0;
        float pinkState = 0.0f;
        int64_t n = 0;

        float next()
        {
            if (toneHz > 0.0f)
            {
                tonePhase += juce::MathConstants<double>::twoPi * toneHz / sampleRate;
                return juce::Decibels::decibelsToGain(noiseDb) * (float)std::sin(tonePhase);
            }

            const float w = gauss(rng);
            pinkState = 0.97f * pinkState + 0.25f * w;
            float s = juce::Decibels::decibelsToGain(noiseDb) * (white ? w : pinkState + 0.3f * w);
            if (voiceDb > -100.0f)
            {
                const double t = (double)n / sampleRate;
                const double gate = std::fmod(t, 1.0) < 0.6 ? 1.0 : 0.0; // 600 ms on, 400 ms off
                double v = 0.0;
                for (int h = 1; h <= 12; ++h)
                    v += std::sin(phase * h) / h;
                s += (float)(juce::Decibels::decibelsToGain((double)voiceDb) * 0.5 * v * gate);
                phase += juce::MathConstants<double>::twoPi * 140.0 / sampleRate;
            }
            ++n;
            return s;
        }
    };

    /** Runs the processor over `seconds` of the source; returns input and output of channel 0. */
    void run(VoiceFrameProProcessor &p, Source &src, double seconds, int blockSize,
             std::vector<float> &in, std::vector<float> &out)
    {
        const int channels = p.getTotalNumInputChannels();
        const auto total = (int64_t)(seconds * src.sampleRate);
        juce::AudioBuffer<float> buffer(channels, blockSize);
        juce::MidiBuffer midi;

        for (int64_t done = 0; done < total; done += blockSize)
        {
            const int n = (int)juce::jmin<int64_t>(blockSize, total - done);
            buffer.setSize(channels, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                const float s = src.next();
                for (int ch = 0; ch < channels; ++ch)
                    buffer.setSample(ch, i, s);
                in.push_back(s);
            }
            p.processBlock(buffer, midi);
            for (int i = 0; i < n; ++i)
                out.push_back(buffer.getSample(0, i));
        }
    }

    float rmsDb(const std::vector<float> &v, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i)
            sum += (double)v[i] * v[i];
        return (float)juce::Decibels::gainToDecibels(std::sqrt(sum / (double)juce::jmax<size_t>(1, to - from)), -200.0);
    }

    bool allFinite(const std::vector<float> &v)
    {
        for (auto x : v)
            if (!std::isfinite(x))
                return false;
        return true;
    }

    //==============================================================================
    void testBypassAndLatency(double sr, int block)
    {
        auto p = makeProcessor(2, sr, block);
        setParam(*p, "bypass", 1.0f);
        Source src{sr, -20.0f, -12.0f};
        std::vector<float> in, out;
        run(*p, src, 1.0, block, in, out);

        const int latency = p->getLatencySamples();
        float maxErr = 0.0f;
        for (size_t i = (size_t)latency + 2048; i < out.size(); ++i)
            maxErr = juce::jmax(maxErr, std::abs(out[i] - in[i - (size_t)latency]));

        check(maxErr < 1.0e-5f, "bypass is a latency-matched copy of the input @" + juce::String(sr) + " Hz / " +
                                    juce::String(block) + " (latency " + juce::String(latency) + ", max err " + juce::String(maxErr) + ")");
    }

    /** Returns (peak lag - reported latency) for broadband noise through the processed path. */
    int measureProcessedOffset(double sr, int block)
    {
        auto p = makeProcessor(2, sr, block);
        setParam(*p, "amount", 0.0f);
        setParam(*p, "limiter", 0.0f);
        // Move every crossover to the top of the range: their all-pass phase would otherwise bias
        // the broadband correlation peak by a few samples (more so when resampling band-limits it)
        for (int i = 1; i <= 5; ++i)
            setParam(*p, "xover" + juce::String(i), 16000.0f);
        Source src{sr, -20.0f, -200.0f};
        src.white = true;
        std::vector<float> in, out;
        run(*p, src, 1.5, block, in, out);

        // Cross-correlate around the reported latency; the crossover all-pass adds little broadband delay
        const int latency = p->getLatencySamples();
        int bestLag = 0;
        double best = -1.0;
        for (int lag = latency - 64; lag <= latency + 64; ++lag)
        {
            double acc = 0.0;
            for (size_t i = (size_t)sr / 2; i < out.size(); ++i)
                acc += (double)out[i] * in[i - (size_t)lag];
            if (acc > best)
            {
                best = acc;
                bestLag = lag;
            }
        }
        return bestLag - latency;
    }

    void testTransparencyAtZero(double sr)
    {
        // At 0 % the processed path should be (all-pass) transparent up to the engine's 20 kHz band limit
        juce::String worst;
        float worstDev = 0.0f;
        for (float hz : {60.0f, 200.0f, 1000.0f, 4000.0f, 8000.0f, 12000.0f, 16000.0f})
        {
            auto p = makeProcessor(2, sr, 512);
            setParam(*p, "amount", 0.0f);
            setParam(*p, "limiter", 0.0f);
            Source src{sr, -12.0f, -200.0f, hz};
            std::vector<float> in, out;
            run(*p, src, 1.0, 512, in, out);
            const size_t start = (size_t)(sr * 0.5);
            const float dev = rmsDb(out, start, out.size()) - rmsDb(in, start, in.size());
            if (std::abs(dev) >= std::abs(worstDev))
            {
                worstDev = dev;
                worst = juce::String(hz, 0) + " Hz";
            }
        }
        check(std::abs(worstDev) < 1.0f, "amount 0% is level-transparent 60 Hz - 16 kHz @" + juce::String(sr) + " Hz (worst " +
                                            juce::String(worstDev, 2) + " dB at " + worst + ")");
    }

    void testNoiseReduction(double sr, int block)
    {
        auto p = makeProcessor(2, sr, block);
        setParam(*p, "amount", 100.0f);
        Source src{sr, -35.0f, -200.0f};
        std::vector<float> in, out;
        run(*p, src, 4.0, block, in, out);

        const size_t start = (size_t)(2.0 * sr);
        const float reduction = rmsDb(in, start, in.size()) - rmsDb(out, start, out.size());
        check(allFinite(out), "output is finite @" + juce::String(sr) + " Hz");
        check(reduction > 10.0f, "stationary noise is reduced by more than 10 dB at 100% @" + juce::String(sr) +
                                     " Hz (" + juce::String(reduction, 1) + " dB)");
        check(p->getEngine().getGainReductionDb() < -6.0f, "measured gain reduction is reported (" +
                                                              juce::String(p->getEngine().getGainReductionDb(), 1) + " dB)");
    }

    void testModesAreStable()
    {
        struct Case
        {
            const char *name;
            std::vector<std::pair<const char *, float>> params;
            int channels;
        };
        const Case cases[] = {
            {"monitor original", {{"monitor", 0.0f}}, 2},
            {"monitor removed", {{"monitor", 2.0f}}, 2},
            {"band solo 3", {{"bandSolo", 3.0f}}, 2},
            {"band listen 5", {{"bandDelta", 5.0f}}, 2},
            {"gate on, -60 dB range", {{"gateOn", 1.0f}, {"gateRange", -60.0f}}, 2},
            {"dual mono, width 200%", {{"stereoLink", 0.0f}, {"stereoWidth", 200.0f}}, 2},
            {"mono track", {{"amount", 80.0f}}, 1},
            {"extreme crossovers", {{"xover1", 16000.0f}, {"xover5", 30.0f}, {"bandBypass2", 1.0f}}, 2},
            {"+24 dB output into limiter", {{"outputGain", 24.0f}, {"inputGain", 24.0f}}, 2},
        };

        for (const auto &c : cases)
        {
            auto p = makeProcessor(c.channels, 48000.0, 256);
            for (const auto &[id, v] : c.params)
                setParam(*p, id, v);
            Source src{48000.0, -25.0f, -10.0f};
            std::vector<float> in, out;
            run(*p, src, 1.0, 256, in, out);

            float peak = 0.0f;
            for (auto x : out)
                peak = juce::jmax(peak, std::abs(x));
            const bool limited = juce::String(c.name).contains("limiter");
            check(allFinite(out) && (!limited || peak <= juce::Decibels::decibelsToGain(-1.0f) + 1.0e-4f),
                  juce::String("stable: ") + c.name + " (peak " + juce::String(juce::Decibels::gainToDecibels(peak), 1) + " dBFS)");
        }
    }

    void testStateAndPresets()
    {
        auto a = makeProcessor(2, 48000.0, 512);
        auto &pm = a->getPresetManager();
        check(pm.getCurrentPreset().name == "Podcast Dialogue" && std::abs(a->getAPVTS().getRawParameterValue("amount")->load() - 60.0f) < 0.01f,
              "new instance starts on Podcast Dialogue (60%)");
        check(!pm.isModified(), "start-up preset is not marked modified");

        pm.loadPreset(5);
        check(std::abs(a->getAPVTS().getRawParameterValue("amount")->load() - 92.0f) < 0.01f, "loading Strong Suppression sets 92%");

        setParam(*a, "gateOn", 1.0f);
        setParam(*a, "xover3", 2000.0f);
        setParam(*a, "bandAmount4", 33.0f);
        check(pm.isModified(), "editing a parameter marks the preset modified");

        pm.selectSlot(1);
        setParam(*a, "amount", 10.0f);
        pm.selectSlot(0);
        check(std::abs(a->getAPVTS().getRawParameterValue("amount")->load() - 92.0f) < 0.01f, "A/B: switching back to A restores its values");

        juce::MemoryBlock state;
        a->getStateInformation(state);
        auto b = makeProcessor(2, 48000.0, 512);
        b->setStateInformation(state.getData(), (int)state.getSize());

        auto value = [](VoiceFrameProProcessor &p, const char *id)
        { return p.getAPVTS().getRawParameterValue(id)->load(); };
        check(value(*b, "gateOn") > 0.5f && std::abs(value(*b, "xover3") - 2000.0f) < 0.5f && std::abs(value(*b, "bandAmount4") - 33.0f) < 0.01f,
              "session state round-trips parameters");
        check(b->getPresetManager().getCurrentPreset().name == "Strong Suppression" && b->getPresetManager().isModified(),
              "session state round-trips the preset name and modified flag");
    }

    //==============================================================================
    void snapshotEditor(const juce::File &dir, int mode, const juce::String &name)
    {
        auto p = makeProcessor(2, 48000.0, 480);
        p->getUIState().setProperty("mode", mode, nullptr);
        if (mode == 1)
            setParam(*p, "gateOn", 1.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        Source src{48000.0, -45.0f, -14.0f};
        juce::AudioBuffer<float> buffer(2, 480);
        juce::MidiBuffer midi;

        // ~3 s of audio, letting the editor's timer run between blocks
        for (int b = 0; b < 300; ++b)
        {
            for (int i = 0; i < 480; ++i)
            {
                const float s = src.next();
                buffer.setSample(0, i, s);
                buffer.setSample(1, i, s * 0.9f);
            }
            p->processBlock(buffer, midi);
            if (b % 3 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

        check(!p->getUndoManager().canUndo(), name + ": no undo step after opening the editor");

        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const auto file = dir.getChildFile(name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream(file);
        const bool written = stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream);
        check(written, name + ": snapshot " + juce::String(image.getWidth()) + "x" + juce::String(image.getHeight()) + " -> " + file.getFullPathName());

        editor.reset();
    }
}

int main(int argc, char *argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;

    std::cout << "VoiceFrame Pro tests" << std::endl;

    std::cout << "DSP" << std::endl;
    for (double sr : {48000.0, 44100.0, 88200.0, 96000.0, 192000.0})
        for (int block : {64, 1000})
            testBypassAndLatency(sr, block);
    {
        // The crossover all-pass delays a broadband signal by a fixed time in the 48 kHz engine domain,
        // so the offset at other host rates must equal the 48 kHz offset scaled by the rate ratio.
        const int reference = measureProcessedOffset(48000.0, 512);
        check(reference >= 0 && reference <= 6, "processed path lines up with reported latency @48000 Hz (offset " + juce::String(reference) + ")");
        for (double sr : {44100.0, 88200.0, 96000.0, 192000.0})
        {
            const int offset = measureProcessedOffset(sr, 512);
            const double expected = reference * sr / 48000.0;
            check(std::abs(offset - expected) <= 2.0, "processed path lines up with reported latency @" + juce::String(sr) +
                                                          " Hz (offset " + juce::String(offset) + ", expected " + juce::String(expected, 1) + ")");
        }
    }
    for (double sr : {48000.0, 44100.0, 88200.0, 96000.0, 192000.0})
        testTransparencyAtZero(sr);
    for (double sr : {48000.0, 44100.0})
        testNoiseReduction(sr, 256);
    testModesAreStable();

    std::cout << "State" << std::endl;
    testStateAndPresets();

    if (argc > 1)
    {
        std::cout << "Editor" << std::endl;
        const juce::File dir(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
        dir.createDirectory();
        snapshotEditor(dir, 0, "simple");
        snapshotEditor(dir, 1, "advanced");
        snapshotEditor(dir, 2, "compact");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String(failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
