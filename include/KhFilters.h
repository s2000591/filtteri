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
// prepare(sampleRate, false) skips the internal oversampling (zero latency):
// use it when the host code already runs at an oversampled rate.
// set(cutoff, resonance, drive/gain) updates all at once (one coefficient pass).

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

    void prepare(double sampleRate, bool oversample = true)
    {
        os = oversample;
        fs2 = (os ? 2.0 : 1.0) * sampleRate;
        for (auto* d : { &dcLp, &dcBp, &dcHp }) d->prepare(sampleRate);
        reset(); updateCoeffs();
    }
    void reset() { s1 = s2 = 0.0; osLp.reset(); osBp.reset(); osHp.reset(); osIn.reset(); }
    int  latencySamples() const { return os ? Oversampler2x::latency() : 0; }

    void setCutoff(double hz)    { cutoff = hz;  updateCoeffs(); }
    void setResonance(double r)  { res = std::clamp(r, 0.0, 1.0); updateCoeffs(); }
    void setDrive(double d)      { drive = std::clamp(d, 0.0, 1.0); updateCoeffs(); }
    void set(double hz, double r, double d)
    {
        cutoff = hz; res = std::clamp(r, 0.0, 1.0); drive = std::clamp(d, 0.0, 1.0);
        updateCoeffs();
    }

    Out process(float in)
    {
        if (!os)
        {
            double lp, bp, hp;
            tick(in, lp, bp, hp);
            return { (float)dcLp.process(lp), (float)dcBp.process(bp), (float)dcHp.process(hp) };
        }
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
    bool os = true;
    double fs2 = 96000.0, cutoff = 1000.0, res = 0.3, drive = 0.2;
    double g = 0.0, k = 2.0, lim = 3.0, fold = 0.0, bias = 0.0, inGain = 1.0, outComp = 1.0;
    double s1 = 0.0, s2 = 0.0;
    Oversampler2x osIn, osLp, osBp, osHp;
    DcBlock dcLp, dcBp, dcHp;

    void updateCoeffs()
    {
        const double fc = std::clamp(cutoff, 10.0, 0.45 * fs2);
        g = std::tan(kPi * fc / fs2);

        // Resonance voiced toward the midrange: full strength around ~900 Hz,
        // tapering gently toward the lows and highs (log-frequency bell; the
        // edges keep 80 % so the filter still sings at both ends).
        // (Kulmahiomakone 26.11.2026: wider bell, stronger self-oscillation.)
        const double oct = std::log2(fc / 900.0);
        const double voice = 0.8 + 0.2 * std::exp(-(oct * oct) / (2.0 * 2.0 * 2.0));
        const double r = res * voice;
        k = std::max(-0.2, 2.0 * (1.0 - 1.1 * r)); // k = 1/Q; <0 = self-osc

        // In-core distortion: lower state ceiling = more crunch. The ceiling
        // grows with resonance and the input is turned down as resonance rises,
        // so the resonant ring has room to swing instead of being clipped away
        // (that's what made the filter sound bland next to a hot synth).
        lim    = 3.0 / (1.0 + 10.0 * drive * drive) * (1.0 + 3.0 * res);
        fold   = std::clamp((drive - 0.4) / 0.6, 0.0, 1.0); // folding/rustle from 0.4 up
        bias   = 0.2 * drive;                                // asymmetry -> even harmonics
        const double inScale = 1.0 - 0.7 * std::pow(res, 1.5);
        inGain = (1.0 + 2.5 * drive) * inScale;
        // make-up gain: roughly undoes the ceiling and the input turn-down, so
        // Drive is heard as character rather than as a level drop
        // and the strong resonance is tamed so it sings without jumping in level
        outComp = std::pow(1.0 + 10.0 * drive * drive, 0.3) / std::pow(1.0 + 2.5 * drive, 0.25)
                / (1.0 + 6.0 * res * res * res);
    }

    static double ceil3(double y) { return y / std::sqrt(1.0 + (y * y) / 9.0); }

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

        // light level compensation only, so turning Drive up is clearly audible;
        // a soft ceiling (~±3) keeps the strong resonance's transients bounded
        lp = ceil3(lp * outComp); bp = ceil3(bp * outComp); hp = ceil3(hp * outComp);
    }
};

// ---------------------------------------------------------------------------
class VillapaitaFilter
{
public:
    void prepare(double sampleRate, bool oversample = true)
    {
        os = oversample;
        fs2 = (os ? 2.0 : 1.0) * sampleRate;
        dc.prepare(sampleRate); reset(); updateCoeffs();
    }
    void reset() { s.fill(0.0); bassLp = 0.0; osIn.reset(); osOut.reset(); }
    int  latencySamples() const { return os ? Oversampler2x::latency() : 0; }

    void setCutoff(double hz)     { cutoff = hz; updateCoeffs(); }
    void setResonance(double r)   { res = std::clamp(r, 0.0, 1.0); updateCoeffs(); }
    // 0 = attenuated & polite, ~0.5 = unity, 1 = hot & biting
    void setInputGain(double g01) { gain01 = std::clamp(g01, 0.0, 1.0); updateCoeffs(); }
    void set(double hz, double r, double g01)
    {
        cutoff = hz; res = std::clamp(r, 0.0, 1.0); gain01 = std::clamp(g01, 0.0, 1.0);
        updateCoeffs();
    }

    float process(float in)
    {
        if (!os) return (float)dc.process(tick(in));
        double a, b;
        osIn.upsample(in, a, b);
        const double ya = tick(a);
        const double yb = tick(b);
        return (float)dc.process(osOut.downsample(ya, yb));
    }

private:
    bool os = true;
    double fs2 = 96000.0, cutoff = 1000.0, res = 0.3, gain01 = 0.5;
    double k = 0.0, inGain = 1.0, outGain = 1.0, bassMakeup = 0.35, bassReturn = 0.0, bassA = 0.01, bassLp = 0.0, resKnee = 1.0;
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
        // Resonance curve: more happens in the upper half of the knob.
        // ~4 = self-oscillation, max 5.6 = strong, singing self-oscillation.
        // (Kulmahiomakone 26.11.2026: boosted from max 4.25.)
        k = 6.5 * std::pow(res, 1.25);
        // 0.25x ... 6x; at high resonance + high gain the input is eased back a
        // little so the resonance isn't choked by the saturated poles
        inGain  = 0.25 * std::pow(24.0, gain01) * (1.0 - 0.75 * std::pow(res, 1.5) * gain01);
        // level compensation: input gain, and the louder self-oscillation
        outGain = 1.0 / std::pow(inGain, 0.75) / (1.0 + 0.25 * std::max(0.0, k - 3.0) * (1.0 - 0.7 * gain01));
        bassMakeup = 0.35;
        resKnee = std::max(1.0, inGain);
        // bass return: like the real module (demo video, 100 % reso + 100 % gain)
        // the bass stays strong under the singing peak. Done in parallel (not by
        // pushing more input into the loop, which would choke the resonance).
        bassReturn = 0.9 * std::clamp((res - 0.3) / 0.7, 0.0, 1.0);
        bassA = 1.0 - std::exp(-2.0 * kPi * std::min(150.0, cutoff * 0.5) / fs2);
    }

    // Diode-pair control element: softer, rounder knee than tanh.
    static double diode(double u, double a)
    {
        const double x = u / a;
        return a * x / std::sqrt(1.0 + x * x);
    }

    // Soft-clipping diodes in the resonance loop (antiparallel pair).
    // Knee raised and softened (was 0.6 / 2.5) so the resonant peak isn't
    // flattened before it can be heard.
    // The knee follows the input gain, so the resonance keeps up with a hot
    // input instead of drowning under it (demo video: 100 % gain + 100 % reso
    // still sings loudly).
    double resClip(double y) const
    {
        const double t = resKnee;
        const double ay = std::abs(y);
        if (ay <= t) return y;
        const double over = ay - t;
        return std::copysign(t + over / (1.0 + 1.5 * over), y);
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
        double u = x * (1.0 + bassMakeup * k) - k * resClip(y4est);
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
        double y = diode(u + 0.08, 0.9) - diode(0.08, 0.9);
        bassLp += bassA * (x - bassLp);
        y += bassReturn * diode(bassLp, 0.9);
        return y * outGain * 1.15;
    }
};

} // namespace kh
