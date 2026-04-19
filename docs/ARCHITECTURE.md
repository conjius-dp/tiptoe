# Tiptoe — Technical Walkthrough

A real-time spectral-gating denoiser. You capture a noise profile from a
silent-ish section, then every FFT bin whose magnitude sits under the learned
noise × a threshold multiplier is attenuated by a reduction gain. The UI
shows the learned curve, the gating threshold, and the live input spectrum in
real time.

---

## 1. Architecture at a glance

```
┌──────────────────────────────────────────────────────────────┐
│ TiptoeAudioProcessor  (juce::AudioProcessor)                 │
│                                                              │
│   AudioProcessorValueTreeState   ─┐                          │
│   ├─ threshold (0.5 .. 5.0)       │  parameters              │
│   └─ reduction (-60 .. 0 dB)      │                          │
│                                   ▼                          │
│   SpectralGateTiptoe  gates[2]    ◄── audio thread           │
│     (one per stereo channel)                                 │
│                                                              │
│   audio → processBlock() ──► gates[ch].processMono()         │
│                          or  gates[ch].learnFromBlock()      │
└──────────────────────────────────────────────────────────────┘
                 │                ▲
           lock-free              │ knob values
           snapshots              │ via APVTS
                 ▼                │
┌──────────────────────────────────────────────────────────────┐
│ TiptoeAudioProcessorEditor  (JUCE AudioProcessorEditor)      │
│                                                              │
│   SpectrumGraph   (top ~36% of the window)                   │
│   AnimatedSlider ×2  (threshold + reduction knobs)           │
│   TextButton  learnButton                                    │
│   Label       latencyLabel                                   │
│                                                              │
│   Timer @ 30 Hz ── pulls latest noise + input magnitudes     │
│                    copies threshold value from APVTS         │
│                    calls SpectrumGraph::setSnapshot          │
└──────────────────────────────────────────────────────────────┘
```

Two clean boundaries:

- **Audio thread** owns `SpectralGateTiptoe` and runs FFTs per frame.
- **UI thread** only reads snapshots; never touches the DSP data directly.

---

## 2. DSP: the spectral gate

### Configuration (constants from `SpectralGateTiptoe.h`)

```cpp
static constexpr int kFFTOrder = 11;
static constexpr int kFFTSize  = 1 << kFFTOrder;     // 2048
static constexpr int kHopSize  = kFFTSize / 2;       // 1024 (50% overlap)
static constexpr int kNumBins  = kFFTSize / 2 + 1;   // 1025
static constexpr int kFFTMask  = kFFTSize - 1;       // power-of-2 wrap mask
```

- **Frame size 2048**: ~46 ms of spectral context at 44.1 kHz — good
  time/frequency trade-off for broadband noise.
- **50% overlap + Hann**: the classic COLA (Constant OverLap-Add) condition,
  sum-to-unity: `Σ w[n − kH]² = 1`, so *no synthesis window is needed* and
  overlap-add reconstructs the input exactly when bins aren't modified.

### Overlap-add pipeline

```
time ──►
Audio samples: ─┬────────┬────────┬────────┬────────┬────────
                │                 │                 │
           inputFifo        inputFifo        inputFifo      …  (circular, 2048 samples)
                │                 │                 │
     ┌──────────┼────┐    ┌───────┼────┐    ┌──────┼─────┐
     │ frame 0  ▼    │    │frame 1▼    │    │frame 2▼    │     (2048-sample windows,
     │ [──── FFT ────┤    │ [──── FFT ─┤    │ [──── FFT ─┤      advanced by 1024 each)
     │ 1025 bins     │    │            │    │            │
     │ gate each bin │    │            │    │            │
     │ IFFT          │    │            │    │            │
     └───────┬───────┘    └───────┬────┘    └───────┬────┘
             │                    │                 │
             ▼                    ▼                 ▼
      outputFifo ◄─── overlap-add: new frame summed in at its offset
                   ──► samples are pulled out sample-by-sample
```

### Per-sample loop inside `processMono`

```cpp
for (int i = 0; i < numSamples; ++i)
{
    inputFifo[inputFifoIndex] = samples[i];         // write input
    samples[i] = outputFifo[inputFifoIndex];        // read processed output
    outputFifo[inputFifoIndex] = 0.0f;              // and clear that slot

    ++inputFifoIndex;
    ++hopCounter;

    if (hopCounter >= kHopSize) {                   // every 1024 samples,
        hopCounter = 0;
        processFrame();                             //   do one FFT frame
    }

    inputFifoIndex &= kFFTMask;                     // wrap via bitmask (no modulo)
}
```

Why `&= kFFTMask` instead of `% kFFTSize`? `kFFTSize` is a power of two so the
mask is equivalent, and `&` is a single cycle on any CPU.

### One frame (`processFrame`)

```cpp
void SpectralGateTiptoe::processFrame()
{
    // Gather the 2048-sample window from the circular FIFO, apply the Hann.
    for (int i = 0; i < kFFTSize; ++i) {
        int idx = (inputFifoIndex + i) & kFFTMask;
        fftWorkspace[i] = inputFifo[idx] * window[i];
    }
    std::fill(fftWorkspace.begin() + kFFTSize, fftWorkspace.end(), 0.0f);

    fft.performRealOnlyForwardTransform(fftWorkspace.data());

    //     ┌─── snapshot per-bin magnitudes for the UI before gating ────┐
    //     │                                                            │
    //   publish inputMagSnapshots_[writeIdx]                           │
    //   flip inputMagWriteIndex_                                       │
    //     └───────────────────────────────────────────────────────────┘

    if (hasNoiseProfile_) {
        for (int i = 0; i < kNumBins; ++i) {
            float re = fftWorkspace[i * 2];
            float im = fftWorkspace[i * 2 + 1];
            float magSq = re*re + im*im;                     // |X|²
            float thresholdSq = noiseProfileSq_[i] * thresholdSq_;
            if (magSq < thresholdSq) {                       //  ┌─ gate ─┐
                fftWorkspace[i * 2]     *= reductionGain_;   //  │  bin   │
                fftWorkspace[i * 2 + 1] *= reductionGain_;   //  │  out   │
            }                                                //  └────────┘
        }
    }

    fft.performRealOnlyInverseTransform(fftWorkspace.data());

    // Overlap-add back into outputFifo at the current wrap position.
    for (int i = 0; i < kFFTSize; ++i) {
        int idx = (inputFifoIndex + i) & kFFTMask;
        outputFifo[idx] += fftWorkspace[i];
    }
}
```

Comparing *squared* magnitudes and *squared* thresholds avoids the `sqrt` per
bin — that's why `noiseProfileSq_[i] = noiseProfile_[i]²` and
`thresholdSq_ = thresholdMultiplier²` are precomputed on the setter. Per-bin
we save both a `sqrt` and a multiply.

### The gating rule (per bin)

```
                ┌──────────────────────────┐
                │  input FFT bin magnitude │
                │        |X[k]|²           │
                └─────────────┬────────────┘
                              │
                              ▼
   ┌─ compare ─────────────────────────────────────┐
   │  |X[k]|²   vs   (noiseProfile[k] × thr)²      │
   └──────────────┬──────────────┬────────────────┘
                  │              │
             below (noise)   above (signal)
                  │              │
                  ▼              ▼
   X[k] *= reductionGain     X[k] unchanged
   (attenuates the bin)       (passes through)
```

`reductionGain = 10^(reductionDB / 20)`, set when the `reduction` knob moves.

---

## 3. Noise learning

When the user hits **START**, `TiptoeAudioProcessor::startLearning()` flips a
flag. `processBlock` then short-circuits:

```cpp
if (learning_) {
    for (int ch = 0; ch < std::min(totalNumInputChannels, 2); ++ch)
        gates[ch].learnFromBlock(buffer.getReadPointer(ch), numSamples);
    return;                   // skip gating; host hears the dry input
}
```

`learnFromBlock` has its own FIFO so it can produce windowed frames
independently of the processing pipeline:

```cpp
void SpectralGateTiptoe::learnFromBlock(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        learnFifo_[learnFifoIndex_++] = samples[i];
        if (learnFifoIndex_ >= kFFTSize) {
            learnFrame(learnFifo_.data());                 // one FFT
            // slide the FIFO by kHopSize (50% overlap)
            std::copy(learnFifo_.begin() + kHopSize,
                      learnFifo_.end(),
                      learnFifo_.begin());
            learnFifoIndex_ = kFFTSize - kHopSize;
        }
    }
}
```

Each `learnFrame` computes per-bin magnitudes and accumulates:

```cpp
for (int i = 0; i < kNumBins; ++i) {
    float mag = std::sqrt(re*re + im*im);
    noiseAccumulator_[i] += mag;              // summed over frames
}
++noiseFrameCount_;
```

When the user clicks **STOP**, `stopLearning()` averages:

```cpp
noiseProfile_[i]   = noiseAccumulator_[i] / noiseFrameCount_;
noiseProfileSq_[i] = noiseProfile_[i] * noiseProfile_[i];
```

The profile is the **mean magnitude per bin** across all learned frames.
`noiseProfileSq_` is precomputed so the hot processing loop can compare
squared values.

```
learning flow
─────────────

 user hits START ── startLearning() sets learning_ = true, zeroes accumulator
                          │
                          ▼
 audio in ─── processBlock ──► learnFromBlock
                                    │
                                    ▼  (every 2048 samples, hop 1024)
                                learnFrame
                                    │  Hann window → FFT → |X[k]|
                                    ▼
                          noiseAccumulator_[k] += |X[k]|
                          noiseFrameCount_++
                          publish running-average snapshot to UI
                                    ▲
                                    │
                   many frames accumulate ~stationary noise
                                    │
 user hits STOP  ──► stopLearning() divides accumulator by frameCount →
                     noiseProfile_[k] = mean magnitude, hasNoiseProfile_ = true
```

---

## 4. UI ↔ DSP: lock-free spectrum snapshots

The editor reads the latest magnitudes on a 30 Hz timer. The audio thread
must never block — so we use a classic **double-buffered lock-free handoff**:

```
inputMagSnapshots_ [2]   ─┬─ two std::vector<float> of kNumBins
                         └─ one active (reader), one inactive (writer)

atomic<int> inputMagWriteIndex_   ─── which slot the writer just published
```

Audio-thread write (in `processFrame` / `learnFrame`):

```cpp
const int writeIdx = (inputMagWriteIndex_.load(relaxed) + 1) & 1;
auto& dst = inputMagSnapshots_[writeIdx];
for (int i = 0; i < kNumBins; ++i)
    dst[i] = std::sqrt(re*re + im*im);
inputMagWriteIndex_.store(writeIdx, memory_order_release);   // publish
```

UI-thread read (in the timer):

```cpp
const int readIdx = inputMagWriteIndex_.load(memory_order_acquire);
out = inputMagSnapshots_[readIdx];                  // copy
```

- **`release` / `acquire`** pairing guarantees the reader sees all the bin
  writes that happened before the index flip.
- **No locks, no allocation on the audio thread** — the writer just
  overwrites the inactive vector in place.
- At most one stale frame is visible to the UI, which is invisible at 30 Hz.

Same pattern is used for the running noise profile during learning:

```
learnFrame
    │ (sums one frame into noiseAccumulator_, increments frameCount)
    │
    ▼
noiseSnapshots_[(noiseWriteIndex_+1) & 1][k] = accumulator[k] / frameCount
noiseWriteIndex_.store(..., release)
```

So the UI gets a running average every learn frame — that's how the noise
curve visibly "fills in" while the user is learning.

---

## 5. The editor timer

```cpp
void TiptoeAudioProcessorEditor::timerCallback()
{
    // Latency text (throttled to ~5 Hz)
    if (++latencyTick >= 12) { /* ... */ latencyLabel.setText(/* ... */); }

    // Spectrum graph
    const float thr = processorRef.getAPVTS()
                          .getRawParameterValue("threshold")->load();
    spectrumGraph.setThresholdMultiplier(thr);

    processorRef.copyInputMagnitudes(scratchInputMags);   // lock-free read
    processorRef.copyNoiseProfile(scratchNoiseMags);      // lock-free read
    spectrumGraph.setSnapshot(scratchNoiseMags, scratchInputMags);

    // Hover-colour animations (knobs + learn button) interpolated here
    // Knob double-click snap-to-default animation stepped here
    // Latency label hide/peek animation stepped here
}
```

The APVTS parameter is read atomically — `getRawParameterValue()` returns
`std::atomic<float>*`. Knobs are bound to parameters via `SliderAttachment`,
so rotating the knob feeds the value back to APVTS, which the audio thread
reads in `processBlock`.

---

## 6. Spectrum graph

`SpectrumGraph` holds three arrays and draws three paths:

```
─ noise profile     (darker orange, 2.5 px stroke)  — learned baseline
─ threshold curve   (mid orange,    1.5 px stroke)  — noise × thr knob
─ live input        (bright orange, 1.0 px stroke)  — with EMA smoothing
```

### Axes

```
  dB  ▲
  0   │           ◄─── magRef() = kFFTSize / 4  (full-scale sine peaks here)
      │  │││││
      │  │││││││
      │ ╱      ╲
 −20  │╱        ╲╲
      │           ╲
 −40  │            ╲
      │             ╲╲
 −60  │               ╲
      │                ╲╲
 −80  ┼─────────────────────────►  freq  (log scale)
       20 Hz    100   1k   10k  20 kHz
```

### Bin → pixel mappings (log X, dB Y)

```cpp
float freqToX(float f, float width) const {
    f = clamp(f, 20.f, 20000.f);
    float t = (log10(f) - log10(20.f)) / (log10(20000.f) - log10(20.f));
    return t * width;
}

float magToY(float mag, float height) const {
    float db = 20 * log10(max(mag, floorMag) / magRef());
    float t  = clamp((db - (-80)) / (0 - (-80)), 0.f, 1.f);
    return height - t * height;                 // 0 dB at top, −80 at bottom
}
```

### Path construction — important detail

To make the curve reach **both** borders symmetrically (bin 1 at sample
rate 44.1 kHz is ~21.5 Hz, so `freqToX` gives ~1% from the left), the first
and last in-range bins are captured and the path is anchored at
`area.left` / `area.right` with those Y values:

```cpp
// left anchor
out.startNewSubPath(area.getX(), magToY(mags[firstIn] * scale, h));

for (int bin = firstIn; bin <= lastIn; ++bin)
    out.lineTo(area.getX() + freqToX(binToFreq(bin), w),
               area.getY() + magToY(mags[bin] * scale, h));

// right anchor
out.lineTo(area.getRight(), magToY(mags[lastIn] * scale, h));
```

### Input smoothing

```cpp
const float alpha = 0.35f;
inputSmoothed_[i] = alpha * input[i] + (1 - alpha) * inputSmoothed_[i];
```

One-pole exponential: noisy per-frame magnitudes turn into a smoothly morphing
curve the eye can read.

---

## 7. Parameter plumbing (threshold / reduction)

```
knob drag                  APVTS                       audio thread
────────      SliderAttachment         getRawParameterValue
   │            ──────────►            ───────────►
   │  thresholdSlider           "threshold"                 │
   ▼                                                        ▼
Slider value            std::atomic<float>          gate.setThreshold()
                                                        │
                                                  thresholdSq_ = t*t
                                                        │
                                                        ▼
                                                compared against
                                                noiseProfileSq_[k]
```

- UI → parameter: automatic via JUCE's `SliderAttachment` (no manual callback
  wiring).
- Parameter → DSP: `processBlock` reads the atomics at the top of each block
  and pushes into the gate instances.
- Parameter → UI graph: the editor timer reads the same atomic to slide the
  threshold curve live.

---

## 8. Performance notes

- **Power-of-two bitmask wrap** instead of modulo on the input/output FIFOs:
  `idx & kFFTMask`.
- **Squared magnitude comparison** everywhere in the hot loop: no `sqrt`
  until we emit the visualisation snapshot.
- **Analysis-only Hann** + 50% overlap → synthesis window is unnecessary
  (COLA = 1).
- **`invWindowSum_` is kept as 1.0** and still multiplied — it would let us
  scale if the COLA condition changed, without branching.
- **`std::atomic<int>` write index**, not a mutex, for the UI ↔ DSP handoff.
  Audio thread never allocates or locks.
- **Tests** (`Tests/TestSpectralGateTiptoe.cpp`, 22 cases) verify: silence →
  silence, pass-through is bit-exact when no profile, multiple block sizes
  produce identical output, gating attenuates below and preserves above
  threshold, "must process 1 s of audio in under 50 ms" perf ceiling.

---

## TL;DR mental model

FIFO → windowed FFT → compare each bin to learned floor → attenuate the noisy
ones → IFFT → overlap-add.

Plus a lock-free mirror of the bin magnitudes the UI reads at 30 Hz for the
spectrum graph.
