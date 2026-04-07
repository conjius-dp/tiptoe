# Denoiser

A general-purpose spectral gating denoiser audio plugin. Learns a noise profile from a sample of background noise, then attenuates matching frequencies in real time.

Available as **VST3**, **AU**, and **Standalone** formats with stereo input/output.

## How It Works

1. Click **Learn Noise** during a section that contains only the noise you want to remove.
2. Click again to stop learning.
3. Adjust **Threshold** (how aggressively frequencies are gated) and **Reduction** (how much gated frequencies are attenuated in dB).

The plugin uses an FFT-based spectral gate with a 2048-sample Hann window and 50% overlap-add for artefact-free reconstruction.

## Dependencies

All dependencies are fetched automatically by CMake at configure time.

| Dependency | Version | Link |
|---|---|---|
| JUCE | 8.0.12 | [juce-framework/JUCE@8.0.12](https://github.com/juce-framework/JUCE/releases/tag/8.0.12) |
| Catch2 | 3.5.2 | [catchorg/Catch2@v3.5.2](https://github.com/catchorg/Catch2/releases/tag/v3.5.2) |
| CMake | >= 3.22 | [cmake.org](https://cmake.org/download/) |
| C++ compiler | C++17 | Clang, GCC, or MSVC |

## Build

```bash
git clone https://github.com/conjius/denoiser.git
cd denoiser
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Build Outputs

Plugins are built to `build/Denoiser_artefacts/` and automatically copied to your system plugin directories:

| Format | macOS Location |
|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |
| AU | `~/Library/Audio/Plug-Ins/Components/` |
| Standalone | `build/Denoiser_artefacts/Standalone/` |

## Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target DenoiserTests
./build/DenoiserTests
```

22 test cases covering FFT round-trip accuracy, noise profile learning, spectral gating behaviour, overlap-add continuity, processing latency measurement, and performance.

Run benchmarks:

```bash
./build/DenoiserTests "[!benchmark]"
```

## Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| Threshold | 0.5x -- 5.0x | 1.5x | Multiplier on the noise profile. Higher = more aggressive gating. |
| Reduction | -60 dB -- 0 dB | -30 dB | Attenuation applied to gated frequency bins. |

## License

All rights reserved.
