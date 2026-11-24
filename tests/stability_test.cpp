// Stability / response / self-oscillation checks for KhFilters.h
#include "KhFilters.h" // build: g++ -std=c++17 -O2 -Iinclude tests/stability_test.cpp
#include <cstdio>
#include <cstdint>
#include <vector>
#include <random>
#include <string>
#include <functional>

static const double FS = 48000.0;


// PolyBLEP saw
struct Saw {
    double ph = 0, inc = 0;
    void setFreq(double f) { inc = f / FS; }
    static double blep(double t, double dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    double next() { double v = 2.0 * ph - 1.0 - blep(ph, inc); ph += inc; if (ph >= 1) ph -= 1; return v; }
};

// 16th-note goa-ish bassline, cutoff envelope per note + slow sweep.



int main()
{
    // ---- stability torture: random params, loud noise + saw, check finite & bounded
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> U(0, 1);
    double worstK = 0, worstV = 0; bool nanFound = false;
    for (int trial = 0; trial < 300; ++trial) {
        kh::KatkarapuFilter kf; kh::VillapaitaFilter vf;
        kf.prepare(FS); vf.prepare(FS);
        Saw saw; saw.setFreq(20 + 2000 * U(rng));
        double amp = 0.1 + 4.0 * U(rng);
        for (int i = 0; i < 4800; ++i) {
            if (i % 64 == 0) {
                double c = 20 * std::pow(1000.0, U(rng));
                kf.setCutoff(c); kf.setResonance(U(rng)); kf.setDrive(U(rng));
                vf.setCutoff(c); vf.setResonance(U(rng)); vf.setInputGain(U(rng));
            }
            float x = (float)(amp * (0.5 * saw.next() + (U(rng) - 0.5)));
            auto o = kf.process(x); float v = vf.process(x);
            for (float y : { o.lp, o.bp, o.hp }) { if (!std::isfinite(y)) nanFound = true; worstK = std::max(worstK, (double)std::abs(y)); }
            if (!std::isfinite(v)) { nanFound = true; } worstV = std::max(worstV, (double)std::abs(v));
        }
    }
    std::printf("stability: nan=%d  worst|Katkarapu|=%.2f  worst|Villapaita|=%.2f\n", nanFound, worstK, worstV);

    // ---- small-signal magnitude response sanity check (Villapaita LP @1kHz, Katkarapu LP @1kHz)
    for (double f : { 100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 }) {
        kh::KatkarapuFilter kf; kf.prepare(FS); kf.setCutoff(1000); kf.setResonance(0.0); kf.setDrive(0.0);
        kh::VillapaitaFilter vf; vf.prepare(FS); vf.setCutoff(1000); vf.setResonance(0.0); vf.setInputGain(0.0);
        double pk = 0, pv = 0;
        for (int i = 0; i < 48000; ++i) {
            float x = (float)(0.01 * std::sin(2 * kh::kPi * f * i / FS));
            float a = kf.process(x).lp, b = vf.process(x);
            if (i > 24000) { pk = std::max(pk, (double)std::abs(a)); pv = std::max(pv, (double)std::abs(b)); }
        }
        // Villapaita at gain01=0 has out-gain compensation: normalise to unity at DC-ish separately
        std::printf("f=%6.0f  Katkarapu LP %6.1f dB   Villapaita %6.1f dB (incl. gain staging)\n",
                    f, 20 * std::log10(pk / 0.01), 20 * std::log10(pv / 0.01));
    }

    // ---- self-oscillation check
    {
        kh::VillapaitaFilter vf; vf.prepare(FS); vf.setCutoff(440); vf.setResonance(1.0); vf.setInputGain(0.5);
        kh::KatkarapuFilter kf; kf.prepare(FS); kf.setCutoff(900); kf.setResonance(1.0); kf.setDrive(0.3);
        double pv = 0, pk = 0;
        for (int i = 0; i < 96000; ++i) {
            float imp = (i == 0) ? 0.5f : 0.f;
            float v = vf.process(imp); float k = kf.process(imp).bp;
            if (i > 72000) { pv = std::max(pv, (double)std::abs(v)); pk = std::max(pk, (double)std::abs(k)); }
        }
        std::printf("self-osc after 1.5 s: Villapaita peak %.3f, Katkarapu BP peak %.3f\n", pv, pk);
    }

    return nanFound ? 1 : 0;
}
