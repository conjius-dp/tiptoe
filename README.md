# tiptoe

<p align="center">
  <a href="https://github.com/conjius-dp/tiptoe/releases/latest"><img src="https://img.shields.io/github/v/release/conjius-dp/tiptoe?label=stable" alt="Stable"></a>
  <a href="https://github.com/conjius-dp/tiptoe/releases"><img src="https://img.shields.io/github/v/release/conjius-dp/tiptoe?include_prereleases&label=nightly" alt="Nightly"></a>
</p>

A general-purpose spectral gating audio plugin. Learns a noise profile from a sample of background noise, then attenuates matching frequencies in real time.

Available as macOS **VST3**, **AU**, and **Standalone** formats with stereo input/output.

## How It Works

1. Click **Learn Noise** during a section that contains only the noise you want to remove.
2. Click again to stop learning.
3. Adjust **Threshold** (how aggressively frequencies are gated) and **Reduction** (how much gated frequencies are attenuated in dB).

The plugin uses an FFT-based spectral gate with a 2048-sample Hann window and 50% overlap-add for artefact-free reconstruction.

## Dependencies

| Dependency | Version | Link |
|---|---|---|
| JUCE | 8.0.12 | [juce-framework/JUCE@8.0.12](https://github.com/juce-framework/JUCE/releases/tag/8.0.12) |
| CMake | >= 3.22 | [cmake.org](https://cmake.org/download/) |
| C++ compiler | C++17 | Clang, GCC, or MSVC |

## Build

```bash
git clone https://github.com/conjius-dp/tiptoe.git
cd tiptoe
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build build
```

### Build Outputs

Plugins are built to `build/Tiptoe_artefacts/` and automatically copied to your system plugin directories:

| Format | macOS Location |
|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |
| AU | `~/Library/Audio/Plug-Ins/Components/` |
| Standalone | `build/Tiptoe_artefacts/Standalone/` |

## Tests

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build build --target TiptoeTests
./build/TiptoeTests
```

22 test cases covering FFT round-trip accuracy, noise profile learning, spectral gating behaviour, overlap-add continuity, processing latency measurement, and performance.

Tests use JUCE's built-in `UnitTest` framework — no external test dependencies.

## Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| Threshold | 0.5x -- 5.0x | 1.5x | Multiplier on the noise profile. Higher = more aggressive gating. |
| Reduction | -60 dB -- 0 dB | -30 dB | Attenuation applied to gated frequency bins. |
