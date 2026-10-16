#include "DenoiseEngine.h"
#include <rnnoise.h>
#include <mutex>

namespace vfp
{
    //==============================================================================
    // StreamResampler
    //==============================================================================
    void StreamResampler::prepare(double inputRate, double outputRate, int numChannels)
    {
        channels = juce::jlimit(1, 2, numChannels);
        step = inputRate / outputRate;

        // Pass band as a fraction of the input Nyquist: 92 % of the lower Nyquist frequency
        const double bandwidth = juce::jmin(1.0, outputRate / inputRate) * 0.92;
        taps = juce::jlimit(32, 128, 2 * (int)std::ceil(12.0 / bandwidth));

        const int centre = taps / 2 - 1;
        const double halfWidth = taps / 2.0;
        table.assign((size_t)((phases + 1) * taps), 0.0f);

        for (int p = 0; p <= phases; ++p)
        {
            double sum = 0.0;
            auto *row = table.data() + (size_t)p * (size_t)taps;
            for (int i = 0; i < taps; ++i)
            {
                const double d = (double)(i - centre) - (double)p / phases;
                const double x = bandwidth * d;
                const double sinc = std::abs(x) < 1.0e-9 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double w = std::abs(d) >= halfWidth ? 0.0 : [&]
                {
                    const double a = juce::MathConstants<double>::pi * d / halfWidth; // Blackman-Harris, centred
                    return 0.35875 + 0.48829 * std::cos(a) + 0.14128 * std::cos(2.0 * a) + 0.01168 * std::cos(3.0 * a);
                }();
                row[i] = (float)(bandwidth * sinc * w);
                sum += row[i];
            }
            for (int i = 0; i < taps; ++i)
                row[i] = (float)(row[i] / sum); // unity gain at DC for every phase
        }

        for (auto &h : history)
            h.assign((size_t)(taps * 2), 0.0f);

        reset();
    }

    void StreamResampler::reset()
    {
        position = 0.0;
        writePos = 0;
        for (auto &h : history)
            std::fill(h.begin(), h.end(), 0.0f);
    }

    //==============================================================================
    // FrameRing
    //==============================================================================
    void FrameRing::setSize(int numChannels, int capacitySamples)
    {
        channels = numChannels;
        capacity = juce::jmax(1, capacitySamples);
        data.assign((size_t)(channels * capacity), 0.0f);
        clear();
    }

    void FrameRing::clear()
    {
        readPos = 0;
        count = 0;
    }

    void FrameRing::push(const float *frame) noexcept
    {
        if (count >= capacity)
            return;

        const int writePos = (readPos + count) % capacity;
        for (int ch = 0; ch < channels; ++ch)
            data[(size_t)(writePos * channels + ch)] = frame[ch];
        ++count;
    }

    void FrameRing::pushSilence(int numSamples) noexcept
    {
        const float silence[2] = {0.0f, 0.0f};
        for (int i = 0; i < numSamples; ++i)
            push(silence);
    }

    bool FrameRing::pop(float *frame) noexcept
    {
        if (count == 0)
            return false;

        for (int ch = 0; ch < channels; ++ch)
            frame[ch] = data[(size_t)(readPos * channels + ch)];
        readPos = (readPos + 1) % capacity;
        --count;
        return true;
    }

    //==============================================================================
    // Crossover
    //==============================================================================
    namespace
    {
        juce::dsp::ProcessSpec engineSpec(double sampleRate)
        {
            return {sampleRate, (juce::uint32)DenoiseEngine::frameSize, 2};
        }
    }

    void BandSplitter::prepare(double sampleRate)
    {
        for (auto &f : filters)
        {
            f.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            f.prepare(engineSpec(sampleRate));
        }
    }

    void BandSplitter::reset()
    {
        for (auto &f : filters)
            f.reset();
    }

    void BandSplitter::setCrossovers(const std::array<float, numCrossovers> &freqs)
    {
        for (size_t i = 0; i < filters.size(); ++i)
            filters[i].setCutoffFrequency(freqs[i]);
    }

    void BandSplitter::process(int channel, float input, std::array<float, numBands> &bands) noexcept
    {
        float rest = input;
        for (size_t i = 0; i < filters.size(); ++i)
        {
            float low = 0.0f, high = 0.0f;
            filters[i].processSample(channel, rest, low, high);
            bands[i] = low;
            rest = high;
        }
        bands[numBands - 1] = rest;
    }

    void BandSummer::prepare(double sampleRate)
    {
        for (auto &f : allpasses)
        {
            f.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
            f.prepare(engineSpec(sampleRate));
        }
    }

    void BandSummer::reset()
    {
        for (auto &f : allpasses)
            f.reset();
    }

    void BandSummer::setCrossovers(const std::array<float, numCrossovers> &freqs)
    {
        // Band k (0-based) needs the all-pass of every crossover above its own upper edge.
        for (size_t i = 0; i < allpasses.size(); ++i)
            allpasses[i].setCutoffFrequency(freqs[i + 1]);
    }

    float BandSummer::process(int channel, const std::array<float, numBands> &bandValues) noexcept
    {
        float acc = bandValues[0];
        for (size_t i = 0; i < allpasses.size(); ++i)
            acc = allpasses[i].processSample(channel, acc) + bandValues[i + 1];
        return acc + bandValues[numBands - 1];
    }

    //==============================================================================
    // DenoiseEngine
    //==============================================================================
    DenoiseEngine::DenoiseEngine() = default;

    DenoiseEngine::~DenoiseEngine()
    {
        destroyStates();
    }

    void DenoiseEngine::destroyStates()
    {
        for (auto &st : states)
        {
            if (st != nullptr)
                rnnoise_destroy(st);
            st = nullptr;
        }
    }

    void DenoiseEngine::prepare(double hostSampleRate, int maximumBlockSize, int numChannels)
    {
        hostRate = hostSampleRate > 0.0 ? hostSampleRate : engineRate;
        channels = juce::jlimit(1, 2, numChannels);
        resampling = std::abs(hostRate - engineRate) > 0.5;

        const double ratio = hostRate / engineRate;
        const int fifoLatency = resampling ? (int)std::ceil(frameSize * ratio) + 8 : frameSize;
        latencySamples = fifoLatency + (resampling ? (int)std::lround(frameSize * ratio) + measureResamplerDelay() : frameSize);

        if (resampling)
        {
            toEngine.prepare(hostRate, engineRate, channels);
            fromEngine.prepare(engineRate, hostRate, channels);
        }

        outputRing.setSize(channels, juce::jmax(64, maximumBlockSize) + fifoLatency + 2 * (int)std::ceil(frameSize * ratio) + 64);

        splitOriginal.prepare(engineRate);
        splitDenoised.prepare(engineRate);
        sumOutput.prepare(engineRate);
        sumOriginal.prepare(engineRate);
        sumMonitor.prepare(engineRate);
        activeCrossovers.fill(-1.0f);

        reset();
        outputRing.pushSilence(fifoLatency);
    }

    int DenoiseEngine::measureResamplerDelay() const
    {
        // Push broadband noise through a host -> 48 kHz -> host round trip and find the lag with
        // the best cross-correlation; this captures the windowed-sinc delays of both directions.
        StreamResampler down, up;
        down.prepare(hostRate, engineRate, 1);
        up.prepare(engineRate, hostRate, 1);

        constexpr int length = 8192, maxLag = 400;
        juce::Random random(0x5eed);
        std::vector<float> input((size_t)length), response;
        response.reserve((size_t)length * 2);
        for (auto &x : input)
        {
            x = random.nextFloat() * 2.0f - 1.0f;
            down.push(&x, [&](const float *y)
                      { up.push(y, [&](const float *z)
                                { response.push_back(*z); }); });
        }

        int bestLag = 0;
        double best = -1.0e30;
        for (int lag = 0; lag <= maxLag; ++lag)
        {
            double acc = 0.0;
            for (int i = maxLag; i < length && i + lag < (int)response.size(); ++i)
                acc += (double)input[(size_t)i] * response[(size_t)(i + lag)];
            if (acc > best)
            {
                best = acc;
                bestLag = lag;
            }
        }
        return bestLag;
    }

    void DenoiseEngine::reset()
    {
        destroyStates();
        {
            // RNNoise lazily initialises shared tables on first use; serialise creation across instances.
            static std::mutex creationLock;
            const std::lock_guard<std::mutex> lock(creationLock);
            for (int ch = 0; ch < 2; ++ch)
                states[(size_t)ch] = rnnoise_create(nullptr);
        }

        toEngine.reset();
        fromEngine.reset();
        outputRing.clear();
        for (auto *f : {&splitOriginal, &splitDenoised})
            f->reset();
        for (auto *s : {&sumOutput, &sumOriginal, &sumMonitor})
            s->reset();

        for (auto &f : frameIn)
            f.fill(0.0f);
        for (auto &f : previousIn)
            f.fill(0.0f);
        framePos = 0;
        lastWeights.fill(0.0f);
        gateGain[0] = gateGain[1] = 1.0f;
        gateHold[0] = gateHold[1] = 0;
        powerIn = powerOut = 0.0f;
        vadNow.store(0.0f);
        vadPeak.store(0.0f);
        gateGainNow.store(1.0f);
        gainReductionDb.store(0.0f);
    }

    void DenoiseEngine::process(juce::AudioBuffer<float> &buffer, const EngineSettings &settings)
    {
        current = settings;

        // Keep crossovers ordered and inside the audible range
        auto xo = settings.crossovers;
        xo[0] = juce::jlimit(30.0f, 16000.0f, xo[0]);
        for (size_t i = 1; i < xo.size(); ++i)
            xo[i] = juce::jlimit(xo[i - 1] * 1.05f, 20000.0f, xo[i]);

        if (xo != activeCrossovers)
        {
            activeCrossovers = xo;
            splitOriginal.setCrossovers(xo);
            splitDenoised.setCrossovers(xo);
            sumOutput.setCrossovers(xo);
            sumOriginal.setCrossovers(xo);
            sumMonitor.setCrossovers(xo);
        }

        const int numSamples = buffer.getNumSamples();
        const int bufferChannels = juce::jmin(buffer.getNumChannels(), channels);
        if (bufferChannels == 0)
            return;

        float frame[2] = {0.0f, 0.0f};

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
                frame[ch] = buffer.getSample(juce::jmin(ch, bufferChannels - 1), i);

            if (resampling)
                toEngine.push(frame, [this](const float *y)
                              { pushEngineSample(y); });
            else
                pushEngineSample(frame);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (!outputRing.pop(frame))
                frame[0] = frame[1] = 0.0f;

            for (int ch = 0; ch < bufferChannels; ++ch)
                buffer.setSample(ch, i, frame[ch]);
        }
    }

    void DenoiseEngine::pushEngineSample(const float *frame)
    {
        for (int ch = 0; ch < channels; ++ch)
            frameIn[(size_t)ch][(size_t)framePos] = frame[ch];

        if (++framePos == frameSize)
        {
            processFrame();
            framePos = 0;
        }
    }

    void DenoiseEngine::emitEngineSample(const float *frame)
    {
        if (resampling)
            fromEngine.push(frame, [this](const float *y)
                            { outputRing.push(y); });
        else
            outputRing.push(frame);
    }

    void DenoiseEngine::processFrame()
    {
        // 1. RNNoise works on 16-bit scaled floats
        std::array<std::array<float, frameSize>, 2> scaled;
        for (int ch = 0; ch < channels; ++ch)
            for (int k = 0; k < frameSize; ++k)
                scaled[(size_t)ch][(size_t)k] = frameIn[(size_t)ch][(size_t)k] * 32768.0f;

        float vadCh[2] = {0.0f, 0.0f};
        const bool linked = channels == 2 && current.linked;

        if (linked)
        {
            vadCh[0] = vadCh[1] = rnnoise_process_frame_linked(states[0], states[1],
                                                               denoised[0].data(), denoised[1].data(),
                                                               scaled[0].data(), scaled[1].data());
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
                vadCh[ch] = rnnoise_process_frame(states[(size_t)ch], denoised[(size_t)ch].data(), scaled[(size_t)ch].data());
        }

        const float vad = juce::jmax(vadCh[0], channels > 1 ? vadCh[1] : 0.0f);
        vadNow.store(vad, std::memory_order_relaxed);
        float previousPeak = vadPeak.load(std::memory_order_relaxed);
        while (vad > previousPeak && !vadPeak.compare_exchange_weak(previousPeak, vad, std::memory_order_relaxed))
        {
        }

        // 2. Band weights (ramped across the frame to avoid zipper noise)
        std::array<float, numBands> targetWeights;
        for (size_t b = 0; b < numBands; ++b)
            targetWeights[b] = current.bandBypass[b] ? 0.0f : current.amount * current.bandAmounts[b];

        // 3. Gate setup
        const float msToSamples = (float)engineRate * 0.001f;
        const float rangeGain = juce::Decibels::decibelsToGain(current.gateRangeDb, -100.0f);
        const float attackCoef = 1.0f - std::exp(-1.0f / (juce::jmax(0.1f, current.gateAttackMs) * msToSamples));
        const float releaseCoef = 1.0f - std::exp(-1.0f / (juce::jmax(1.0f, current.gateReleaseMs) * msToSamples));
        const int holdSamples = (int)(current.gateHoldMs * msToSamples);
        const float openThreshold = 1.0f - current.gateSensitivity;

        for (int ch = 0; ch < channels; ++ch)
            if ((linked ? vad : vadCh[ch]) >= openThreshold)
                gateHold[ch] = holdSamples + frameSize;

        // 4. Per-sample blend, gate and monitor routing
        const bool bandMonitoring = current.soloBand >= 0 || current.deltaBand >= 0;
        const float invScale = 1.0f / 32768.0f;
        const float invChannels = 1.0f / (float)channels;
        float frameIn2 = 0.0f, frameOut2 = 0.0f;
        std::array<float, numBands> orig{}, den{}, mix{}, mon{};
        std::array<float, numBands> weights{};
        float out[2] = {0.0f, 0.0f};

        for (int k = 0; k < frameSize; ++k)
        {
            const float alpha = (float)(k + 1) / (float)frameSize;
            for (size_t b = 0; b < numBands; ++b)
                weights[b] = lastWeights[b] + (targetWeights[b] - lastWeights[b]) * alpha;

            float monoIn = 0.0f, monoOut = 0.0f;

            for (int ch = 0; ch < channels; ++ch)
            {
                const float o = previousIn[(size_t)ch][(size_t)k];
                const float d = denoised[(size_t)ch][(size_t)k] * invScale;
                splitOriginal.process(ch, o, orig);
                splitDenoised.process(ch, d, den);

                float gateTarget = 1.0f;
                if (current.gateOn)
                {
                    if (gateHold[ch] > 0)
                        --gateHold[ch];
                    else
                        gateTarget = rangeGain;
                }
                gateGain[ch] += (gateTarget - gateGain[ch]) * (gateTarget > gateGain[ch] ? attackCoef : releaseCoef);
                const float g = gateGain[ch];

                for (size_t b = 0; b < numBands; ++b)
                    mix[b] = (orig[b] + weights[b] * (den[b] - orig[b])) * g;

                const float outFull = sumOutput.process(ch, mix);
                const float origFull = sumOriginal.process(ch, orig);

                if (bandMonitoring)
                {
                    for (size_t b = 0; b < numBands; ++b)
                    {
                        if (current.deltaBand >= 0)
                        {
                            mon[b] = (int)b == current.deltaBand ? orig[b] - mix[b] : 0.0f;
                            continue;
                        }

                        const bool include = (int)b == current.soloBand;
                        switch (current.monitor)
                        {
                        case MonitorMode::original:
                            mon[b] = include ? orig[b] : 0.0f;
                            break;
                        case MonitorMode::removed:
                            mon[b] = include ? orig[b] - mix[b] : 0.0f;
                            break;
                        default:
                            mon[b] = include ? mix[b] : 0.0f;
                            break;
                        }
                    }
                    out[ch] = sumMonitor.process(ch, mon);
                }
                else
                {
                    switch (current.monitor)
                    {
                    case MonitorMode::original:
                        out[ch] = origFull;
                        break;
                    case MonitorMode::removed:
                        out[ch] = origFull - outFull;
                        break;
                    default:
                        out[ch] = outFull;
                        break;
                    }
                }

                frameIn2 += origFull * origFull;
                frameOut2 += outFull * outFull;
                monoIn += origFull;
                monoOut += outFull;
            }

            analyser.push(monoIn * invChannels, monoOut * invChannels, (monoIn - monoOut) * invChannels);
            emitEngineSample(out);
        }

        lastWeights = targetWeights;
        previousIn = frameIn;

        // 5. Measured gain reduction: output vs input power, ~300 ms averaging
        const float smoothing = 1.0f - std::exp(-(float)(frameSize / engineRate) / 0.3f);
        const float norm = 1.0f / (float)(frameSize * channels);
        powerIn += (frameIn2 * norm - powerIn) * smoothing;
        powerOut += (frameOut2 * norm - powerOut) * smoothing;

        float reduction = 0.0f;
        if (powerIn > 1.0e-10f)
            reduction = juce::jlimit(-60.0f, 0.0f, 10.0f * std::log10((powerOut + 1.0e-12f) / (powerIn + 1.0e-12f)));
        gainReductionDb.store(reduction, std::memory_order_relaxed);
        gateGainNow.store(juce::jmin(gateGain[0], channels > 1 ? gateGain[1] : gateGain[0]), std::memory_order_relaxed);
    }

    //==============================================================================
    // SafetyLimiter
    //==============================================================================
    void SafetyLimiter::prepare(double sampleRate, int numChannels)
    {
        channels = juce::jlimit(1, 2, numChannels);
        lookahead = juce::jmax(1, (int)std::lround(sampleRate * 0.001));
        attackCoef = 1.0f - std::exp(-1.0f / (0.25f * (float)lookahead));
        releaseCoef = 1.0f - std::exp(-1.0f / (0.08f * (float)sampleRate));
        delay.assign((size_t)(channels * lookahead), 0.0f);
        peakWindow.assign((size_t)lookahead, 0.0f);
        reset();
    }

    void SafetyLimiter::reset()
    {
        std::fill(delay.begin(), delay.end(), 0.0f);
        std::fill(peakWindow.begin(), peakWindow.end(), 0.0f);
        writePos = 0;
        gain = 1.0f;
        reductionDb.store(0.0f);
    }

    void SafetyLimiter::process(juce::AudioBuffer<float> &buffer, bool enabled, float ceilingDb) noexcept
    {
        const float ceiling = juce::Decibels::decibelsToGain(ceilingDb);
        const int numCh = juce::jmin(buffer.getNumChannels(), channels);
        float minGain = 1.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax(peak, std::abs(buffer.getSample(ch, i)));

            peakWindow[(size_t)writePos] = peak;
            const float windowMax = *std::max_element(peakWindow.begin(), peakWindow.end());
            const float target = (enabled && windowMax > ceiling) ? ceiling / windowMax : 1.0f;
            gain += (target - gain) * (target < gain ? attackCoef : releaseCoef);
            minGain = juce::jmin(minGain, gain);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto &slot = delay[(size_t)(ch * lookahead + writePos)];
                const float delayed = slot;
                slot = buffer.getSample(ch, i);
                float y = delayed * gain;
                if (enabled)
                    y = juce::jlimit(-ceiling, ceiling, y);
                buffer.setSample(ch, i, y);
            }

            writePos = (writePos + 1) % lookahead;
        }

        reductionDb.store(juce::Decibels::gainToDecibels(minGain, -60.0f), std::memory_order_relaxed);
    }
}
