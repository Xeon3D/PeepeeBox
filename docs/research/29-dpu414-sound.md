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

## 3. Procedural model

The implementation in `src/device/prn_cp80.c` has two event-driven oscillators:

- **Head carriage:** 420 Hz at full speed, primarily sinusoidal with small
  second and third harmonics matching the measured harmonic falloff. Each motor
  step excites two damped synthetic case modes at 910 and 2380 Hz. A tiny
  deterministic high-passed noise component represents drive and carriage
  grain without introducing a noise sample.
- **Paper feed:** a 150 Hz harmonic-rich geared stepper lasting 15 steps: nine
  vertical character dots plus the manual's default six-dot line spacing. The
  service manual does not specify this motor's pulse rate, so 150 Hz is an
  inference from the repeatable 80–190 Hz low-motor group and its harmonics.

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

The 420 Hz head rate, line-duration formula, two-motor structure and 15-dot
default feed distance are high-confidence results supported by independent
documentation and recording measurements. The paper motor's 150 Hz rate, case
resonances and relative level are tuned inferences: neither Seiko document gives
an acoustic spectrum or paper-motor pulse rate. A direct close-miked recording
or motor tachometer capture would be needed to calibrate those parts more
precisely.
