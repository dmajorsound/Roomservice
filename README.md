# Ahh! 1.3.3 – stereo and 5.1 in one (formerly Roomservice) – nativer VST3/AU/CLAP-Port (JUCE)

Nativer C++-Port von `Roomservice.jsfx` (Version 1.3.3, Stereo and 5.1) als VST3 und CLAP (macOS zusätzlich AU).
Audio und Oberfläche sind die der JSFX. Es läuft keine EEL2-Engine mit, der ganze Code ist kompiliertes C++.

## Wie der Port gebaut ist

Der JSFX-Code (≈ 1 MB EEL2, 552 Funktionen, 3310 globale Variablen) wird **maschinell** nach C++ übersetzt:
`tools/transpile.py` → `gen/`. Der Übersetzer bildet die Rechenregeln des REAPER-EEL2-x64-JIT exakt nach:

- Operator-Rangfolge aus `eel2.y` (z. B. `a*b/c` = `a*(b/c)`, `a+b-c` = `a+(b-c)`, Unär-Minus vor `^`)
- Optimierer-Umformungen aus `nseel-compiler.c`, die Zahlen verändern (`x/0` → `x*0`, `0*x` → 0, `(a^b)^c` → `a^(c*b)` …)
- unscharfe Vergleiche (`==` ±0,00001), Wahrheitswert |x| ≥ 0,00001, `%` mit Beträgen, Bitoperationen auf int64
- Zuweisung löscht Denormals, Inf und NaN (wie `nseel_asm_assign`)
- `local()`-Variablen behalten ihren Wert zwischen Aufrufen; Parameter-Auswertung in EEL-Reihenfolge
- 8M-Slot-Speicher mit Index-Regel `trunc(x + 0,00001)`, Strings mit den JSFX-Nummernbereichen
- `@serialize` als float32, wie REAPER/ysfx

`src/rt.*` ist die Laufzeit (Speicher, Strings, FFT aus WDL, Serialisierung), `src/engine.*` der JSFX-Host-Ablauf
(@init / @slider / @block / @sample, Transport, `ext_noinit`). `src/JuceGfx.*` bildet die gfx-/LICE-Befehle auf JUCE ab,
`src/PluginEditor.*` ruft `@gfx` mit 30 fps auf (Maus, Tastatur, Retina-Skalierung über `gfx_ext_retina`).

Den generierten Code ändert man nicht von Hand: JSFX ändern → `python3 tools/transpile.py jsfx/Ahh.jsfx gen` → bauen.

## Bauen

Voraussetzungen: CMake ≥ 3.22, C++17-Compiler, JUCE (getestet mit 9.0.5, Commit `d60b7a7`).

```
git clone https://github.com/juce-framework/JUCE.git ../juce
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Roomservice_VST3      # macOS zusätzlich: Roomservice_AU
```

Ergebnis: `build/Roomservice_artefacts/Release/VST3/Roomservice.vst3`.

**macOS und Windows ohne eigenen Rechner:** Projekt in ein GitHub-Repo legen. Der Workflow `.github/workflows/build.yml`
baut dann VST3 + AU (macOS, Universal, ad-hoc signiert) und VST3 (Windows) und legt sie als Artefakte ab.

Wichtig: `-ffp-contract=off` (bzw. `/fp:precise`) nicht entfernen, sonst stimmt die Arithmetik nicht mehr mit der JSFX überein.

## Tests (Linux)

- `test/refrender.cpp`: Original-JSFX über ysfx (auf 256 Slider + `slider_automate` mit 2 Argumenten gepatcht)
- `test/natrender.cpp`: derselbe Ablauf mit dem generierten C++
- `test/runcases.sh`: Null-Test aller Fälle in `test/cases/` (21 Einstellungen über alle Module)
- `test/host/`: kleiner VST3-Host: Null-Test durch das fertige Binary, Zustands-Rundlauf, Editor auf dem Bildschirm
- `test/xinput.py`: Klicks/Drags per XTest (für die Oberfläche unter Xvfb)

## Stand

- 84 Null-Tests C++ gegen JSFX bitgleich (21 Fälle × 44,1/48/96 kHz, Blockgrößen 33/64/256/1024)
- 21 Null-Tests durch das gebaute VST3-Binary bitgleich; Zustand speichern/laden byte-identisch
- Oberfläche: Basic- und Expert-Ansicht gezeichnet, Klicks und Drags (Quelle im Stage View) funktionieren,
  Drags melden Touch-Automation an den Host
- Getestet nur unter Linux (Xvfb, eigener Test-Host). Nicht getestet: REAPER, andere DAWs, macOS, Windows, Retina.
