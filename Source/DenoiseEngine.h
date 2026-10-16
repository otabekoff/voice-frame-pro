#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

struct DenoiseState;

namespace vfp
{
    //==============================================================================
    /** Number of crossover bands used for the frequency-dependent blend. */
    static constexpr int numBands = 6;
    static constexpr int numCrossovers = numBands - 1;

    enum class MonitorMode
    {
        original = 0,
        processed,
        removed
    };

    /** Snapshot of everything the engine needs for one block, read from the APVTS on the audio thread. */
    struct EngineSettings
    {
        float amount = 0.5f; // 0..1, global processing intensity
        std::array<float, numCrossovers> crossovers{120.0f, 400.0f, 1500.0f, 4000.0f, 9000.0f};
        std::array<float, numBands> bandAmounts{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}; // 0..1
        std::array<bool, numBands> bandBypass{};
        int soloBand = -1;  // -1 = off
        int deltaBand = -1; // -1 = off, else listen to that band's removed signal
        MonitorMode monitor = MonitorMode::processed;
        bool linked = true;

        bool gateOn = false;
        float gateSensitivity = 0.5f; // 0..1
        float gateRangeDb = -18.0f;
        float gateAttackMs = 5.0f;
        float gateHoldMs = 120.0f;
        float gateReleaseMs = 180.0f;
    };

    //==============================================================================
    /**
     * Single-producer / single-consumer FIFO carrying the analyser's three mono signals
     * (input, output, removed) from the audio thread to the editor at the engine rate.
     */
    class AnalyserFifo
    {
    public:
        static constexpr int capacity = 1 << 15;

        void push(float in, float out, float removed) noexcept
        {
            const auto scope = fifo.write(1);
            if (scope.blockSize1 > 0)
                write(scope.startIndex1, in, out, removed);
            else if (scope.blockSize2 > 0)
                write(scope.startIndex2, in, out, removed);
        }

        /** Reads up to maxSamples into the three destination arrays; returns the count read. */
        int pull(float *in, float *out, float *removed, int maxSamples) noexcept
        {
            const auto scope = fifo.read(juce::jmin(maxSamples, fifo.getNumReady()));
            int n = 0;
            auto copy = [&](int start, int size)
            {
                for (int i = 0; i < size; ++i, ++n)
                {
                    in[n] = bufIn[(size_t)(start + i)];
                    out[n] = bufOut[(size_t)(start + i)];
                    removed[n] = bufRem[(size_t)(start + i)];
                }
            };
            copy(scope.startIndex1, scope.blockSize1);
            copy(scope.startIndex2, scope.blockSize2);
            return n;
        }

    private:
        void write(int index, float in, float out, float removed) noexcept
        {
            bufIn[(size_t)index] = in;
            bufOut[(size_t)index] = out;
            bufRem[(size_t)index] = removed;
        }

        juce::AbstractFifo fifo{capacity};
        std::array<float, capacity> bufIn{}, bufOut{}, bufRem{};
    };

    //==============================================================================
    /**
     * Streaming multichannel resampler: windowed-sinc (Blackman-Harris) interpolation from a
     * phase table, band-limited just below the lower of the two Nyquist frequencies so it also
     * acts as the anti-alias filter. Produces output samples as input samples are pushed, so its
     * latency is fixed.
     */
    class StreamResampler
    {
    public:
        void prepare(double inputRate, double outputRate, int numChannels);
        void reset();

        template <typename Emit>
        void push(const float *frame, Emit &&emit)
        {
            // Mirrored ring: history[writePos + 1 .. writePos + taps] is always contiguous, oldest first
            writePos = (writePos + 1) % taps;
            for (int ch = 0; ch < channels; ++ch)
            {
                auto &h = history[(size_t)ch];
                h[(size_t)writePos] = h[(size_t)(writePos + taps)] = frame[ch];
            }

            while (position < 1.0)
            {
                const float phase = (float)position * (float)phases;
                const int p0 = juce::jmin((int)phase, phases - 1);
                const float frac = phase - (float)p0;
                const float *k0 = table.data() + (size_t)p0 * (size_t)taps;
                const float *k1 = k0 + taps;

                for (int ch = 0; ch < channels; ++ch)
                {
                    const float *x = history[(size_t)ch].data() + writePos + 1;
                    float a = 0.0f, b = 0.0f;
                    for (int i = 0; i < taps; ++i)
                    {
                        a += x[i] * k0[i];
                        b += x[i] * k1[i];
                    }
                    scratch[(size_t)ch] = a + (b - a) * frac;
                }
                emit(scratch.data());
                position += step;
            }
            position -= 1.0;
        }

    private:
        static constexpr int phases = 256;

        int channels = 0, taps = 32, writePos = 0;
        double step = 1.0;
        double position = 0.0;
        std::vector<float> table; // (phases + 1) rows of `taps` coefficients
        std::array<std::vector<float>, 2> history;
        std::array<float, 2> scratch{};
    };

    //==============================================================================
    /** Plain single-threaded ring buffer of multichannel samples (host rate, engine output side). */
    class FrameRing
    {
    public:
        void setSize(int numChannels, int capacitySamples);
        void clear();
        void push(const float *frame) noexcept;
        void pushSilence(int numSamples) noexcept;
        bool pop(float *frame) noexcept;
        int size() const noexcept { return count; }

    private:
        int channels = 0, capacity = 0, readPos = 0, count = 0;
        std::vector<float> data;
    };

    //==============================================================================
    /**
     * Sums six crossover-band signals phase-coherently. Lower bands are passed through the
     * Linkwitz-Riley all-pass sections of the higher crossovers, so that the sum of unity
     * weights is an all-pass copy of the input.
     */
    class BandSummer
    {
    public:
        void prepare(double sampleRate);
        void reset();
        void setCrossovers(const std::array<float, numCrossovers> &freqs);
        float process(int channel, const std::array<float, numBands> &bandValues) noexcept;

    private:
        std::array<juce::dsp::LinkwitzRileyFilter<float>, numCrossovers - 1> allpasses;
    };

    /** Splits a signal into six Linkwitz-Riley bands (cascade topology). */
    class BandSplitter
    {
    public:
        void prepare(double sampleRate);
        void reset();
        void setCrossovers(const std::array<float, numCrossovers> &freqs);
        void process(int channel, float input, std::array<float, numBands> &bands) noexcept;

    private:
        std::array<juce::dsp::LinkwitzRileyFilter<float>, numCrossovers> filters;
    };

    //==============================================================================
    /**
     * RNNoise-based voice denoiser.
     *
     * Host audio is resampled to 48 kHz (RNNoise's native rate), cut into 480-sample frames
     * and processed once full-band by RNNoise. The original (delayed by one frame to line up
     * with RNNoise's own latency) and the denoised frames are then split by the crossover and
     * blended per band, an optional VAD-driven gate is applied, and the selected monitor
     * signal is resampled back to the host rate.
     */
    class DenoiseEngine
    {
    public:
        static constexpr int frameSize = 480;
        static constexpr double engineRate = 48000.0;

        DenoiseEngine();
        ~DenoiseEngine();

        void prepare(double hostSampleRate, int maximumBlockSize, int numChannels);
        void reset();

        /** Latency introduced by this engine, in host samples. */
        int getLatencySamples() const noexcept { return latencySamples; }

        void process(juce::AudioBuffer<float> &buffer, const EngineSettings &settings);

        // Telemetry, safe to read from any thread
        float getVoiceProbability() const noexcept { return vadNow.load(std::memory_order_relaxed); }
        float takeVoiceProbabilityPeak() noexcept { return vadPeak.exchange(0.0f, std::memory_order_relaxed); }
        float getGateGain() const noexcept { return gateGainNow.load(std::memory_order_relaxed); }
        float getGainReductionDb() const noexcept { return gainReductionDb.load(std::memory_order_relaxed); }

        AnalyserFifo &getAnalyserFifo() noexcept { return analyser; }

    private:
        void pushEngineSample(const float *frame);
        void processFrame();
        void emitEngineSample(const float *frame);
        void destroyStates();
        int measureResamplerDelay() const;

        double hostRate = engineRate;
        int channels = 2;
        int latencySamples = 0;
        bool resampling = false;

        std::array<DenoiseState *, 2> states{};
        StreamResampler toEngine, fromEngine;
        FrameRing outputRing;

        std::array<std::array<float, frameSize>, 2> frameIn{};
        std::array<std::array<float, frameSize>, 2> previousIn{};
        std::array<std::array<float, frameSize>, 2> denoised{};
        int framePos = 0;

        BandSplitter splitOriginal, splitDenoised;
        BandSummer sumOutput, sumOriginal, sumMonitor;
        std::array<float, numCrossovers> activeCrossovers{};

        std::array<float, numBands> lastWeights{};
        float gateGain[2]{1.0f, 1.0f};
        int gateHold[2]{0, 0};
        float powerIn = 0.0f, powerOut = 0.0f;

        EngineSettings current;

        std::atomic<float> vadNow{0.0f}, vadPeak{0.0f}, gateGainNow{1.0f}, gainReductionDb{0.0f};
        AnalyserFifo analyser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DenoiseEngine)
    };

    //==============================================================================
    /**
     * Safety peak limiter with a short look-ahead. The look-ahead delay is always in the
     * signal path so reported latency stays constant when the limiter is toggled.
     */
    class SafetyLimiter
    {
    public:
        void prepare(double sampleRate, int numChannels);
        void reset();
        int getLatencySamples() const noexcept { return lookahead; }

        void process(juce::AudioBuffer<float> &buffer, bool enabled, float ceilingDb) noexcept;

        float getGainReductionDb() const noexcept { return reductionDb.load(std::memory_order_relaxed); }

    private:
        int lookahead = 48, channels = 2, writePos = 0;
        float attackCoef = 0.0f, releaseCoef = 0.0f, gain = 1.0f;
        std::vector<float> delay;     // channels * lookahead
        std::vector<float> peakWindow; // lookahead
        std::atomic<float> reductionDb{0.0f};
    };
}
