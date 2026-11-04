#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace si
{
/** Lock-free mono sample ring written by the audio thread, read by the UI. */
class AnalyzerFifo
{
public:
    static constexpr int kSize = 8192;
    void push (const float* l, const float* r, int n)
    {
        int w = write.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) w] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
            w = (w + 1) & (kSize - 1);
        }
        write.store (w, std::memory_order_release);
    }
    /** Copies the most recent `n` samples (n <= kSize). */
    void latest (float* dest, int n) const
    {
        const int w = write.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) dest[i] = buf[(size_t) ((w - n + i) & (kSize - 1))];
    }
private:
    std::array<float, kSize> buf {};
    std::atomic<int> write { 0 };
};

/** UI-side FFT with smoothing; query in dB at any frequency. */
class Spectrum
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder;
    Spectrum() : fft (kOrder), window (kN, juce::dsp::WindowingFunction<float>::hann, true) { smooth.fill (-120.0f); }

    void update (const AnalyzerFifo& fifo, double sampleRate)
    {
        sr = sampleRate;
        std::array<float, kN * 2> data {};
        fifo.latest (data.data(), kN);
        window.multiplyWithWindowingTable (data.data(), kN);
        fft.performFrequencyOnlyForwardTransform (data.data(), true);
        for (int i = 0; i < kN / 2; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (data[(size_t) i] * 4.0f / (float) kN, -120.0f);
            float& s = smooth[(size_t) i];
            s = db > s ? s + 0.6f * (db - s) : s + 0.12f * (db - s);
        }
    }

    float dbAt (float f) const
    {
        const float bin = f / (float) sr * (float) kN;
        const int b0 = juce::jlimit (1, kN / 2 - 2, (int) bin);
        const float fr = juce::jlimit (0.0f, 1.0f, bin - (float) b0);
        // average a few bins at high frequencies so the line is not jagged
        const int span = juce::jmax (0, (int) (bin * 0.02f));
        float acc = 0.0f;
        int cnt = 0;
        for (int k = -span; k <= span; ++k)
        {
            const int i = juce::jlimit (1, kN / 2 - 2, b0 + k);
            acc += smooth[(size_t) i] + fr * (smooth[(size_t) i + 1] - smooth[(size_t) i]);
            ++cnt;
        }
        // +3 dB/oct tilt so pink-ish material looks flat, like most pro analyzers
        return acc / (float) cnt + 3.0f * std::log2 (juce::jmax (20.0f, f) / 1000.0f);
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, kN / 2> smooth {};
    double sr = 48000.0;
};
} // namespace si
