# 29 — DPU-414 mechanical sound

The printer sound is synthesized at run time. No part of either reference
recording is shipped, decoded or used as a wavetable. The recordings establish
the cadence and spectrum; the service documentation establishes which physical
mechanisms can make them.

## 1. What the printer actually is

The DPU-414 is a **serial-dot thermal** printer, not the stationary thermal line
head common in receipt printers. Seiko's specifications give a 9 × 320-dot head,
a 7-dot-wide character plus one-dot space, logical seek, separate normal and
condensed modes, and at most **52.5 characters/s** in normal text. The horizontal
and vertical dot pitch is 0.28 mm. These figures agree between the
[current Seiko product archive](https://www.sii.co.jp/sps/product/oldproduct/dpu414/)
and the [Seiko DPU-414 service manual](https://manuals.plus/m/cc26274524d0bcbdf00c3be70c720bb5a4c661f2aec129186f68b8a5b196ac23.pdf).

The service manual's exploded drawings and diagnostics identify two separate
motors: a head-scanning motor moving the carrier through its FPC and a geared
paper-feed motor driving the platen. It separately diagnoses abnormal noise
during head scanning and during paper feeding. The head drive is described as a
two-phase drive. That points to two different acoustic events on every line:

1. a long, nearly pitched carriage traverse whose duration depends on logical
   seek distance; then
2. a shorter, coarser geared paper advance.

The user's bundled manual is also preserved at
[`Manuals/THERMAL-PRINTER-DPU-414-USER-S-GUIDE.pdf`](../../Manuals/THERMAL-PRINTER-DPU-414-USER-S-GUIDE.pdf).

## 2. Measurements from the two references

The cleanest segment in `Dpu-414 printer - NSK Marine Services (360p,
h264).mp4` has a persistent **420.98 Hz** narrow-band component. Frame-by-frame
spectral tracking places it at 421–422 Hz through the carriage passes. Its
second and third harmonics, near 844 and 1266 Hz, are about 17–18 dB below the
fundamental. The same track also shows alternating roughly 0.46 and 0.99 second
mechanical groups, consistent with short and long logical-seek lines.

That pitch is independently predicted by the specification:

```
52.5 normal characters/s × (7 printed columns + 1 space) = 420 steps/s
```

This is stronger evidence than simply copying the recording's spectrum: the
published geometry and speed predict the measured motor tone to within the
resolution and speed error of a camera recording.

`DPU-414 Printer Test - 지마스터 (720p, h264).mp4` is useful for separating
manual feed, idle and self-test phases. Its self-test has a weaker carriage band
around 401 Hz and a broad low mechanical group near 86 Hz. The difference is
plausibly unit condition, supply/battery speed, load and camera transfer rather
than a different mechanism.

The NSK recording contains traffic, voices and a conspicuous changing band
around 3.6–4.0 kHz. Those sounds do not track head motion or paper advance and
are deliberately excluded. Both videos are AAC camera captures, so their
absolute loudness and high-frequency balance are not treated as calibration.

The paper was also tracked independently of the soundtrack. Frames were scaled
to a fixed raster, the exposed printed region was horizontally high-passed to
remove illumination, and each frame was matched against the preceding frame at
vertical offsets of zero through five pixels. During the regular part of the
self-test, ordinary advances occupy **five or six frames at 29.927 fps**
(0.167–0.200 s). The second video independently has a median detected advance of
five frames at 30 fps. That makes the earlier 0.100 s feed estimate too short.

Using those optical intervals as a mask against the soundtrack shows that paper
motion raises the 0.8–1.5 kHz band by about 3 dB and the 4–8 kHz band by about
4 dB over the preceding carriage interval. The most repeatable broad excesses
are near 1.35 and 3.1 kHz, with a less stable group between 6 and 8 kHz. The
salient wide-band transients have a median 35%-height duration of about 7 ms.
Those figures are relative: camera gain, speech and room noise make absolute
sound pressure unrecoverable.

## 3. Procedural model

The source/filter split follows established real-time friction synthesis: a
noise-like contact source drives a resonant object model. Paper-sound research
also distinguishes sliding friction from discrete buckling events. This roll is
transported smoothly rather than crumpled, so the model uses continuous friction
and small start/stop flexes instead of a dense field of artificial crumpling
pops. See [Avanzini, Serafin and Rocchesso (2005)](https://doi.org/10.1109/TSA.2005.852984)
and [Cirio et al. (2016)](https://www.cs.columbia.edu/cg/crumpling/).

The implementation in `src/device/prn_cp80.c` has two event-driven motor
oscillators and a paper source:

- **Head carriage:** 420 Hz at full speed, primarily sinusoidal with small
  second and third harmonics matching the measured harmonic falloff. Each motor
  step excites two damped synthetic case modes at 910 and 2380 Hz. A tiny
  deterministic high-passed noise component represents drive and carriage
  grain without introducing a noise sample.
- **Paper feed:** an 85 Hz harmonic-rich geared stepper lasting 15 steps: nine
  vertical character dots plus the manual's default six-dot line spacing. This
  produces a 176 ms advance, inside the independently measured video interval
  and close to the first recording's broad 86 Hz group. The service manual does
  not specify the paper motor's pulse rate.
- **Paper:** deterministic noise is high- and low-pass filtered into sliding
  friction whose colour and level follow paper speed. A separate slow noise
  process varies contact pressure, the motor steps modulate it shallowly, and
  small stochastic fibre releases excite short synthetic modes at 1.35, 3.1
  and 6.9 kHz. Start/stop flexes are larger. No recorded paper, impulse response
  or wavetable is embedded.

A print event is created only when a rendered line reaches the paper; the FEED
button creates a feed-only event. Carriage duration is eight steps per occupied
column, so short lines stop sooner. Direction alternates after each pass to
represent bidirectional logical seek. Ink occupancy changes load slightly, and
the existing battery model lowers motor speed as the pack fades. This keeps the
sound locked to the visible paper rather than to serial bytes that may still be
buffered.

The mixer queue is bounded and all pseudo-random grain is seeded, so a given
print sequence is repeatable. Mechanical sound is enabled by default in the
device configuration and can be suppressed for a run with
`PEEPEEBOX_PRN_SOUND=0`.

## 4. Confidence and limits

The 420 Hz head rate, line-duration formula, two-motor structure, 15-dot default
feed distance and 0.17–0.20 s paper-motion interval are high-confidence results
supported by independent documentation and/or both recordings. The chosen 85
Hz paper-motor rate, paper resonances and relative level remain tuned inferences:
neither Seiko document gives an acoustic spectrum or paper-motor pulse rate. A
direct close-miked recording or motor tachometer capture would be needed to
calibrate those parts more precisely.
