// Offline tests for the NovaTune DSP engine (no JUCE needed).
// Build:  g++ -O2 -std=c++17 -ISource Tests/EngineTest.cpp -o engine_test && ./engine_test
#include "DSP/AutoTuneEngine.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <vector>

using namespace novatune;

static const double PI = 3.14159265358979323846;
static int failures = 0;

static void check (bool ok, const char* what, double value, const char* unit = "")
{
    std::printf ("  [%s] %-58s %10.3f %s\n", ok ? "OK  " : "FAIL", what, value, unit);
    if (! ok) ++failures;
}

// Harmonic-rich, voice-like signal. freqAt(t) gives the instantaneous frequency.
static std::vector<float> makeVoice (double fs, double seconds, const std::function<double (double)>& freqAt, float amp = 0.3f)
{
    std::vector<float> x ((size_t) (fs * seconds));
    double phi = 0.0;
    for (size_t n = 0; n < x.size(); ++n)
    {
        const double t = (double) n / fs;
        phi += 2.0 * PI * freqAt (t) / fs;
        double s = 0.0;
        for (int k = 1; k <= 8; ++k) s += std::sin (k * phi) / k;
        x[n] = (float) (amp * s / 1.8);
    }
    return x;
}

static std::vector<float> run (const std::vector<float>& in, double fs, const EngineParams& p, int blockSize = 256)
{
    AutoTuneEngine e;
    e.setParams (p);
    e.prepare (fs, 1);
    std::vector<float> out (in);
    for (size_t pos = 0; pos < out.size(); pos += (size_t) blockSize)
    {
        float* ptr = out.data() + pos;
        const int n = (int) std::min ((size_t) blockSize, out.size() - pos);
        e.process (&ptr, 1, n);
    }
    return out;
}

// Frequency by normalised autocorrelation, searching only around `expected` Hz.
static double measurePitch (const float* x, int n, double fs, double expected)
{
    const int lo = (int) (fs / (expected * 1.3)), hi = (int) (fs / (expected * 0.77));
    std::vector<double> r ((size_t) hi + 2, 0.0);
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (int i = 0; i + lag < n; ++i) { xy += (double) x[i] * x[i + lag]; xx += (double) x[i] * x[i]; yy += (double) x[i + lag] * x[i + lag]; }
        r[(size_t) lag] = (xx > 0 && yy > 0) ? xy / std::sqrt (xx * yy) : 0.0;
    }
    int best = lo;
    for (int lag = lo; lag <= hi; ++lag) if (r[(size_t) lag] > r[(size_t) best]) best = lag;
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const double denom = a + c - 2 * b;
    const double lag = best + (std::fabs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0);
    return fs / lag;
}

static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

static double rms (const float* x, int n)
{
    double s = 0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / n);
}

static double db (double r) { return 20.0 * std::log10 (r); }

// Pitch contour (cents relative to `ref`) in 2048-sample windows.
static std::vector<double> contour (const std::vector<float>& x, double fs, double expected, double fromSec, double toSec)
{
    std::vector<double> c;
    for (size_t pos = (size_t) (fromSec * fs); pos + 2048 < (size_t) (toSec * fs); pos += 512)
        c.push_back (cents (measurePitch (x.data() + pos, 2048, fs, expected), expected));
    return c;
}

static double stddev (const std::vector<double>& v)
{
    double m = 0; for (double d : v) m += d; m /= (double) v.size();
    double s = 0; for (double d : v) s += (d - m) * (d - m);
    return std::sqrt (s / (double) v.size());
}

int main()
{
    // 1. Hard tuning to the nearest chromatic note, several sample rates.
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("\n1. Chromatic, retune 0 ms, input 215 Hz (-40 cents from A3), fs = %.0f\n", fs);
        auto in = makeVoice (fs, 4.0, [] (double) { return 215.0; });
        EngineParams p; p.retuneMs = 0.f;
        auto out = run (in, fs, p);
        const int from = (int) (2.5 * fs), n = (int) (1.2 * fs);
        const double f = measurePitch (out.data() + from, n, fs, 220.0);
        check (std::fabs (cents (f, 220.0)) < 3.0, "output pitch error vs A3 (220 Hz)", cents (f, 220.0), "cents");
        const double r = db (rms (out.data() + from, n) / rms (in.data() + from, n));
        check (std::fabs (r) < 1.5, "level change", r, "dB");

        // Amplitude steadiness (20 ms RMS windows)
        const int w = (int) (0.02 * fs);
        double mn = 1e9, mx = 0;
        for (int pos = from; pos + w < from + n; pos += w / 2) { const double v = rms (out.data() + pos, w); mn = std::min (mn, v); mx = std::max (mx, v); }
        check (db (mx / mn) < 2.5, "max/min short-term level (steadiness)", db (mx / mn), "dB");
    }

    // 2. Scale: C major, input C#4 + 20 cents -> must go to D4.
    {
        const double fs = 48000.0;
        std::printf ("\n2. C major, input 280.4 Hz (C#4 + 20 cents) -> D4 (293.66 Hz)\n");
        auto in = makeVoice (fs, 4.0, [] (double) { return 277.18 * std::pow (2.0, 20.0 / 1200.0); });
        EngineParams p; p.retuneMs = 0.f; p.key = 0; p.scale = Major;
        auto out = run (in, fs, p);
        const double f = measurePitch (out.data() + (int) (2.5 * fs), (int) (1.2 * fs), fs, 293.66);
        check (std::fabs (cents (f, 293.66)) < 3.0, "output pitch error vs D4", cents (f, 293.66), "cents");
    }

    // 3. Already in tune -> untouched (bypass path).
    {
        const double fs = 48000.0;
        std::printf ("\n3. Input exactly 220 Hz (in tune)\n");
        auto in = makeVoice (fs, 3.0, [] (double) { return 220.0; });
        EngineParams p; p.retuneMs = 0.f;
        auto out = run (in, fs, p);
        const int from = (int) (1.5 * fs), n = (int) (1.0 * fs);
        const double f = measurePitch (out.data() + from, n, fs, 220.0);
        check (std::fabs (cents (f, 220.0)) < 1.0, "output pitch error", cents (f, 220.0), "cents");
        check (std::fabs (db (rms (out.data() + from, n) / rms (in.data() + from, n))) < 0.5, "level change", db (rms (out.data() + from, n) / rms (in.data() + from, n)), "dB");
    }

    // 4. Slow retune: should glide, not jump.
    {
        const double fs = 48000.0;
        std::printf ("\n4. Retune 400 ms, input 215 Hz -> A3\n");
        auto in = makeVoice (fs, 5.0, [] (double) { return 215.0; });
        EngineParams p; p.retuneMs = 400.f;
        auto out = run (in, fs, p);
        const double early = cents (measurePitch (out.data() + (int) (0.35 * fs), (int) (0.2 * fs), fs, 218.0), 215.0);
        const double late  = cents (measurePitch (out.data() + (int) (4.0 * fs), (int) (0.8 * fs), fs, 220.0), 220.0);
        check (early > 5.0 && early < 38.0, "still gliding at ~0.45 s (cents moved from 215 Hz)", early, "cents");
        check (std::fabs (late) < 3.0, "arrived at A3 after 4 s", late, "cents");
    }

    // 5. Vibrato: hard tune flattens it, flex-tune keeps it.
    {
        const double fs = 48000.0;
        std::printf ("\n5. Vibrato +/-30 cents @ 5.5 Hz around A3\n");
        auto vib = [] (double t) { return 220.0 * std::pow (2.0, 30.0 * std::sin (2 * PI * 5.5 * t) / 1200.0); };
        auto in = makeVoice (fs, 5.0, vib);
        const double sdIn = stddev (contour (in, fs, 220.0, 2.0, 4.5));

        EngineParams hard; hard.retuneMs = 0.f;
        const double sdHard = stddev (contour (run (in, fs, hard), fs, 220.0, 2.0, 4.5));

        EngineParams flex; flex.retuneMs = 0.f; flex.flexCents = 50.f;
        const double sdFlex = stddev (contour (run (in, fs, flex), fs, 220.0, 2.0, 4.5));

        std::printf ("    input vibrato std: %.1f cents\n", sdIn);
        check (sdHard < 0.35 * sdIn, "hard tune: vibrato std (cents)", sdHard, "cents");
        check (sdFlex > 0.7 * sdIn && sdFlex < 1.4 * sdIn, "flex-tune 50 ct: vibrato std preserved", sdFlex, "cents");
    }

    // 6. Unvoiced material (noise) passes through at the same level, no NaN/inf.
    {
        const double fs = 48000.0;
        std::printf ("\n6. White noise (unvoiced)\n");
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> d (-0.2f, 0.2f);
        std::vector<float> in ((size_t) (3 * fs));
        for (auto& s : in) s = d (rng);
        EngineParams p; p.retuneMs = 0.f;
        auto out = run (in, fs, p);
        bool finite = true;
        for (float s : out) if (! std::isfinite (s) || std::fabs (s) > 1.0f) finite = false;
        check (finite, "all samples finite and within +/-1", finite ? 1.0 : 0.0);
        const int from = (int) (1.0 * fs), n = (int) (1.5 * fs);
        const double r = db (rms (out.data() + from, n) / rms (in.data() + from, n));
        check (std::fabs (r) < 1.5, "noise level change", r, "dB");
    }

    // 7. Voice with silence and note changes: no NaN/clicks.
    {
        const double fs = 48000.0;
        std::printf ("\n7. Melody with gaps (A3 -> C4 -> silence -> E4), C major\n");
        auto in = makeVoice (fs, 6.0, [] (double t) { return t < 2.0 ? 213.0 : (t < 4.0 ? 258.0 : 335.0); });
        for (size_t n = (size_t) (3.5 * fs); n < (size_t) (4.2 * fs); ++n) in[n] = 0.f;  // silence gap
        EngineParams p; p.retuneMs = 10.f; p.key = 0; p.scale = Major;
        auto out = run (in, fs, p);
        double maxStep = 0; bool finite = true;
        for (size_t n = 1; n < out.size(); ++n)
        {
            if (! std::isfinite (out[n])) finite = false;
            maxStep = std::max (maxStep, (double) std::fabs (out[n] - out[n - 1]));
        }
        check (finite, "all samples finite", finite ? 1.0 : 0.0);
        check (maxStep < 0.45, "largest sample-to-sample jump (click check)", maxStep);
        const double f2 = measurePitch (out.data() + (int) (1.5 * fs), (int) (0.4 * fs), fs, 220.0);
        const double f3 = measurePitch (out.data() + (int) (3.0 * fs), (int) (0.4 * fs), fs, 261.63);
        const double f5 = measurePitch (out.data() + (int) (5.3 * fs), (int) (0.5 * fs), fs, 329.63);
        check (std::fabs (cents (f2, 220.00)) < 4.0, "segment 1 -> A3", cents (f2, 220.00), "cents");
        check (std::fabs (cents (f3, 261.63)) < 4.0, "segment 2 -> C4", cents (f3, 261.63), "cents");
        check (std::fabs (cents (f5, 329.63)) < 4.0, "segment 3 -> E4", cents (f5, 329.63), "cents");
    }

    // 8. Stereo: both channels identical when fed identical input.
    {
        const double fs = 48000.0;
        std::printf ("\n8. Stereo coherence\n");
        auto in = makeVoice (fs, 2.0, [] (double) { return 215.0; });
        std::vector<float> L (in), R (in);
        AutoTuneEngine e; EngineParams p; p.retuneMs = 0.f; e.setParams (p); e.prepare (fs, 2);
        for (size_t pos = 0; pos < L.size(); pos += 256)
        {
            float* ch[2] = { L.data() + pos, R.data() + pos };
            e.process (ch, 2, (int) std::min<size_t> (256, L.size() - pos));
        }
        double maxDiff = 0; for (size_t n = 0; n < L.size(); ++n) maxDiff = std::max (maxDiff, (double) std::fabs (L[n] - R[n]));
        check (maxDiff < 1e-6, "max |L - R|", maxDiff);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
