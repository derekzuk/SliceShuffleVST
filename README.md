# Slicer

BPM-based WAV slicer: slice a WAV on a BPM grid, rearrange slices randomly. Built with **JUCE** (CMake-first). Targets: **CLI** (offline) and **VST3** (e.g. FL Studio).

## Project layout (best practices)

- **CMake-first** — no Projucer; uses JUCE’s official CMake API.
- **JUCE as submodule** — pinned at a known commit for reproducible builds.
- **Out-of-source builds** — all build artifacts go in `build/` (gitignored).
- **Separated layers:**
  - `src/dsp/` — pure slicing/shuffle logic, **no JUCE** (unit-testable without a DAW).
  - `src/cli/` — console app: load WAV → slice by BPM → shuffle → write WAV.
  - `src/plugin/` — VST3 (+ Standalone): `AudioProcessor` + editor, uses the same DSP core.

## Requirements

- **CMake** 3.22+
- **C++17** (or as required by JUCE)
- **JUCE** (added as submodule; see below)
- Platform: macOS, Windows, or Linux with a suitable toolchain (Xcode, MSVC, etc.)

## One-time setup

### 1. Clone with JUCE submodule

```bash
git clone --recurse-submodules https://github.com/your-org/slicer.git
cd slicer
```

If the repo is already cloned without submodules:

```bash
git submodule update --init --recursive
```

### 2. Add JUCE as submodule (if starting from an empty repo)

From the repo root:

```bash
git init
git submodule add https://github.com/juce-framework/JUCE.git juce
```

Pin a specific tag (recommended):

```bash
cd juce
git checkout 7.0.9   # or latest stable tag
cd ..
git add juce
git commit -m "Pin JUCE to 7.0.9"
```

## Build (out-of-source)

From the **project root** (not inside `src/`):

```bash
cmake -B build
cmake --build build
```

- **CLI:** `build/src/SlicerCli_artefacts/SlicerCli` (with Make/Ninja; Xcode/VS use `Debug/` or `Release/` under that).
- **Plugin:** `build/SlicerPlugin_artefacts/Debug/VST3/Slicer.vst3` (or `Release/`).

Optional: use a generator explicitly:

```bash
cmake -G Xcode -B build
cmake -G "Ninja" -B build
```

## Run

- **CLI (stub):**  
  `./build/src/SlicerCli_artefacts/SlicerCli input.wav output.wav --bpm 120`  
  (Full usage to be implemented: `SlicerCli input.wav output.wav --bpm 120`.)

- **Plugin:** Load `Slicer.vst3` in FL Studio (or any VST3 host).

## Roadmap

1. **CLI:** Implement WAV load → slice by BPM (using `SlicerEngine`) → shuffle → write WAV.
2. **Plugin:** Use the same DSP in `processBlock` with preloaded buffer; no file I/O on the audio thread. Add **APVTS** and stable parameter IDs from day one for host compatibility.

## License

Your choice. JUCE is GPL/Commercial — ensure your use complies with JUCE’s license.
