#pragma once
#include <array>
#include <initializer_list>

namespace novatune
{

enum ScaleType
{
    Chromatic = 0,
    Major,
    Minor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Mixolydian,
    PentatonicMajor,
    PentatonicMinor,
    Blues,
    Custom,
    NumScales
};

inline const char* scaleName (int s)
{
    static const char* names[] = { "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
                                   "Dorian", "Mixolydian", "Pentatonic Major", "Pentatonic Minor",
                                   "Blues", "Custom" };
    return names[(s >= 0 && s < NumScales) ? s : 0];
}

inline const char* noteName (int pitchClass)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pitchClass % 12) + 12) % 12];
}

// Bitmask of the semitones above the root that belong to the scale.
inline unsigned scaleIntervals (int s)
{
    auto m = [] (std::initializer_list<int> l)
    {
        unsigned r = 0;
        for (int i : l) r |= 1u << i;
        return r;
    };

    switch (s)
    {
        case Major:           return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case Minor:           return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case HarmonicMinor:   return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case MelodicMinor:    return m ({ 0, 2, 3, 5, 7, 9, 11 });
        case Dorian:          return m ({ 0, 2, 3, 5, 7, 9, 10 });
        case Mixolydian:      return m ({ 0, 2, 4, 5, 7, 9, 10 });
        case PentatonicMajor: return m ({ 0, 2, 4, 7, 9 });
        case PentatonicMinor: return m ({ 0, 3, 5, 7, 10 });
        case Blues:           return m ({ 0, 3, 5, 6, 7, 10 });
        case Chromatic:
        default:              return 0xFFFu;
    }
}

// allowed[pitchClass] is true if that note (C = 0 ... B = 11) is a valid target.
inline std::array<bool, 12> buildMask (int key, int scale, const std::array<bool, 12>& custom)
{
    std::array<bool, 12> out {};
    const unsigned iv = scaleIntervals (scale);

    for (int pc = 0; pc < 12; ++pc)
    {
        if (scale == Custom)
            out[(size_t) pc] = custom[(size_t) pc];
        else
            out[(size_t) pc] = ((iv >> (((pc - key) % 12 + 12) % 12)) & 1u) != 0;
    }
    return out;
}

} // namespace novatune
