// AvaruusradioFilter.h — formant filter that mumbles like a badly tuned
// radio station. Inspired by the "space radio" sound (Paratek, Oxymoron...).
// Header-only, no JUCE dependency. C++17.
//
// Signal flow:
//   input -> [consonant noise] -> 4 parallel formant bandpasses (vowel morph)
//         -> syllable envelope -> [radio: SSB mistune, band-limit, fading,
//            static, heterodyne whistle, detector grit] -> output
//
// Main controls:
//   setBabble(0..1)  "Mutina":  0 = static vowel (setVowel),
//                               1 = random speech-like vowel/syllable stream
//   setRadio(0..1)   "Kuuluvuus": 0 = clean, 1 = distant, drifting station
// Extra:
//   setVowel(0..1)   manual vowel position  a-ä-e-i-y-ö-o-u (Finnish vowels)
//   setRate(Hz)      syllables per second (~2..12)
//   setSharpness(0..1) formant resonance
//   setSeed(n)       different "speaker"
//
// Feed it something buzzy (saw, pulse, noise). Works per sample, mono.

#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace kh
{
namespace ar
{
constexpr double kPi = 3.14159265358979323846;

struct Rng // xorshift32: realtime-safe, deterministic
{
    uint32_t s = 0x12345678u;
    void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double uni() { return (next() >> 8) * (1.0 / 16777216.0); }      // 0..1
    double bi()  { return 2.0 * uni() - 1.0; }                        // -1..1
};

struct Svf // TPT state-variable filter
{
    double g = 0.1, k = 1.0, s1 = 0.0, s2 = 0.0;
    void set(double fc, double q, double fs)
    {
        g = std::tan(kPi * std::clamp(fc, 10.0, 0.45 * fs) / fs);
        k = 1.0 / std::max(q, 0.05);
    }
    void reset() { s1 = s2 = 0.0; }
    // returns {lp, bp, hp}
    void tick(double x, double& lp, double& bp, double& hp)
    {
        hp = (x - (k + g) * s1 - s2) / (1.0 + k * g + g * g);
        const double v1 = g * hp; bp = v1 + s1; s1 = bp + v1;
        const double v2 = g * bp; lp = v2 + s2; s2 = lp + v2;
    }
};

struct OnePole // simple smoother / lowpass
{
    double a = 0.0, y = 0.0;
    void setTime(double seconds, double fs) { a = std::exp(-1.0 / std::max(1e-6, seconds * fs)); }
    double tick(double x) { y = x + a * (y - x); return y; }
};

// 90-degree phase splitter (Olli Niemitalo's allpass pairs) for SSB shifting.
class Hilbert
{
public:
    void reset() { for (auto& st : stA) st = {}; for (auto& st : stB) st = {}; aDelay = 0.0; }
    void tick(double x, double& re, double& im)
    {
        const double a = chain(x, stA, cA);
        re = aDelay; aDelay = a;           // path A gets one sample delay
        im = chain(x, stB, cB);
    }
private:
    struct St { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static constexpr std::array<double, 4> cA { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    static constexpr std::array<double, 4> cB { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
    std::array<St, 4> stA{}, stB{};
    double aDelay = 0.0;
    static double chain(double x, std::array<St, 4>& st, const std::array<double, 4>& c)
    {
        for (int i = 0; i < 4; ++i)
        {
            const double a2 = c[i] * c[i];
            const double y = a2 * (x + st[i].y2) - st[i].x2;
            st[i].x2 = st[i].x1; st[i].x1 = x;
            st[i].y2 = st[i].y1; st[i].y1 = y;
            x = y;
        }
        return x;
    }
};
} // namespace ar

class AvaruusradioFilter
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        glide.setTime(0.04, fs);
        sylEnv.setTime(0.012, fs);
        fadeSmooth.setTime(0.35, fs);
        driftSmooth.setTime(1.5, fs);
        whistleSmooth.setTime(2.5, fs);
        crackleLp.setTime(0.0003, fs);
        reset();
    }

    void reset()
    {
        for (auto& f : formants) f.reset();
        bandHp.reset(); bandLp.reset(); consonantBp.reset();
        hilbert.reset();
        rng.seed(seed);
        glide.y = vowel; sylEnv.y = 1.0; fadeSmooth.y = 1.0;
        driftSmooth.y = 0.0; whistleSmooth.y = 1500.0; crackleLp.y = 0.0;
        sylCounter = 0; sylLength = 1; target = vowel; ctrlCounter = 0;
        shiftPhase = whistlePhase = 0.0; consonantLeft = 0; pause = false;
        speakerScale = 1.0; accent = 1.0; fadeTarget = 1.0; driftTarget = 0.0; whistleTarget = 1500.0;
        updateFormants(vowel);
        updateRadioFilters();
    }

    void setVowel(double v)     { vowel = std::clamp(v, 0.0, 1.0); }
    void setBabble(double b)    { babble = std::clamp(b, 0.0, 1.0); }
    void setRadio(double r)     { radio = std::clamp(r, 0.0, 1.0); updateRadioFilters(); }
    void setRate(double hz)     { rate = std::clamp(hz, 0.5, 20.0); }
    void setSharpness(double s) { sharp = std::clamp(s, 0.0, 1.0); }
    void setSeed(uint32_t s)    { seed = s; rng.seed(s); }

    float process(float inF)
    {
        const double in = inF;

        // ---------------- control-rate stuff (every 16 samples) ----------
        if (ctrlCounter-- <= 0)
        {
            ctrlCounter = 15;
            updateFormants(glide.y);
            // radio: slow random targets for fading / drift / whistle
            if (rng.uni() < 16.0 / fs * 1.5) fadeTarget = 1.0 - radio * (0.3 + 0.7 * rng.uni()) * (rng.uni() < 0.25 ? 1.0 : 0.6);
            if (rng.uni() < 16.0 / fs * 0.4) driftTarget = rng.bi();
            if (rng.uni() < 16.0 / fs * 0.3) whistleTarget = 600.0 + 2600.0 * rng.uni();
        }

        // ---------------- syllable clock -------------------------------
        if (++sylCounter >= sylLength) newSyllable();
        const double phase = double(sylCounter) / sylLength;

        // vowel position: manual <-> random target
        const double vpos = vowel + babble * (target - vowel);
        glide.tick(vpos);

        // syllable amplitude shape: quick attack, sag at the end, gaps
        double shape = std::min(1.0, phase * 12.0) * (1.0 - 0.6 * phase * phase);
        if (pause) shape = 0.0;
        const double envTarget = 1.0 - babble + babble * shape * accent;
        const double env = sylEnv.tick(envTarget);

        // consonant: short noise burst at the start of some syllables
        double exc = in;
        if (consonantLeft > 0)
        {
            --consonantLeft;
            double lp, bp, hp;
            consonantBp.tick(rng.bi(), lp, bp, hp);
            const double level = babble * 0.35 * (consonantLeft / (double)consonantLen);
            exc += level * (consonantHiss ? hp : bp) * (0.3 + std::abs(in) * 2.0);
        }

        // ---------------- formant bank ---------------------------------
        double voice = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            double lp, bp, hp;
            formants[i].tick(exc, lp, bp, hp);
            voice += formantGain[i] * formants[i].k * bp; // unity-peak bandpass
        }
        voice *= env * 1.6;

        if (radio <= 0.0) return (float)voice;

        // ---------------- radio --------------------------------------
        // 1) SSB mistuning: frequency shift (Donald-Duck drift)
        const double drift = driftSmooth.tick(driftTarget);
        const double shiftHz = radio * radio * 90.0 * drift;
        double re, im;
        hilbert.tick(voice, re, im);
        shiftPhase += 2.0 * ar::kPi * shiftHz / fs;
        if (shiftPhase > ar::kPi) shiftPhase -= 2.0 * ar::kPi;
        if (shiftPhase < -ar::kPi) shiftPhase += 2.0 * ar::kPi;
        const double shifted = re * std::cos(shiftPhase) - im * std::sin(shiftPhase);
        double y = voice + std::min(1.0, radio * 1.6) * (shifted - voice);

        // 2) band-limit (AM/shortwave receiver)
        double lp, bp, hp;
        bandHp.tick(y, lp, bp, hp); y = hp;
        bandLp.tick(y, lp, bp, hp); y = lp;

        // 3) fading
        y *= fadeSmooth.tick(fadeTarget);

        // 4) detector grit: soft asymmetric clip
        const double drive = 1.0 + 4.0 * radio;
        y = (std::tanh(drive * y + 0.1 * radio) - std::tanh(0.1 * radio)) / drive * 1.3;

        // 5) static: hiss + crackle
        const double hiss = rng.bi() * 0.02 * radio;
        double crackle = 0.0;
        if (rng.uni() < radio * radio * 0.0015) crackle = rng.bi() * 0.6 * radio;
        crackle = crackleLp.tick(crackle) * 3.0 + crackle * 0.3;
        // 6) heterodyne whistle, gliding slowly
        const double wf = whistleSmooth.tick(whistleTarget);
        whistlePhase += 2.0 * ar::kPi * wf / fs;
        if (whistlePhase > 2.0 * ar::kPi) whistlePhase -= 2.0 * ar::kPi;
        const double whistle = std::sin(whistlePhase) * 0.025 * radio * radio;

        return (float)(y + hiss + crackle + whistle);
    }

private:
    double fs = 48000.0;
    double vowel = 0.0, babble = 0.0, radio = 0.0, rate = 5.0, sharp = 0.6;
    uint32_t seed = 1;
    ar::Rng rng;

    // vowel table (approx. adult male formants, Hz): a ä e i y ö o u
    static constexpr int NV = 8;
    static constexpr double F[NV][4] = {
        { 730, 1150, 2500, 3500 },  // a
        { 660, 1600, 2500, 3500 },  // ä
        { 450, 1900, 2550, 3500 },  // e
        { 290, 2250, 3000, 3700 },  // i
        { 300, 1750, 2200, 3300 },  // y
        { 430, 1500, 2350, 3300 },  // ö
        { 450,  800, 2450, 3400 },  // o
        { 330,  700, 2350, 3300 },  // u
    };
    std::array<ar::Svf, 4> formants;
    static constexpr std::array<double, 4> formantGain { 1.0, 0.6, 0.3, 0.15 };
    static constexpr std::array<double, 4> baseQ { 6.0, 9.0, 12.0, 14.0 };

    ar::OnePole glide, sylEnv, fadeSmooth, driftSmooth, whistleSmooth, crackleLp;
    ar::Svf bandHp, bandLp, consonantBp;
    ar::Hilbert hilbert;

    int sylCounter = 0, sylLength = 1, ctrlCounter = 0, consonantLeft = 0, consonantLen = 1;
    bool pause = false, consonantHiss = false;
    double target = 0.0, accent = 1.0, speakerScale = 1.0;
    double shiftPhase = 0.0, whistlePhase = 0.0;
    double fadeTarget = 1.0, driftTarget = 0.0, whistleTarget = 1500.0;
    int wordLeft = 0;

    void newSyllable()
    {
        sylCounter = 0;
        sylLength = std::max(1, (int)(fs / rate * (0.6 + 0.8 * rng.uni())));

        // words of 1..4 syllables with short gaps between them
        if (wordLeft <= 0)
        {
            wordLeft = 1 + (int)(rng.uni() * 4.0);
            pause = rng.uni() < 0.35;
            if (rng.uni() < 0.2) speakerScale = 0.9 + 0.25 * rng.uni(); // new phrase / speaker
            if (pause) return;
        }
        pause = false;
        --wordLeft;

        // next vowel: mostly nearby, sometimes a jump
        target = (rng.uni() < 0.6) ? std::clamp(target + 0.3 * rng.bi(), 0.0, 1.0) : rng.uni();
        accent = 0.55 + 0.45 * rng.uni();

        // consonant at the onset (~60 %): hissy (s, f) or plosive-ish (t, k)
        if (rng.uni() < 0.6)
        {
            consonantHiss = rng.uni() < 0.5;
            consonantLen = std::max(1, (int)(fs * (consonantHiss ? 0.06 : 0.015)));
            consonantLeft = consonantLen;
            consonantBp.set(consonantHiss ? 5000.0 : 1500.0 + 2000.0 * rng.uni(), 1.2, fs);
        }
    }

    void updateFormants(double pos)
    {
        const double x = std::clamp(pos, 0.0, 1.0) * (NV - 1);
        const int i0 = std::min((int)x, NV - 2);
        const double t = x - i0;
        for (int k = 0; k < 4; ++k)
        {
            const double f = (F[i0][k] * (1.0 - t) + F[i0 + 1][k] * t) * speakerScale;
            const double q = baseQ[k] * (0.4 + 1.6 * sharp);
            formants[k].set(f, q, fs);
        }
    }

    void updateRadioFilters()
    {
        bandHp.set(20.0 + 330.0 * radio, 0.7, fs);
        bandLp.set(18000.0 * std::pow(2600.0 / 18000.0, radio), 0.9, fs);
    }
};

} // namespace kh
