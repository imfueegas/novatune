#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace novatune
{

// Real-time pitch shifter for monophonic sources (voice).
//
// Two read heads sweep a delay line with a half-window offset and are cross-faded with
// a sin^2 window. The window length is kept at 2*k periods of the detected pitch, so the
// two heads are in phase and the cross-fade doesn't comb-filter voiced material.
// When the requested shift is ~0 the output is a clean delayed copy of the input
// (no processing at all). The latency is constant: getLatencySamples().
class PitchShifter
{
public:
    static constexpr int maxChannels = 8;

    void prepare (double sampleRate, int numChannels)
    {
        fs = (float) sampleRate;
        channels = std::max (1, std::min (numChannels, maxChannels));

        windowMin = 0.008f * fs;
        windowMax = 0.028f * fs;
        centre = (int) std::ceil (windowMax * 0.5f) + 4;

        const int need = 2 * centre + 16;
        size = 1;
        while (size < need) size <<= 1;
        mask = (uint32_t) (size - 1);

        buf.assign ((size_t) channels, std::vector<float> ((size_t) size, 0.f));
        fadeStep = 1.f / (0.010f * fs);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f);
        writeIdx = 0;
        phase = 0.5f;
        bypassMix = 1.f;
        bypassed = true;
        ratio = 1.f;
        periodUsed = 0.f;
        window = windowTarget = 0.016f * fs;
    }

    int getLatencySamples() const { return centre; }

    // Pitch ratio: 1 = unchanged, 2 = one octave up.
    void setRatio (float r) { ratio = r; }

    // Period (in samples) of the input pitch, used to size the cross-fade window.
    void setPeriod (float periodSamples)
    {
        if (periodSamples < 2.f) return;

        // Only re-size the window when the pitch has moved noticeably. Following every
        // small variation (vibrato) would itself modulate the pitch of the output.
        if (periodUsed > 0.f && std::fabs (periodSamples / periodUsed - 1.f) < 0.05f)
            return;
        periodUsed = periodSamples;

        const float halfTarget = 0.0075f * fs;
        const float k = std::min (8.f, std::max (1.f, std::round (halfTarget / periodSamples)));
        windowTarget = std::min (windowMax, std::max (windowMin, 2.f * k * periodSamples));
    }

    // One sample for every channel. in/out hold `channels` values.
    void process (const float* in, float* out)
    {
        for (int c = 0; c < channels; ++c)
            buf[(size_t) c][writeIdx & mask] = in[c];

        // Window length: follows the target. Moving it changes the delay of the heads, so
        // while we're running through the shifter it changes slowly (a few cents at most);
        // while resting in bypass it can jump, nobody can hear it.
        {
            const float diffW = windowTarget - window;
            const float step = (bypassMix >= 0.999f) ? diffW : std::min (maxWindowStep, std::max (-maxWindowStep, diffW));
            window += step;
        }

        // Bypass with hysteresis, cross-faded.
        const float dev = std::fabs (ratio - 1.f);
        if (bypassed) { if (dev > exitDev) bypassed = false; }
        else          { if (dev < enterDev) bypassed = true; }

        const float bTarget = bypassed ? 1.f : 0.f;
        bypassMix += std::min (fadeStep, std::max (-fadeStep, bTarget - bypassMix));

        // Read heads.
        const float pA = phase;
        float pB = phase + 0.5f;
        if (pB >= 1.f) pB -= 1.f;

        const float dA = (float) centre + (pA - 0.5f) * window;
        const float dB = (float) centre + (pB - 0.5f) * window;

        const float sA = std::sin (3.14159265f * pA);
        const float gA = sA * sA;
        const float gB = 1.f - gA;

        for (int c = 0; c < channels; ++c)
        {
            const float wet = gA * readCubic (c, dA) + gB * readCubic (c, dB);
            const float dry = buf[(size_t) c][(writeIdx - (uint32_t) centre) & mask];
            out[c] = wet + bypassMix * (dry - wet);
        }

        // Advance.
        if (bypassMix >= 1.f)
        {
            phase = 0.5f;   // rest position: head A alone, at exactly `centre` samples of delay
        }
        else
        {
            phase += (1.f - ratio) / window;
            phase -= std::floor (phase);
        }

        ++writeIdx;
    }

private:
    float readCubic (int c, float delay) const
    {
        const int   i    = (int) delay;
        const float t    = delay - (float) i;
        const auto& b    = buf[(size_t) c];
        const uint32_t base = writeIdx - (uint32_t) i;

        const float ym1 = b[(base + 1) & mask];
        const float y0  = b[base & mask];
        const float y1  = b[(base - 1) & mask];
        const float y2  = b[(base - 2) & mask];

        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    static constexpr float enterDev = 0.0017f;   // ~3 cents
    static constexpr float exitDev  = 0.0035f;   // ~6 cents
    static constexpr float maxWindowStep = 0.004f;

    float fs = 48000.f;
    int channels = 1, centre = 0, size = 0;
    uint32_t mask = 0, writeIdx = 0;
    std::vector<std::vector<float>> buf;

    float windowMin = 0.f, windowMax = 0.f, window = 0.f, windowTarget = 0.f;
    float phase = 0.5f, ratio = 1.f, bypassMix = 1.f, fadeStep = 0.01f, periodUsed = 0.f;
    bool bypassed = true;
};

} // namespace novatune
