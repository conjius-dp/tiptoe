# Denoiser

## Project
JUCE 8.0.12 audio plugin (VST3, AU, Standalone) — general-purpose spectral gating denoiser with stereo in/out.

## Build
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Rules
- TDD everything — write tests before implementation.
- Never commit, push, or create PRs without explicit user approval.
