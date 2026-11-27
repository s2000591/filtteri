// Kumiankka (303-tyyppinen diodiporras) ja Villapaidan acid-tila:
// vakaus, taajuusvaste (piikin paikka, basson ohentuminen), resonanssin sammuminen.
// build: g++ -std=c++17 -O2 -Iinclude tests/kumiankka_test.cpp -o kumiankka_test
#include "KhFilters.h"
#include <cstdio>
#include <random>
#include <functional>
#include <memory>

static const double FS = 48000.0;
using Proc = std::function<float(float)>;

static double gainAt(Proc p, double f, double amp = 0.01)
{
    double pk = 0;
    for (int i = 0; i < 24000; ++i) {
        float y = p((float)(amp * std::sin(2 * kh::kPi * f * i / FS)));
        if (i > 16000) pk = std::max(pk, (double)std::abs(y));
    }
    return 20 * std::log10(pk / amp + 1e-12);
}

static void sweep(const char* name, std::function<Proc()> make)
{
    double best = -1e9, bestF = 0;
    std::printf("  %-28s", name);
    for (double f : { 40.0, 100.0, 300.0, 600.0, 800.0, 1000.0, 1250.0, 2000.0, 4000.0, 8000.0 }) {
        double g = gainAt(make(), f);
        if (g > best) { best = g; bestF = f; }
        std::printf("%6.1f", g);
    }
    std::printf("   huippu %.0f Hz\n", bestF);
}

static double ringAfter(Proc p, double tSec)
{
    double pk = 0; const int n0 = (int)(tSec * FS);
    for (int i = 0; i < n0 + 4800; ++i) {
        float y = p(i == 0 ? 0.5f : 0.f);
        if (i >= n0) pk = std::max(pk, (double)std::abs(y));
    }
    return pk;
}

int main()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(0, 1);
    bool nan = false; double worstK = 0, worstV = 0;
    for (int t = 0; t < 300; ++t) {
        kh::KumiankkaFilter k; k.prepare(FS);
        kh::VillapaitaFilter v; v.prepare(FS);
        double amp = 0.1 + 4.0 * U(rng), ph = 0, inc = (20 + 2000 * U(rng)) / FS;
        for (int i = 0; i < 4800; ++i) {
            if (i % 64 == 0) {
                double c = 20 * std::pow(1000.0, U(rng));
                k.set(c, U(rng), U(rng));
                v.set(c, U(rng), U(rng)); v.setAcid(U(rng));
            }
            ph += inc; if (ph >= 1) ph -= 1;
            float x = (float)(amp * ((2 * ph - 1) * 0.5 + (U(rng) - 0.5)));
            float a = k.process(x), b = v.process(x);
            if (!std::isfinite(a) || !std::isfinite(b)) nan = true;
            worstK = std::max(worstK, (double)std::abs(a)); worstV = std::max(worstV, (double)std::abs(b));
        }
    }
    std::printf("vakaus: nan=%d  worst|Kumiankka|=%.2f  worst|Villapaita acid|=%.2f\n", nan, worstK, worstV);

    std::printf("\ntaajuusvaste (dB), cutoff 1 kHz, pieni signaali\n  %-28s", "");
    for (int f : { 40, 100, 300, 600, 800, 1000, 1250, 2000, 4000, 8000 }) std::printf("%6d", f);
    std::printf("\n");
    for (double r : { 0.0, 0.5, 0.8, 1.0 }) {
        char nm[64]; std::snprintf(nm, sizeof nm, "Kumiankka reso %.1f", r);
        sweep(nm, [r] { auto f = std::make_shared<kh::KumiankkaFilter>(); f->prepare(FS); f->set(1000, r, 0);
                         return Proc([f](float x) { return f->process(x); }); });
    }
    for (double a : { 0.0, 1.0 })
        for (double r : { 0.5, 1.0 }) {
            char nm[64]; std::snprintf(nm, sizeof nm, "Villapaita acid %.0f reso %.1f", a, r);
            sweep(nm, [a, r] { auto f = std::make_shared<kh::VillapaitaFilter>(); f->prepare(FS); f->set(1000, r, 0.5); f->setAcid(a);
                               return Proc([f](float x) { return f->process(x); }); });
        }

    std::printf("\nresonanssin soiminen impulssin jälkeen (reso 1.0, cutoff 440)\n");
    for (double t : { 0.05, 0.2, 1.0 }) {
        auto k = std::make_shared<kh::KumiankkaFilter>(); k->prepare(FS); k->set(440, 1.0, 0);
        auto v0 = std::make_shared<kh::VillapaitaFilter>(); v0->prepare(FS); v0->set(440, 1.0, 0.5);
        auto v1 = std::make_shared<kh::VillapaitaFilter>(); v1->prepare(FS); v1->set(440, 1.0, 0.5); v1->setAcid(1.0);
        std::printf("  %.2f s: Kumiankka %.4f   Villapaita %.4f   Villapaita acid %.4f\n", t,
                    ringAfter([k](float x) { return k->process(x); }, t),
                    ringAfter([v0](float x) { return v0->process(x); }, t),
                    ringAfter([v1](float x) { return v1->process(x); }, t));
    }
    return nan ? 1 : 0;
}
