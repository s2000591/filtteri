# Filtteri

Neljä luonteikasta suodatinta VST-syntikoihin (esim. Kulmahiomakone).
Header-only C++17, ei JUCE-riippuvuutta: `include/`.

| Luokka | Esikuva | Luonne |
|---|---|---|
| `kh::KatkarapuFilter` | Crazy Chicken **Shrimpy** | Multimode SVF (LP/BP/HP yhtä aikaa), resonanssi painottuu kevyesti keskitaajuuksille ja itseoskilloi koko alueella, särö suodattimen *sisällä*; Driven yläpäässä (yli ~0.4) taittuva, rahiseva reuna |
| `kh::VillapaitaFilter` | Dwyfor Tech **Pas-Isel** | 4-napainen alipäästö, jokaisella navalla oma diodimainen epälineaarisuus ja pieni virityspoikkeama, soft-clip-diodit resonanssisilmukassa (Korg MS -tyyliin); pieni tulovahvistus = siisti, iso = pureva (herää noin 0.75:stä). Basso säilyy resonanssin alla ja resonanssi laulaa myös täydellä vahvistuksella |
| `kh::KumiankkaFilter` | Roland **TB-303** | Diodiporras: neljä toisiaan kuormittavaa napaa (ei puskureita), pyöreä ja kumimainen leikkaus. Basso ohenee resonanssin noustessa, resonanssi soi pitkään mutta ei itseoskilloi, ja matalilla cutoffeilla se on heikompi. Drive (ei 303:ssa) kuumentaa diodeja |
| `kh::AvaruusradioFilter` | "Avaruusradio" (Paratek, Oxymoron) | Formanttisuodatin (suomen vokaalit a–ä–e–i–y–ö–o–u), satunnainen tavu-/sanagenerointi "mutinaksi" ja huonosti kuuluva radio: SSB-virityksen ryömintä, kaistanrajaus, häipymät, rätinä, vihellys |

`AvaruusradioFilter` on omassa headerissaan: `include/AvaruusradioFilter.h`.

Shrimpy- ja Pas-Isel-moduleista ei ole julkista kytkentäkaaviota, joten nämä ovat
valmistajien kuvauksiin perustuvia luonnemalleja, eivät piiriemulaatioita. Myös Kumiankka on
luonnemalli (kytketty diodiporras), ei 303:n piiriemulaatio.

**Villapaidan acid-tila** (`setAcid(0..1)`): 0 = Pas-Isel-luonne bittitarkasti ennallaan,
1 = 303-henkinen: basso ohenee resolla, resonanssi jää juuri itseoskilloinnin alle ja yksi
napa laskee muita alemmas (loivempi, pyöreämpi leikkaus). Väliarvot liukuvat.

Katkarapu, Villapaita ja Kumiankka ajetaan sisäisesti 2× ylinäytteistettynä. Viive: `latencySamples()` (15 näytettä).

## Käyttö

```cpp
#include "KhFilters.h"

kh::KatkarapuFilter kat;
kat.prepare(sampleRate);
kat.setCutoff(800.0);    // Hz
kat.setResonance(0.8);   // 0..1 (itseoskilloi lähellä maksimia)
kat.setDrive(0.5);       // 0..1 (yli ~0.4 alkaa taittuminen)
auto o = kat.process(x); // o.lp, o.bp, o.hp

kh::VillapaitaFilter vil;
vil.prepare(sampleRate);
vil.setCutoff(800.0);
vil.setResonance(0.8);   // 0..1 (~0.7+ itseoskilloi)
vil.setInputGain(0.5);   // 0 = kohtelias, 0.5 ≈ yksikkövahvistus, 1 = pureva
vil.setAcid(1.0);        // 0 = Pas-Isel, 1 = 303-henkinen
float y = vil.process(x);

kh::KumiankkaFilter kum;
kum.prepare(sampleRate);
kum.set(800.0, 0.85, 0.0); // cutoff Hz, reso 0..1 (ei itseoskilloi), drive 0..1
float z = kum.process(x);
```

```cpp
#include "AvaruusradioFilter.h"

kh::AvaruusradioFilter radio;
radio.prepare(sampleRate);
radio.setBabble(0.8);    // Mutina: 0 = staattinen vokaali, 1 = satunnaista "puhetta"
radio.setRadio(0.6);     // Kuuluvuus: 0 = puhdas, 1 = kaukainen ryömivä asema
radio.setVowel(0.0);     // käsin valittu vokaali kun Mutina = 0 (a ... u)
radio.setRate(5.0);      // tavuja sekunnissa
radio.setSharpness(0.6); // formanttien terävyys
radio.setSeed(42);       // eri "puhuja"
float y = radio.process(x); // syötä jotain surisevaa: saha, pulssi, kohina
```

Jos isäntäkoodi pyörii jo ylinäytteistettynä (esim. Kulmahiomakone 4x), ohita
suotimien oma ylinäytteistys: `kat.prepare(fs, false)` / `vil.prepare(fs, false)` / `kum.prepare(fs, false)`.
Silloin viivettä ei ole. `set(cutoff, reso, drive)` päivittää kaiken yhdellä
kerroinlaskulla.

Setterit laskevat kertoimet (sisältää `tan`-kutsuja), joten kutsu niitä
blokin välein tai esim. joka 16. näyte, ja silota parametrit itse.
Yksi instanssi per ääni (polyfonisessa syntikassa).

## Muutokset 27.11.2026

- Uusi **Kumiankka** (303-tyyppinen diodiporras) ja testi `tests/kumiankka_test.cpp`.
- **Villapaita:** `setAcid()`. Oletus 0 tuottaa täsmälleen saman äänen kuin ennen.
- Uusi demolinja `acidLine` (aksentit, slidet, kasautuva accent sweep) ja näytteet `00_dry_acidline`, `u*`, `v08`, `v09`.

## Muutokset 26.11.2026 (Kulmahiomakoneen kanssa yhteiset)

Mitattu ja viritetty Kulmahiomakoneessa (kohina- ja sahatestit, Pas-Isel-demovideon ääni):

- **Katkarapu:** resonanssi hukkui sisäiseen säröön, kun tulo oli kuuma. Nyt säröraja kasvaa
  resonanssin mukana ja tuloa lasketaan resonanssin noustessa, joten resonanssi soi
  (piikki 900 Hz:ssä reso 1:llä noin 4 dB → noin 19 dB). Leveämpi resonanssialue (reunoilla 80 %),
  vahvempi itseoskillointi, taittuminen alkaa Drivesta 0.4, kevyempi tasokompensaatio
  (Drive kuuluu luonteena, ei tason pudotuksena), pehmeä katto lähdöissä (~±3).
- **Villapaita:** resonanssi max 4.25 → 6.5, takaisinkytkennän diodien kynnys seuraa
  tulovahvistusta ja suurella resolla + vahvistuksella tuloa hellitetään, joten resonanssi ei
  tukehdu kuumaan tuloon (100/100: resonanssi noin −3 dB basson perustaajuudesta, ennen −23 dB).
  Basso palautetaan rinnakkain, joten se säilyy resonanssin alla kuten oikeassa modulissa.
- Näytteet `k*` ja `v*` renderöity uudelleen.

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
| `v08_acidline_acid0.mp3` | Acid-linja Villapaidan läpi, acid 0 |
| `v09_acidline_acid1.mp3` | Sama, acid 1 |

### Kumiankka

Acid-linja: 138 BPM, aksentit ja slidet, accent sweep kasautuu peräkkäisillä aksenteilla.

| Tiedosto | Mitä |
|---|---|
| `00_dry_acidline.mp3` | Suodattamaton acid-linja (vertailu) |
| `u01_acid_mid.mp3` | Reso 0.6 |
| `u02_acid_squelch.mp3` | Reso 0.9 |
| `u03_acid_hot.mp3` | Reso 0.9, drive 0.8 |
| `u04_goabass.mp3` | Goa-bassolinja, reso 0.75 |
| `u05_pad.mp3` | Molli-padi, hidas sweep |

### Avaruusradio

| Tiedosto | Mitä |
|---|---|
| `r01_vowel_sweep.mp3` | Vokaalit a → u → a käsin, ei mutinaa |
| `r02_mumble.mp3` | Mutina 0.6, radio 0 |
| `r03_fast_chatter.mp3` | Mutina 1.0, 9 tavua/s |
| `r04_mumble_radio_half.mp3` | Mutina 0.8, radio 0.5 |
| `r05_distant_station.mp3` | Mutina 0.85, radio 1.0: kaukainen asema |
| `r06_radio_ramp.mp3` | Radio nousee 0 → 1 kahdessatoista sekunnissa |
| `r07_talking_bassline.mp3` | Goa-basso puhuu (tavut 16-osien tahdissa) |
| `r08_pad_radio.mp3` | Padi radion läpi |
| `r09_whisper_station.mp3` | Kohinatulo: kuiskaava asema |

## Rakentaminen

```sh
# testit (vakaus, taajuusvaste, itseoskillointi)
g++ -std=c++17 -O2 -Iinclude tests/stability_test.cpp -o stability_test && ./stability_test
g++ -std=c++17 -O2 -Iinclude tests/avaruusradio_test.cpp -o avaruusradio_test && ./avaruusradio_test
g++ -std=c++17 -O2 -Iinclude tests/kumiankka_test.cpp -o kumiankka_test && ./kumiankka_test

# näytteiden renderöinti (WAV)
g++ -std=c++17 -O2 -Iinclude demos/render_demos.cpp -o render_demos && ./render_demos samples
```

## Ideoita myöhemmäksi

- Lo-fi-säätimet Katkarapuun, Villapaitaan ja Kumiankkaan
- Säädin, josta kuulee digitaalisen aliasoinnin
