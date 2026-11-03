#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace novatune
{

// Monophonic pitch detector based on YIN (de Cheveigne & Kawahara, 2002).
// Works on a decimated copy of the input (~24 kHz) to keep the CPU cost low.
// No allocations after prepare(), safe to call push() from the audio thread.
class PitchDetector
{
public:
    struct Result
    {
        float frequency  = 0.f;   // Hz, only meaningful when voiced
        float confidence = 0.f;   // 0..1
        bool  voiced     = false;
    };

    void prepare (double sampleRate)
    {
        sr     = sampleRate;
        decim  = std::max (1, (int) std::lround (sampleRate / 24000.0));
        fsA    = sampleRate / decim;
        tauMin = std::max (2, (int) std::floor (fsA / maxHz));
        tauMax = (int) std::ceil (fsA / minHz);
        frameLen = window + tauMax;

        ringSize = 1;
        while (ringSize < frameLen + 8) ringSize <<= 1;

        ring.assign ((size_t) ringSize, 0.f);
        frame.assign ((size_t) frameLen, 0.f);
        diff.assign ((size_t) tauMax + 2, 0.f);
        cmnd.assign ((size_t) tauMax + 2, 1.f);

        hpR = (float) (1.0 - 2.0 * 3.14159265358979 * 50.0 / fsA);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.f);
        writePos = 0;
        hopCounter = 0;
        accum = 0.f;
        accumCount = 0;
        hpX = hpY = 0.f;
        result = {};
    }

    double getHopSeconds() const { return (double) hop * decim / sr; }

    // Feed one input sample. Returns true when a new analysis result is available.
    bool push (float x)
    {
        accum += x;
        if (++accumCount < decim)
            return false;

        const float s = accum / (float) decim;
        accum = 0.f;
        accumCount = 0;

        const float y = s - hpX + hpR * hpY;   // remove DC / rumble
        hpX = s;
        hpY = y;

        ring[writePos & (uint32_t) (ringSize - 1)] = y;
        ++writePos;

        if (++hopCounter < hop)
            return false;

        hopCounter = 0;
        analyse();
        return true;
    }

    const Result& getResult() const { return result; }

private:
    void analyse()
    {
        const uint32_t m = (uint32_t) (ringSize - 1);
        const uint32_t start = writePos - (uint32_t) frameLen;
        for (int j = 0; j < frameLen; ++j)
            frame[(size_t) j] = ring[(start + (uint32_t) j) & m];

        result = {};

        // Energy gate on the most recent samples.
        double e = 0.0;
        for (int j = tauMax; j < frameLen; ++j)
            e += (double) frame[(size_t) j] * frame[(size_t) j];
        if (std::sqrt (e / window) < gateRms)
            return;

        // Difference function. The reference window is the NEWEST `window` samples and it is
        // compared against the same window `tau` samples earlier, so the estimate describes
        // the most recent signal (lowest possible lag).
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            const float* a = frame.data() + tauMax;
            const float* b = frame.data() + tauMax - tau;
            for (int j = 0; j < window; ++j)
            {
                const double d = (double) a[j] - (double) b[j];
                sum += d * d;
            }
            diff[(size_t) tau] = (float) sum;
        }

        // Cumulative mean normalised difference.
        cmnd[0] = 1.f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 1e-12 ? (float) (diff[(size_t) tau] * tau / running) : 1.f;
        }

        // Absolute threshold: first dip below the threshold, followed down to its minimum.
        int tau = -1;
        for (int t = tauMin; t < tauMax; ++t)
        {
            if (cmnd[(size_t) t] < threshold)
            {
                while (t + 1 < tauMax && cmnd[(size_t) t + 1] < cmnd[(size_t) t])
                    ++t;
                tau = t;
                break;
            }
        }

        if (tau < 0)
        {
            int best = tauMin;
            for (int t = tauMin; t < tauMax; ++t)
                if (cmnd[(size_t) t] < cmnd[(size_t) best]) best = t;

            if (cmnd[(size_t) best] >= fallbackThreshold)
                return;
            tau = best;
        }

        // Parabolic interpolation for sub-sample accuracy.
        float better = (float) tau;
        if (tau > 1 && tau < tauMax)
        {
            const float s0 = cmnd[(size_t) tau - 1], s1 = cmnd[(size_t) tau], s2 = cmnd[(size_t) tau + 1];
            const float denom = s0 + s2 - 2.f * s1;
            if (std::fabs (denom) > 1e-9f)
                better = (float) tau + 0.5f * (s0 - s2) / denom;
        }

        const float freq = (float) (fsA / better);
        if (freq < minHz || freq > maxHz)
            return;

        result.frequency  = freq;
        result.confidence = 1.f - cmnd[(size_t) tau];
        result.voiced     = true;
    }

    static constexpr float minHz = 70.f;
    static constexpr float maxHz = 1100.f;
    static constexpr int   window = 512;     // analysis window (decimated samples)
    static constexpr int   hop    = 128;     // new estimate every `hop` decimated samples
    static constexpr float threshold = 0.15f;
    static constexpr float fallbackThreshold = 0.30f;
    static constexpr float gateRms = 0.0025f; // about -52 dBFS

    double sr = 48000.0, fsA = 24000.0;
    int decim = 2, tauMin = 2, tauMax = 300, frameLen = 0, ringSize = 0;
    std::vector<float> ring, frame, diff, cmnd;
    uint32_t writePos = 0;
    int hopCounter = 0, accumCount = 0;
    float accum = 0.f, hpX = 0.f, hpY = 0.f, hpR = 0.99f;
    Result result;
};

} // namespace novatune
