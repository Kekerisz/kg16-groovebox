# KG-16 Groovebox

Dobgép és basszus-groovebox VST3 és AU pluginként (Mac: Intel és Apple Silicon, Windows: 64 bit). Tíz dobhang öt klasszikus gép-karakterrel, három basszusmotor, 64 lépéses szekvenszer A/B változatokkal, sávonkénti swing, szaturáció és Humanizer. A DAW piano rolljából MIDI-vel is vezérelhető.

## Telepítő letöltése

A telepítőket a GitHub fordítja le minden feltöltés után.

1. Nyisd meg a repó **Actions** fülét, és kattints a legfrissebb zöld pipás „Build installers” futásra.
2. Lent az **Artifacts** résznél töltsd le:
   - `KG16-Groovebox-Mac` → `KG16-Groovebox-Mac.pkg`
   - `KG16-Groovebox-Windows` → `KG16-Groovebox-Windows-Setup.exe`

Ha `v` kezdetű címkét adsz egy feltöltésnek (például `v1.0.0`), a két telepítő a **Releases** oldalon is megjelenik, onnan bárki letöltheti GitHub-fiók nélkül.

## Telepítés

**Mac:** kattints duplán a `.pkg` fájlra. Mivel a telepítő nincs Apple-fejlesztői tanúsítvánnyal aláírva, a macOS először nem engedi megnyitni. Ilyenkor menj a **Rendszerbeállítások → Adatvédelem és biztonság** menübe, és lent kattints a **Megnyitás mindenképp** gombra. A telepítő ide tesz:
- VST3: `/Library/Audio/Plug-Ins/VST3/KG-16 Groovebox.vst3`
- AU: `/Library/Audio/Plug-Ins/Components/KG-16 Groovebox.component`
- Önálló program: `/Applications/KG-16 Groovebox.app`

**Windows:** futtasd a `KG16-Groovebox-Windows-Setup.exe` fájlt. A SmartScreen figyelmeztetésnél válaszd a **További információ → Futtatás mindenképp** lehetőséget. A VST3 ide kerül: `C:\Program Files\Common Files\VST3\KG-16 Groovebox.vst3`. A plugin ablaka a Microsoft Edge WebView2-t használja, ami Windows 10 és 11 alatt általában már telepítve van.

## Használat a DAW-ban

**Ableton Live:** Beállítások → Plug-Ins → kapcsold be a VST3 (Macen az Audio Units) mappát, és nyomd meg a **Rescan** gombot. A KG-16 a böngészőben a Plug-Ins alatt, a „Kevin G Production” mappában jelenik meg. Húzd egy MIDI-sávra.

**Cubase:** a pluginkezelő (Studio → VST Plug-in Manager) indításkor magától megtalálja. Instrument-sávként add hozzá.

**Lejátszás:**
- A beépített szekvenszer a DAW tempójára és transportjára szinkronizál. A minta a DAW ütemeihez igazodik, a DAW lejátszásával indul és áll meg.
- Ha a DAW áll, a plugin saját Play gombja a beállított BPM-mel játszik.
- A **Belső szekvenszer** gombokkal a dob és a basszus szekvenszere külön kikapcsolható, így csak a piano roll vezérli a hangokat.

**MIDI bemenet** (a MIDI-sávon):
- **Dobok (GM kiosztás):** B0 sub · C1 kick · C#1 rim · D1 / E1 snare · D#1 clap · F1, G1, A1, B1, C2, D2 tom (mélytől a magasig) · F#1 / G#1 closed HH · A#1 open HH · C#2, D#2, E2, G2, A2, B2 cymbal · G#2 cowbell. A leütés ereje a hangerőt is állítja.
- **Basszus:** bármelyik hang. Az egymásba érő hangok slide-dal csúsznak át, az erős leütés (kb. 108 felett) accent.
- **1. csatorna dob, 2. csatorna basszus:** egy sávról mindkettő.

Ableton alatt a legegyszerűbb, ha két példányt teszel be: az egyik MIDI módja „Dobok”, a másiké „Basszus”.

## Lépésszerkesztés és keverés

A dobrács feletti **Lépés szerkesztése** sorban választod ki, mit állítasz a lépéseken:
- **Erő** és **Esély** (0–100%), **Mikro** (±30 ms): húzd fel-le a lépést, dupla kattintás visszaállítja.
- **Ismétlés** (1–8 ütés), **Feltétel** (1:2, 1:4 … csak a megadott körökben szól), **Flam** (halk előütés 20 ms-mal előtte): kattintással.
- **P·Hang**, **P·Lecseng.**, **P·Szín**: parameter lock, a lépés saját hangolása, lecsengése és hangszíne. Dupla kattintás feloldja.

Hangonként (a hang nevére kattintva): swing (1/4, 1/8, 1/16), szaturáció (FET, cső, trafó), Humanizer, saját **Hossz** (poliritmus, például 15 lépéses hi-hat a 16-os kick mellett) és a pluginban **Kimenet: Külön**.

**Keverő:** dobok és basszus hangereje, dobbusz-kompresszor, accent drive (a kiemelt ütések erősebben hajtják a szaturációt), sidechain (a kick lenyomja a basszust) és annak elengedése.

**Külön kimenetek:** egy hangnál válaszd a **Külön** kimenetet, és a DAW-ban kapcsold be a plugin megfelelő kimenetét (Ableton: egy új audiosáv bemeneténél a KG-16 sávját és a hang nevét válaszd; Cubase: az instrument-sáv kimeneti listájában aktiváld). A különválasztott hang kimarad a fő kimenetből.

## Saját minták

A **Saját** karakternél a **Minták feltöltése** gomb fájlválasztót nyit; egy hanghoz több fájl is kijelölhető (WAV, AIFF, FLAC, MP3). A plugin a fájlok helyét a projektbe menti, ezért a mintákat ne töröld és ne helyezd át, különben a projekt megnyitásakor nem találja őket.

## Fejlesztés

- `ui/index.html` a plugin felülete. A forrása a böngészős prototípus; frissítés: `scripts/sync-ui.sh útvonal/a/prototípushoz/index.html`.
- `Source/Engine.h` a hangmotor (a prototípus WebAudio-felépítésének C++ megfelelője), `Source/State.h` a felület állapotát olvassa be.
- Helyi fordítás (Mac, Xcode parancssori eszközökkel és CMake-kel):
  `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`
- A JUCE 8 keretrendszert a CMake magától letölti. Kereskedelmi terjesztés előtt nézd át a JUCE licencfeltételeit (juce.com/legal).
