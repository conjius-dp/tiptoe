# Denoiser

## Project
JUCE 8.0.12 audio plugin (VST3, AU, Standalone) — general-purpose spectral gating denoiser with stereo in/out.

## Dependencies
- JUCE 8.0.12 (git submodule)
- CMake >= 3.22
- C++17 compiler
- Catch2 v3.5.2 (fetched by CMake)

## Build
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Tests
```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target DenoiserTests
./build/DenoiserTests
```

Run benchmarks:
```
./build/DenoiserTests "[!benchmark]"
```

## Architecture
- `Source/DSP/SpectralGateDenoiser.h/.cpp` — Core DSP: FFT overlap-add (2048 Hann, 50% overlap), noise learning, spectral gating.
- `Source/PluginProcessor.h/.cpp` — JUCE plugin wrapper, APVTS parameters (threshold, reduction), stereo processing.
- `Source/PluginEditor.h/.cpp` — GUI: two rotary knobs + Learn Noise button.
- `Tests/TestSpectralGateDenoiser.cpp` — 19 Catch2 test cases.

## Rules
- TDD everything — write tests before implementation.
- Never commit, push, or create PRs without explicit user approval.
