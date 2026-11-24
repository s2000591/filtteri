// KhFilters.h — character filters for Kulmahiomakone & friends.
// Header-only, no JUCE dependency. C++17.
//
//   KatkarapuFilter  — inspired by Crazy Chicken "Shrimpy":
//       multimode (LP/BP/HP at once) state-variable core, resonance voiced
//       toward the midrange, distortion INSIDE the core (states are shaped),
//       which turns into a folding, "rustling" edge at high Drive.
//
//   VillapaitaFilter — inspired by Dwyfor Tech "Pas-Isel":
//       4-pole lowpass made of four independent poles, each with its own
//       diode-style nonlinear element and slight mismatch (Buchla-style
//       per-pole buffering), soft-clipping diodes in the resonance loop
//       (Korg MS style). Low input gain = polite and clean,
//       high input gain = biting and woolly.
//
// Neither module has a public schematic; these are character models built
// from the makers' descriptions, not circuit emulations.
//
// Both run 2x oversampled internally (halfband FIR). Latency is reported
// by latencySamples(). Call setters per block (or smoothed per sample).

#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace kh
{
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// 2x oversampler: windowed-sinc halfband FIR for both up and down.
class Oversampler2x
{
public:
    static constexpr int N = 31; // taps (odd)

    Oversampler2x()
    {
        const int M = N - 1;
        double sum = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const double t = n - M / 2.0;
            const double sinc = (t == 0.0) ? 0.5 : std::sin(kPi * 0.5 * t) / (kPi * t);
            const double w = 0.42 - 0.5 * std::cos(2 * kPi * n / M) + 0.08 * std::cos(4 * kPi * n / M);
            h[n] = sinc * w;
            sum += h[n];
        }
        for (auto& c : h) c /= sum; // unity DC gain
        reset();
    }

    void reset() { up.fill(0.0); dn.fill(0.0); upPos = dnPos = 0; }

    static constexpr int latency() { return (N - 1) / 2; } // at base rate (up+down)

    // one base-rate sample in -> two high-rate samples out
    void upsample(double x, double& a, double& b)
    {
        push(up, upPos, 2.0 * x); a = conv(up, upPos);
        push(up, upPos, 0.0);     b = conv(up, upPos);
    }

    // two high-rate samples in -> one base-rate sample out
    double downsample(double a, double b)
    {
        push(dn, dnPos, a);
        push(dn, dnPos, b);
        return conv(dn, dnPos);
    }

private:
    std::array<double, N> h{};
    std::array<double, N> up{}, dn{};
    int upPos = 0, dnPos = 0;

    static void push(std::array<double, N>& buf, int& pos, double v)
    {
        pos = (pos + 1) % N;
        buf[pos] = v;
    }
    double conv(const std::array<double, N>& buf, int pos) const
    {
        double acc = 0.0;
        for (int i = 0; i < N; ++i)
        {
            acc += h[i] * buf[pos];
            pos = (pos == 0) ? N - 1 : pos - 1;
        }
        return acc;
    }
};

// ---------------------------------------------------------------------------
// ~8 Hz DC blocker (the asymmetric shaping creates some DC).
struct DcBlock
{
    double x1 = 0.0, y1 = 0.0, R = 0.999;
    void prepare(double fs) { R = 1.0 - 2.0 * kPi * 8.0 / fs; x1 = y1 = 0.0; }
    double process(double x) { const double y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
};

// ---------------------------------------------------------------------------
class KatkarapuFilter
{
public:
    struct Out { float lp, bp, hp; };

    void prepare(double sampleRate)
    {
        fs2 = 2.0 * sampleRate;
        for (auto* d : { &dcLp, &dcBp, &dcHp }) d->prepare(sampleRate);
        reset(); updateCoeffs();
    }
    void reset() { s1 = s2 = 0.0; osLp.reset(); osBp.reset(); osHp.reset(); osIn.reset(); }
    int  latencySamples() const { return Oversampler2x::latency(); }

    void setCutoff(double hz)    { cutoff = hz;  updateCoeffs(); }
    void setResonance(double r)  { res = std::clamp(r, 0.0, 1.0); updateCoeffs(); }
    void setDrive(double d)      { drive = std::clamp(d, 0.0, 1.0); updateCoeffs(); }

    Out process(float in)
    {
        double a, b;
        osIn.upsample(in, a, b);
        double lpA, bpA, hpA, lpB, bpB, hpB;
        tick(a, lpA, bpA, hpA);
        tick(b, lpB, bpB, hpB);
        return { (float)dcLp.process(osLp.downsample(lpA, lpB)),
                 (float)dcBp.process(osBp.downsample(bpA, bpB)),
                 (float)dcHp.process(osHp.downsample(hpA, hpB)) };
    }

private:
    double fs2 = 96000.0, cutoff = 1000.0, res = 0.3, drive = 0.2;
    double g = 0.0, k = 2.0, lim = 3.0, fold = 0.0, bias = 0.0, inGain = 1.0;
    double s1 = 0.0, s2 = 0.0;
    Oversampler2x osIn, osLp, osBp, osHp;
    DcBlock dcLp, dcBp, dcHp;

    void updateCoeffs()
    {
        const double fc = std::clamp(cutoff, 10.0, 0.45 * fs2);
        g = std::tan(kPi * fc / fs2);

        // Resonance voiced toward the midrange: full strength around ~900 Hz,
        // tapering off toward the lows and highs (log-frequency bell).
        const double oct = std::log2(fc / 900.0);
        const double voice = 0.5 + 0.5 * std::exp(-(oct * oct) / (2.0 * 1.7 * 1.7));
        const double r = res * voice;
        k = std::max(-0.08, 2.0 * (1.0 - 1.03 * r)); // k = 1/Q; <0 = self-osc (midrange only)

        // In-core distortion: lower state ceiling = more crunch.
        lim    = 3.0 / (1.0 + 7.0 * drive * drive);
        fold   = std::clamp((drive - 0.6) / 0.4, 0.0, 1.0); // folding/rustle at top
        bias   = 0.15 * drive;                               // asymmetry -> even harmonics
        inGain = 1.0 + 1.5 * drive;
    }

    // State shaper: tanh-ish clip that morphs into a sine fold.
    double shape(double u) const
    {
        // Both curves are normalised to unity slope at zero, so small
        // signals pass the core untouched and only big swings get shaped.
        const double x = u / lim;
        const double tb = std::tanh(bias);
        const double clip = (std::tanh(x + bias) - tb) / (1.0 - tb * tb);
        if (fold <= 0.0) return lim * clip;
        const double fld = (std::sin(1.6 * x + bias) - std::sin(bias)) / (1.6 * std::cos(bias));
        return lim * ((1.0 - fold) * clip + fold * fld);
    }

    void tick(double x, double& lp, double& bp, double& hp)
    {
        x *= inGain;
        hp = (x - (k + g) * s1 - s2) / (1.0 + k * g + g * g);
        const double v1 = g * hp;
        bp = v1 + s1;
        s1 = shape(bp + v1);          // distortion lives in the core
        const double v2 = g * bp;
        lp = v2 + s2;
        s2 = shape(lp + v2);

        const double out = 1.0 / std::sqrt(inGain);
        lp *= out; bp *= out; hp *= out;
    }
};

// ---------------------------------------------------------------------------
class VillapaitaFilter
{
public:
    void prepare(double sampleRate) { fs2 = 2.0 * sampleRate; dc.prepare(sampleRate); reset(); updateCoeffs(); }
    void reset() { s.fill(0.0); osIn.reset(); osOut.reset(); }
    int  latencySamples() const { return Oversampler2x::latency(); }

    void setCutoff(double hz)     { cutoff = hz; updateCoeffs(); }
    void setResonance(double r)   { res = std::clamp(r, 0.0, 1.0); updateCoeffs(); }
    // 0 = attenuated & polite, ~0.5 = unity, 1 = hot & biting
    void setInputGain(double g01) { gain01 = std::clamp(g01, 0.0, 1.0); updateCoeffs(); }

    float process(float in)
    {
        double a, b;
        osIn.upsample(in, a, b);
        const double ya = tick(a);
        const double yb = tick(b);
        return (float)dc.process(osOut.downsample(ya, yb));
    }

private:
    double fs2 = 96000.0, cutoff = 1000.0, res = 0.3, gain01 = 0.5;
    double k = 0.0, inGain = 1.0, outGain = 1.0;
    std::array<double, 4> G{}, s{};
    // Per-pole mismatch: each pole is its own little circuit.
    static constexpr std::array<double, 4> detune { 1.000, 1.013, 0.988, 1.006 };
    static constexpr std::array<double, 4> knee   { 2.40, 2.70, 2.20, 2.50 };
    Oversampler2x osIn, osOut;
    DcBlock dc;

    void updateCoeffs()
    {
        for (int i = 0; i < 4; ++i)
        {
            const double fc = std::clamp(cutoff * detune[i], 10.0, 0.45 * fs2);
            const double g = std::tan(kPi * fc / fs2);
            G[i] = g / (1.0 + g);
        }
        k = 4.25 * res;                                    // ~4 = self-oscillation
        inGain  = 0.25 * std::pow(24.0, gain01);           // 0.25x ... 6x
        outGain = 1.0 / std::pow(inGain, 0.75);
    }

    // Diode-pair control element: softer, rounder knee than tanh.
    static double diode(double u, double a)
    {
        const double x = u / a;
        return a * x / std::sqrt(1.0 + x * x);
    }

    // Soft-clipping diodes in the resonance loop (antiparallel pair).
    static double resClip(double y)
    {
        const double t = 0.6;
        const double ay = std::abs(y);
        if (ay <= t) return y;
        const double over = ay - t;
        return std::copysign(t + over / (1.0 + 2.5 * over), y);
    }

    double tick(double x)
    {
        x *= inGain;

        // Zero-delay-feedback estimate of the 4th pole (linear prediction).
        const double G4 = G[0] * G[1] * G[2] * G[3];
        const double S = G[1] * G[2] * G[3] * (1 - G[0]) * s[0]
                       + G[2] * G[3] * (1 - G[1]) * s[1]
                       + G[3] * (1 - G[2]) * s[2]
                       + (1 - G[3]) * s[3];
        const double y4est = (G4 * x + S) / (1.0 + k * G4);

        // Resonance feedback through clipping diodes, with a little bass
        // make-up so it stays chunky ("woolly") rather than thin.
        double u = x * (1.0 + 0.35 * k) - k * resClip(y4est);
        u = diode(u, 3.0);   // op-amp headroom: keeps the core bounded

        for (int i = 0; i < 4; ++i)
        {
            // Each pole's diode element shapes its own input and state
            // (Huovilainen-style), so hot signals add harmonics ("bite").
            const double v = G[i] * (diode(u, knee[i]) - diode(s[i], knee[i]));
            const double y = v + s[i];
            s[i] = y + v;
            u = y;
        }
        // Hot signals push the output buffer into its own (slightly
        // asymmetric) diode knee after the poles — that's the bite.
        const double y = diode(u + 0.08, 0.9) - diode(0.08, 0.9);
        return y * outGain * 1.15;
    }
};

} // namespace kh
