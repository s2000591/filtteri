# Filtteri

Kaksi luonteikasta suodatinta VST-syntikoihin (esim. Kulmahiomakone).
Header-only C++17, ei JUCE-riippuvuutta: `include/KhFilters.h`.

| Luokka | Esikuva | Luonne |
|---|---|---|
| `kh::KatkarapuFilter` | Crazy Chicken **Shrimpy** | Multimode SVF (LP/BP/HP yhtä aikaa), resonanssi painottuu keskitaajuuksille, särö suodattimen *sisällä*; Driven yläpäässä taittuva, rahiseva reuna |
| `kh::VillapaitaFilter` | Dwyfor Tech **Pas-Isel** | 4-napainen alipäästö, jokaisella navalla oma diodimainen epälineaarisuus ja pieni virityspoikkeama, soft-clip-diodit resonanssisilmukassa (Korg MS -tyyliin); pieni tulovahvistus = siisti, iso = pureva |

Kummastakaan modulista ei ole julkista kytkentäkaaviota, joten nämä ovat
valmistajien kuvauksiin perustuvia luonnemalleja, eivät piiriemulaatioita.

Molemmat ajetaan sisäisesti 2× ylinäytteistettynä. Viive: `latencySamples()` (15 näytettä).

## Käyttö

```cpp
#include "KhFilters.h"

kh::KatkarapuFilter kat;
kat.prepare(sampleRate);
kat.setCutoff(800.0);    // Hz
kat.setResonance(0.8);   // 0..1 (itseoskilloi keskitaajuuksilla)
kat.setDrive(0.5);       // 0..1 (yli ~0.6 alkaa taittuminen)
auto o = kat.process(x); // o.lp, o.bp, o.hp

kh::VillapaitaFilter vil;
vil.prepare(sampleRate);
vil.setCutoff(800.0);
vil.setResonance(0.8);   // 0..1 (~0.95+ itseoskilloi)
vil.setInputGain(0.5);   // 0 = kohtelias, 0.5 ≈ yksikkövahvistus, 1 = pureva
float y = vil.process(x);
```

Setterit laskevat kertoimet (sisältää `tan`-kutsuja), joten kutsu niitä
blokin välein tai esim. joka 16. näyte, ja silota parametrit itse.
Yksi instanssi per ääni (polyfonisessa syntikassa).

## Ääninäytteet (`samples/`)

Bassolinja: 145 BPM, F1, 16-osat, cutoff-envelope per nuotti + hidas nousu ja lasku.
Kaikki on normalisoitu –2 dBFS:n huippuun, joten äänenvoimakkuudet eivät ole vertailukelpoisia.

| Tiedosto | Mitä |
|---|---|
| `00_dry_bassline.mp3` | Suodattamaton saha (vertailu) |
| `k01_lp_clean.mp3` | Katkarapu LP, res 0.6, drive 0.1 |
| `k02_lp_crunch.mp3` | Katkarapu LP, res 0.8, drive 0.5 |
| `k03_lp_rustle.mp3` | Katkarapu LP, res 0.9, drive 0.9 (taittuminen) |
| `k04_bp.mp3` | Katkarapu BP, res 0.85, drive 0.6 |
| `k05_hp.mp3` | Katkarapu HP, res 0.7, drive 0.4 |
| `k06_drive_ramp.mp3` | Drive nousee 0 → 1 kahdeksassa sekunnissa |
| `k07_percussion_bp.mp3` | Kohinapurskeet BP-lähtöön, korkea resonanssi: rumpuääni |
| `k08_pad_lp.mp3` | Molli-padi, hidas sweep |
| `v01_polite.mp3` | Villapaita, gain 0.15, res 0.6 |
| `v02_medium.mp3` | Villapaita, gain 0.6, res 0.8 |
| `v03_bite.mp3` | Villapaita, gain 1.0, res 0.9 |
| `v04_gain_ramp.mp3` | Tulovahvistus nousee 0 → 1 kahdeksassa sekunnissa |
| `v05_self_oscillation.mp3` | Ei tuloa, itseoskillointi, cutoff liukuu kuin siniosk. |
| `v06_pad.mp3` | Molli-padi, hidas sweep |
| `v07_percussion.mp3` | Kohinapurskeet, korkea resonanssi |

## Rakentaminen

```sh
# testit (vakaus, taajuusvaste, itseoskillointi)
g++ -std=c++17 -O2 -Iinclude tests/stability_test.cpp -o stability_test && ./stability_test

# näytteiden renderöinti (WAV)
g++ -std=c++17 -O2 -Iinclude demos/render_demos.cpp -o render_demos && ./render_demos samples
```
