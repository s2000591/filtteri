// Renders the audio examples in samples/ (as 48 kHz 16-bit mono WAV).
// Build: g++ -std=c++17 -O2 -I../include render_demos.cpp -o render_demos
// Run:   ./render_demos <output-dir>
#include "KhFilters.h"
#include "AvaruusradioFilter.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <random>
#include <string>
#include <functional>
#include <memory>

static const double FS = 48000.0;
using Buf = std::vector<float>;

static void writeWav(const std::string& path, Buf x)
{
    float pk = 0; for (float s : x) pk = std::max(pk, std::abs(s));
    const float norm = pk > 0 ? 0.8f / pk : 1.f;   // every example normalised to -2 dBFS
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    uint32_t n = (uint32_t)x.size();
    std::fwrite("RIFF", 1, 4, f); w32(36 + n * 2); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(1); w32((uint32_t)FS); w32((uint32_t)FS * 2); w16(2); w16(16);
    std::fwrite("data", 1, 4, f); w32(n * 2);
    for (float s : x) { int v = (int)std::lround(std::clamp(s * norm, -1.f, 1.f) * 32767.f); w16((uint16_t)(int16_t)v); }
    std::fclose(f);
    std::printf("wrote %s (peak before normalise %.2f)\n", path.c_str(), pk);
}

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

// (input sample, cutoff Hz, time s) -> output
using Proc = std::function<float(float, double, double)>;

// 16th-note goa bassline at 145 BPM, per-note cutoff envelope + slow sweep up & down.
static Buf goaBass(Proc p, double seconds = 6.0, double baseCut = 150, double envAmt = 2.5)
{
    const int notes[16] = { 0, 0, 12, 0, 0, 7, 0, 12, 0, 0, 10, 0, 0, 12, 3, 0 };
    const double step = 60.0 / 145.0 / 4.0;
    Saw saw; Buf out;
    for (int i = 0; i < (int)(seconds * FS); ++i) {
        double t = i / FS; int st = (int)(t / step); double tin = t - st * step;
        saw.setFreq(43.65 * std::pow(2.0, notes[st % 16] / 12.0));
        double amp = tin < step * 0.85 ? 1.0 : 0.0;
        double sweep = 0.5 - 0.5 * std::cos(2 * kh::kPi * t / seconds);
        double cut = baseCut * std::pow(2.0, 3.5 * sweep + envAmt * std::exp(-tin / 0.06));
        out.push_back(p((float)(0.5 * amp * saw.next()), cut, t));
    }
    return out;
}

// 303-tyylinen acid-linja: 16 askelta, aksentit ja slidet. Aksentti = kovempi
// ja lyhyempi, korkeampi suodinpyyhkäisy, joka kasautuu peräkkäisillä
// aksenteilla (accent sweep). Slide = gate auki seuraavaan + sävelliuku.
static Buf acidLine(Proc p, double seconds = 8.0, double baseCut = 180, double envOct = 3.0)
{
    const int    note[16]   = { 0, 12, 0, 3, 0, 15, 12, 0, 7, 0, 10, 12, 0, 3, 5, 0 };
    const bool   acc[16]    = { 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0 };
    const bool   slide[16]  = { 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    const double step = 60.0 / 138.0 / 4.0;
    Saw saw; Buf out; double pitch = note[0], env = 0, accCap = 0;
    int last = -1;
    for (int i = 0; i < (int)(seconds * FS); ++i) {
        double t = i / FS; int st = (int)(t / step); double tin = t - st * step; int s16 = st % 16;
        if (st != last) {
            const bool tied = slide[(st + 15) % 16];
            if (!tied) env = 1.0;
            if (acc[s16]) accCap += 0.5 * (1.0 - accCap);
            last = st;
        }
        pitch += (note[s16] - pitch) * (slide[(st + 15) % 16] ? 0.0012 : 1.0);
        saw.setFreq(43.65 * std::pow(2.0, pitch / 12.0));
        const double dec = acc[s16] ? 0.2 : 0.5;
        env *= std::exp(-1.0 / (dec * 0.25 * FS));
        accCap *= std::exp(-1.0 / (0.35 * FS));
        const bool gate = tin < step * 0.6 || slide[s16];
        const double amp = gate ? (acc[s16] ? 1.0 : 0.65) : 0.0;
        const double sweep = 0.5 - 0.5 * std::cos(2 * kh::kPi * t / seconds);
        const double cut = baseCut * std::pow(2.0, 2.5 * sweep + envOct * env + (acc[s16] ? 1.8 * accCap : 0.0));
        out.push_back(p((float)(0.5 * amp * saw.next()), cut, t));
    }
    return out;
}

// Slow pad: detuned saws playing a minor chord, cutoff sweeps slowly.
static Buf pad(Proc p, double seconds = 8.0)
{
    const double freqs[] = { 110.0, 130.81, 164.81, 220.0 };
    std::vector<Saw> saws;
    for (double f : freqs) for (double d : { -0.12, 0.0, 0.13 }) { Saw s; s.setFreq(f * std::pow(2.0, d / 12.0)); s.ph = (saws.size() * 0.37); s.ph -= std::floor(s.ph); saws.push_back(s); }
    Buf out;
    for (int i = 0; i < (int)(seconds * FS); ++i) {
        double t = i / FS, x = 0;
        for (auto& s : saws) x += s.next();
        double env = std::min(1.0, t / 1.0) * std::min(1.0, (seconds - t) / 1.0);
        double cut = 200.0 * std::pow(2.0, 4.0 * (0.5 - 0.5 * std::cos(2 * kh::kPi * t / seconds)));
        out.push_back(p((float)(0.12 * env * x), cut, t));
    }
    return out;
}

// Noise bursts + envelopes on cutoff: the filter as a percussion voice.
static Buf percussion(Proc p, double seconds = 6.0)
{
    std::mt19937 rng(7); std::uniform_real_distribution<double> U(-1, 1);
    const double step = 60.0 / 145.0 / 4.0;
    const double pitches[8] = { 90, 0, 400, 90, 1200, 0, 400, 2500 };
    Buf out;
    for (int i = 0; i < (int)(seconds * FS); ++i) {
        double t = i / FS; int st = (int)(t / step); double tin = t - st * step;
        double base = pitches[st % 8];
        double x = base > 0 ? U(rng) * std::exp(-tin / 0.004) : 0.0;
        double cut = (base > 0 ? base : 90) * (1.0 + 1.5 * std::exp(-tin / 0.02));
        out.push_back(p((float)x, cut, t));
    }
    return out;
}

// Buzzy "vocal cords": saw with slow random intonation and a little vibrato.
static Buf buzz(std::function<float(float, double)> p, double seconds, double baseHz = 110.0, uint32_t seed = 5)
{
    std::mt19937 rng(seed); std::uniform_real_distribution<double> U(-1, 1);
    Saw saw; Buf out; double pitch = 0, target = 0;
    for (int i = 0; i < (int)(seconds * FS); ++i) {
        double t = i / FS;
        if (i % 9000 == 0) target = 3.0 * U(rng);
        pitch += (target - pitch) * 0.0003;
        saw.setFreq(baseHz * std::pow(2.0, (pitch + 0.15 * std::sin(2 * kh::kPi * 5.2 * t)) / 12.0));
        out.push_back(p((float)(0.5 * saw.next()), t));
    }
    return out;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? std::string(argv[1]) + "/" : "./";

    auto kat = [](double res, double drv, int sel) {
        auto f = std::make_shared<kh::KatkarapuFilter>(); f->prepare(FS); f->setResonance(res); f->setDrive(drv);
        auto n = std::make_shared<int>(0);
        return Proc([=](float x, double c, double) {
            if (((*n)++ & 15) == 0) f->setCutoff(c);
            auto o = f->process(x); return sel == 0 ? o.lp : sel == 1 ? o.bp : o.hp; });
    };
    auto vil = [](double res, double g) {
        auto f = std::make_shared<kh::VillapaitaFilter>(); f->prepare(FS); f->setResonance(res); f->setInputGain(g);
        auto n = std::make_shared<int>(0);
        return Proc([=](float x, double c, double) {
            if (((*n)++ & 15) == 0) f->setCutoff(c);
            return f->process(x); });
    };

    auto kum = [](double res, double drv) {
        auto f = std::make_shared<kh::KumiankkaFilter>(); f->prepare(FS); f->setResonance(res); f->setDrive(drv);
        auto n = std::make_shared<int>(0);
        return Proc([=](float x, double c, double) {
            if (((*n)++ & 15) == 0) f->setCutoff(c);
            return f->process(x); });
    };
    auto vilAcid = [](double res, double g, double acid) {
        auto f = std::make_shared<kh::VillapaitaFilter>(); f->prepare(FS); f->setResonance(res); f->setInputGain(g); f->setAcid(acid);
        auto n = std::make_shared<int>(0);
        return Proc([=](float x, double c, double) {
            if (((*n)++ & 15) == 0) f->setCutoff(c);
            return f->process(x); });
    };

    // Reference
    writeWav(dir + "00_dry_bassline.wav", goaBass([](float x, double, double) { return x; }));
    writeWav(dir + "00_dry_acidline.wav", acidLine([](float x, double, double) { return x; }));

    // Kumiankka (303-tyyppinen diodiporras)
    writeWav(dir + "u01_acid_mid.wav",     acidLine(kum(0.6, 0.0)));
    writeWav(dir + "u02_acid_squelch.wav", acidLine(kum(0.9, 0.0)));
    writeWav(dir + "u03_acid_hot.wav",     acidLine(kum(0.9, 0.8)));
    writeWav(dir + "u04_goabass.wav",      goaBass(kum(0.75, 0.2)));
    writeWav(dir + "u05_pad.wav",          pad(kum(0.8, 0.1)));
    // Villapaita: sama acid-linja, acid pois / päällä
    writeWav(dir + "v08_acidline_acid0.wav", acidLine(vilAcid(0.85, 0.5, 0.0)));
    writeWav(dir + "v09_acidline_acid1.wav", acidLine(vilAcid(0.9, 0.5, 1.0)));

    // Katkarapu
    writeWav(dir + "k01_lp_clean.wav",   goaBass(kat(0.6, 0.1, 0)));
    writeWav(dir + "k02_lp_crunch.wav",  goaBass(kat(0.8, 0.5, 0)));
    writeWav(dir + "k03_lp_rustle.wav",  goaBass(kat(0.9, 0.9, 0)));
    writeWav(dir + "k04_bp.wav",         goaBass(kat(0.85, 0.6, 1), 6.0, 200, 2.0));
    writeWav(dir + "k05_hp.wav",         goaBass(kat(0.7, 0.4, 2), 6.0, 60, 2.0));
    {   // drive ramp 0 -> 1 at fixed-ish cutoff
        kh::KatkarapuFilter f; f.prepare(FS); f.setResonance(0.85); int n = 0;
        writeWav(dir + "k06_drive_ramp.wav", goaBass([&](float x, double c, double t) {
            if ((n++ & 15) == 0) { f.setCutoff(std::min(c, 900.0) + 300); f.setDrive(t / 8.0); }
            return f.process(x).lp; }, 8.0, 250, 1.5));
    }
    writeWav(dir + "k07_percussion_bp.wav", percussion(kat(0.97, 0.35, 1)));
    writeWav(dir + "k08_pad_lp.wav",        pad(kat(0.7, 0.3, 0)));

    // Villapaita
    writeWav(dir + "v01_polite.wav", goaBass(vil(0.6, 0.15)));
    writeWav(dir + "v02_medium.wav", goaBass(vil(0.8, 0.6)));
    writeWav(dir + "v03_bite.wav",   goaBass(vil(0.9, 1.0)));
    {   // input gain ramp 0 -> 1
        kh::VillapaitaFilter f; f.prepare(FS); f.setResonance(0.8); int n = 0;
        writeWav(dir + "v04_gain_ramp.wav", goaBass([&](float x, double c, double t) {
            if ((n++ & 15) == 0) { f.setCutoff(std::min(c, 900.0) + 300); f.setInputGain(t / 8.0); }
            return f.process(x); }, 8.0, 250, 1.5));
    }
    {   // self-oscillation, no input: cutoff glides like a sine VCO
        kh::VillapaitaFilter f; f.prepare(FS); f.setResonance(1.0); f.setInputGain(0.5);
        Buf b; const double secs = 6.0;
        for (int i = 0; i < (int)(secs * FS); ++i) {
            double t = i / FS;
            if ((i & 15) == 0) f.setCutoff(220.0 * std::pow(2.0, 2.0 * (0.5 - 0.5 * std::cos(2 * kh::kPi * t / 3.0))));
            b.push_back(f.process(i == 0 ? 0.5f : 0.f));
        }
        writeWav(dir + "v05_self_oscillation.wav", b);
    }
    writeWav(dir + "v06_pad.wav",           pad(vil(0.7, 0.4)));
    writeWav(dir + "v07_percussion.wav",    percussion(vil(0.97, 0.5)));
    // Avaruusradio (formant / mumble / bad radio)
    auto ar = [](double babble, double radioAmt, double rate, uint32_t seed) {
        auto f = std::make_shared<kh::AvaruusradioFilter>(); f->prepare(FS); f->setSeed(seed);
        f->setBabble(babble); f->setRadio(radioAmt); f->setRate(rate); f->setSharpness(0.6);
        return f;
    };
    {   // manual vowel sweep a -> u and back, no babble
        auto f = ar(0.0, 0.0, 5, 1);
        writeWav(dir + "r01_vowel_sweep.wav", buzz([&](float x, double t) {
            f->setVowel(0.5 - 0.5 * std::cos(2 * kh::kPi * t / 8.0)); return f->process(x); }, 8.0));
    }
    { auto f = ar(0.6, 0.0, 5, 2);  writeWav(dir + "r02_mumble.wav",            buzz([&](float x, double) { return f->process(x); }, 10.0)); }
    { auto f = ar(1.0, 0.0, 9, 3);  writeWav(dir + "r03_fast_chatter.wav",      buzz([&](float x, double) { return f->process(x); }, 10.0, 140.0)); }
    { auto f = ar(0.8, 0.5, 5, 4);  writeWav(dir + "r04_mumble_radio_half.wav", buzz([&](float x, double) { return f->process(x); }, 10.0)); }
    { auto f = ar(0.85, 1.0, 4.5, 5); writeWav(dir + "r05_distant_station.wav", buzz([&](float x, double) { return f->process(x); }, 12.0, 95.0)); }
    {   // radio knob ramps 0 -> 1 while it keeps talking
        auto f = ar(0.8, 0.0, 5, 6);
        writeWav(dir + "r06_radio_ramp.wav", buzz([&](float x, double t) {
            if (((int)(t * FS) & 63) == 0) { f->setRadio(t / 12.0); } return f->process(x); }, 12.0));
    }
    { auto f = ar(0.7, 0.3, 145.0 / 60.0 * 2, 7);
      writeWav(dir + "r07_talking_bassline.wav", goaBass([&](float x, double, double) { return f->process(x * 1.5f); }, 8.0)); }
    { auto f = ar(0.6, 0.7, 3, 8);  writeWav(dir + "r08_pad_radio.wav", pad([&](float x, double, double) { return f->process(x * 2.f); }, 10.0)); }
    {   // noise source: whispering station
        auto f = ar(0.9, 0.8, 6, 9); std::mt19937 rng(11); std::uniform_real_distribution<double> U(-1, 1);
        Buf b; for (int i = 0; i < (int)(10 * FS); ++i) b.push_back(f->process((float)(0.4 * U(rng))));
        writeWav(dir + "r09_whisper_station.wav", b);
    }
    return 0;
}
