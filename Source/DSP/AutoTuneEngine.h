#pragma once
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "Scales.h"
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace novatune
{

struct EngineParams
{
    int   key            = 0;              // 0..11 (C..B)
    int   scale          = Chromatic;      // ScaleType
    std::array<bool, 12> customNotes {};   // used when scale == Custom
    float retuneMs       = 25.f;           // 0 = hard "robot" tuning, 400 = very natural
    float flexCents      = 0.f;            // dead zone around the note: keeps natural vibrato
    float amount         = 1.f;            // 0..1 correction strength
    float transposeSemis = 0.f;            // extra shift applied after tuning
    float referenceA     = 440.f;

    EngineParams() { customNotes.fill (true); }
};

class AutoTuneEngine
{
public:
    static constexpr int maxChannels = PitchShifter::maxChannels;

    struct UiState
    {
        bool  voiced = false;
        float detectedMidi = 0.f;   // pitch of the input (MIDI note number, fractional)
        float outputMidi   = 0.f;   // pitch of the output
    };

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = std::max (1, std::min (numChannels, maxChannels));
        detector.prepare (sampleRate);
        shifter.prepare (sampleRate, channels);
        hopSeconds = detector.getHopSeconds();
        setParams (params);
        reset();
    }

    void reset()
    {
        detector.reset();
        shifter.reset();
        shift = targetShift = 0.f;
        voicedActive = false;
        histValid = false;
        framesUnvoiced = 0;
        currentNote = -1000;
        lastMidi = 0.f;
    }

    int getLatencySamples() const { return shifter.getLatencySamples(); }

    void setParams (const EngineParams& p)
    {
        params = p;
        allowed = buildMask (p.key, p.scale, p.customNotes);
        anyAllowed = std::any_of (allowed.begin(), allowed.end(), [] (bool b) { return b; });

        const double tau = std::max (2.0, (double) p.retuneMs) * 1e-3;
        alphaVoiced   = (float) (1.0 - std::exp (-1.0 / (tau * sr)));
        alphaUnvoiced = (float) (1.0 - std::exp (-1.0 / (0.025 * sr)));
    }

    // In-place processing. `channels` are the audio buffers of the host.
    void process (float* const* ch, int numCh, int numSamples)
    {
        numCh = std::min (numCh, channels);
        if (numCh <= 0) return;

        float in[maxChannels] {}, out[maxChannels] {};
        const float inv = 1.f / (float) numCh;

        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.f;
            for (int c = 0; c < numCh; ++c)
            {
                in[c] = ch[c][i];
                mono += in[c];
            }

            if (detector.push (mono * inv))
                handleDetection (detector.getResult());

            shift += (voicedActive ? alphaVoiced : alphaUnvoiced) * (targetShift - shift);
            shifter.setRatio (std::exp2 (shift * (1.f / 12.f)));
            shifter.process (in, out);

            for (int c = 0; c < numCh; ++c)
                ch[c][i] = out[c];
        }

        uiVoiced.store (voicedActive, std::memory_order_relaxed);
        uiDetected.store (lastMidi, std::memory_order_relaxed);
        uiOutput.store (lastMidi + shift, std::memory_order_relaxed);
    }

    UiState getUiState() const
    {
        return { uiVoiced.load (std::memory_order_relaxed),
                 uiDetected.load (std::memory_order_relaxed),
                 uiOutput.load (std::memory_order_relaxed) };
    }

private:
    void handleDetection (const PitchDetector::Result& r)
    {
        const float transpose = params.transposeSemis;

        if (!r.voiced)
        {
            if (voicedActive && ++framesUnvoiced * hopSeconds > holdSeconds)
            {
                // Consonant / silence: stop correcting, keep only the transposition.
                voicedActive = false;
                histValid = false;
                currentNote = -1000;
                targetShift = clampShift (transpose);
            }
            return;
        }

        framesUnvoiced = 0;
        voicedActive = true;

        const float midi = 69.f + 12.f * std::log2 (r.frequency / params.referenceA);
        if (!histValid)
        {
            hist.fill (midi);
            histValid = true;
        }
        else
        {
            hist[0] = hist[1];
            hist[1] = hist[2];
            hist[2] = midi;
        }

        // The median only rejects outliers (octave slips, glitches). Smooth movement, like
        // vibrato, goes through untouched so the correction doesn't lag behind the audio.
        const float median = median3 (hist[0], hist[1], hist[2]);
        const float med = std::fabs (midi - median) < 0.7f ? midi : median;
        lastMidi = med;
        shifter.setPeriod ((float) sr / r.frequency);

        float correction = 0.f;
        if (anyAllowed)
        {
            const int note = chooseNote (med);
            const float err = med - (float) note;
            const float flex = params.flexCents * 0.01f;
            correction = (std::min (flex, std::max (-flex, err)) - err) * params.amount;
        }

        targetShift = clampShift (correction + transpose);
    }

    int chooseNote (float midi)
    {
        const int base = (int) std::floor (midi);
        int best = -1000;
        float bestDist = 1e9f;

        for (int m = base - 6; m <= base + 7; ++m)
        {
            if (!allowed[(size_t) (((m % 12) + 12) % 12)]) continue;
            const float d = std::fabs (midi - (float) m);
            if (d < bestDist) { bestDist = d; best = m; }
        }

        // Hysteresis: keep the current note until the new candidate is clearly closer.
        if (currentNote > -1000 && allowed[(size_t) (((currentNote % 12) + 12) % 12)]
            && std::fabs (midi - (float) currentNote) <= bestDist + noteHysteresis)
            return currentNote;

        currentNote = best;
        return best;
    }

    static float median3 (float a, float b, float c)
    {
        return std::max (std::min (a, b), std::min (std::max (a, b), c));
    }

    static float clampShift (float s) { return std::min (12.f, std::max (-12.f, s)); }

    static constexpr double holdSeconds = 0.060;
    static constexpr float  noteHysteresis = 0.15f;

    double sr = 48000.0, hopSeconds = 0.005;
    int channels = 1;

    EngineParams params;
    std::array<bool, 12> allowed {};
    bool anyAllowed = true;

    PitchDetector detector;
    PitchShifter shifter;

    float shift = 0.f, targetShift = 0.f;
    float alphaVoiced = 0.01f, alphaUnvoiced = 0.001f;
    bool voicedActive = false, histValid = false;
    int framesUnvoiced = 0, currentNote = -1000;
    std::array<float, 3> hist {};
    float lastMidi = 0.f;

    std::atomic<bool>  uiVoiced { false };
    std::atomic<float> uiDetected { 0.f }, uiOutput { 0.f };
};

} // namespace novatune
