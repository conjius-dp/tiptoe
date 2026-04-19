# tiptoe

<p align="center">
  <picture><img src="https://conjius-dp.github.io/tiptoe/screenshot.png" width="373" alt="Tiptoe"></picture>
</p>

<p align="center">
  <a href="https://github.com/conjius-dp/tiptoe/releases/latest/download/Tiptoe-macOS-VST3.zip"><img src="Assets/badges/download-vst3-macos.png" height="32" alt="Download VST3 for macOS"></a>
  &nbsp;
  <a href="https://github.com/conjius-dp/tiptoe/releases/latest/download/tiptoe-macOS-AU.zip"><img src="Assets/badges/download-au-macos.png" height="32" alt="Download AU for macOS"></a>
  &nbsp;
  <a href="https://github.com/conjius-dp/tiptoe/releases/latest/download/Tiptoe-Windows-VST3.zip"><img src="Assets/badges/download-vst3-windows.png" height="32" alt="Download VST3 for Windows"></a>
</p>

<p align="center">
  <a href="https://github.com/conjius-dp/tiptoe/actions/workflows/ci.yml"><img src="https://github.com/conjius-dp/tiptoe/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/conjius-dp/tiptoe/actions/workflows/ci.yml"><img src="https://img.shields.io/endpoint?url=https%3A%2F%2Fconjius-dp.github.io%2Ftiptoe%2Fcoverage.json" alt="Coverage"></a>
  <a href="https://github.com/conjius-dp/tiptoe/releases/latest"><img src="https://img.shields.io/github/v/release/conjius-dp/tiptoe?label=stable&color=brightgreen" alt="Stable"></a>
  <a href="https://github.com/conjius-dp/tiptoe/releases"><img src="https://img.shields.io/github/v/release/conjius-dp/tiptoe?include_prereleases&label=nightly" alt="Nightly"></a>
  <a href="https://github.com/conjius-dp/tiptoe/releases"><img src="https://img.shields.io/github/downloads/conjius-dp/tiptoe/total?label=downloads&color=blue" alt="Total downloads"></a>
</p>

<p align="center">
  <a href="https://github.com/conjius-dp/tiptoe/graphs/commit-activity"><img src="https://repobeats.axiom.co/api/embed/ccbb782b0fcf93b609feffe2e10efebcdc9c738a.svg" width="700" alt="Repobeats analytics image"></a>
</p>

Spectral-gating denoiser. Learn a noise profile, then attenuate matching frequencies in real time. 75 % overlap Hann, soft-knee per-bin gate with learned attack/release.

VST3 (macOS, Windows), AU + Standalone (macOS). Stereo in/out.

## Modes

Toggle with the **MODE** pill in the UI (also exposed to the host as the `hq` bool param).

| Mode | Latency @ 44.1 kHz | How it works |
|---|---|---|
| **Realtime** (default) | 162 samples / 3.67 ms | 2 kHz Linkwitz-Riley crossover → 8× decimated low band (FFT 16) + full-rate high band (FFT 128), delay-aligned and summed |
| **HQ** | 512 samples / 11.6 ms | Single-band, FFT 2048 |

Flipping the toggle re-reports `setLatencySamples()` live, so the host re-aligns PDC without a reload. The spectrum display always shows the HQ gate's 257-bin FFT (run in parallel at ~0.5 % CPU in realtime mode) so both modes look identical in the UI.

## Usage

1. **Learn Noise** during a noise-only section.
2. Click again to stop.
3. Dial **Sensitivity** and **Reduction**.
4. Pick **MODE** (realtime vs HQ).
5. Power button (top-right) hard-bypasses the plugin.

## Parameters

| Parameter | Range | Default |
|---|---|---|
| Sensitivity | 0.1× – 3.0× | 1.0× |
| Reduction   | 0 dB – −60 dB | −30 dB |
| HQ          | on / off | off (realtime) |
| Bypass      | on / off | off |

## Build

```bash
git clone https://github.com/conjius-dp/tiptoe.git
cd tiptoe
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build build
```

VST3 / AU bundles are auto-copied to `~/Library/Audio/Plug-Ins/`.

Requires JUCE 8.0.12, CMake ≥ 3.22, a C++17 compiler.

## Tests

```bash
cmake --build build --target TiptoeTests && ./build/TiptoeTests
```

## macOS: plugin won't open after download

macOS quarantines anything downloaded from a browser and blocks unsigned plugins with a Gatekeeper dialog. Strip the quarantine flag once after dropping the plugin into its folder:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/tiptoe.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/tiptoe.component
```

Restart your DAW and the plugin loads silently.
