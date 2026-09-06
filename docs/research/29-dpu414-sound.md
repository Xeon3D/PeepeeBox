# 29 — DPU-414 mechanical sound

The printer sound is synthesized at run time. No part of any reference
recording is shipped, decoded or used as a wavetable. The four recordings
establish cadence and spectrum; Seiko's service documentation establishes the
mechanisms that can make them.

## 1. What the printer actually is

The DPU-414 is a **serial-dot thermal** printer, not the stationary thermal line
head common in receipt printers. Seiko's specifications give a 9 × 320-dot head,
a 7-dot-wide character plus one-dot space, logical seek, separate normal and
condensed modes, and at most **52.5 characters/s** in normal text. The horizontal
and vertical dot pitch is 0.28 mm. These figures agree between the
[current Seiko product archive](https://www.sii.co.jp/sps/product/oldproduct/dpu414/),
the newly supplied [service manual](../../Seiko%20DPU-414%20Service%20Manual%20.pdf),
and the bundled
[user guide](../../Manuals/THERMAL-PRINTER-DPU-414-USER-S-GUIDE.pdf).

The service manual's exploded drawings and diagnostics identify two separate
motors: a head-scanning motor moving the carrier through its FPC and a geared
paper-feed motor driving the platen. It separately diagnoses abnormal noise
during head scanning and paper feeding. Most importantly for the sound model,
the controller schematic exposes four drive outputs for each motor: CM1–CM4 for
the carriage and FM1–FM4 for the feed. The service checks measure 4.88 ± 0.5 V
between each motor common and each phase. The earlier description of the head as
a two-phase motor was therefore unsupported; “2-2 phase” elsewhere in the manual
describes a thermal-head heating mode, not the motor wiring.

This construction points to two different acoustic events on every line:

1. a long, nearly pitched carriage traverse whose duration depends on logical
   seek distance; then
2. a shorter, coarser geared paper advance.

## 2. Measurements from the four references

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

`DPU-414 Printer Test - 지마스터 (720p, h264).mp4` separates manual feed,
idle and self-test phases. Its self-test has a weaker carriage band around
401 Hz and a broad low mechanical group near 86 Hz. The difference is plausibly
unit condition, supply/battery speed, load and camera transfer rather than a
different mechanism.

`The Pretty Perfect Portable Printing Parade (PPPPP) for r_retrobattlestations
Portable Week - Abraham Moller (720p, h264).mp4` supplies the missing clean feed
evidence. Isolated carriage passes measure 406.9 and 419.2 Hz, reinforcing the
existing 420 Hz full-speed target. More importantly, isolated paper advances at
28.48 and 61.16 seconds have a stable **about-150 Hz** pulse train. In the first
advance, its prominent components are 151, 301, 454 and 603 Hz; the upper three
are approximately -3.5, -5.8 and -5.0 dB relative to the fundamental. A second,
longer advance independently puts the fundamental at 149.7 Hz.

`Funke Gerber Cryoskop 1 için DPU 414 Termal Printer tanıtımı - Merkim Group
(720p, h264).mp4` contains narration and room sound over most operations, so it
is not used for fine amplitude calibration. Its repeated carriage and feed
operations are nevertheless a useful qualitative check that these events need
separate pitched and geared textures.

The NSK recording contains traffic, voices and a conspicuous changing band
around 3.6–4.0 kHz. Those sounds do not track head motion or paper advance and
are deliberately excluded. All four videos are AAC camera captures, so their
absolute loudness and high-frequency balance are not treated as calibration.

The paper was also tracked independently of the soundtrack. Frames were scaled
to a fixed raster, the exposed printed region was horizontally high-passed to
remove illumination, and each frame was matched against the preceding frame at
small vertical offsets. In the first two references, ordinary advances occupy
five or six frames at about 30 fps (0.167–0.200 s). Motion in the clean PPPPP
recording aligns with the 150 Hz motor chunks and puts an ordinary advance near
0.20 s; longer chunks correspond to adjacent or blank feed lines.

The new evidence resolves an ambiguity in the old model. Fifteen dots of paper
travel do not imply fifteen audible motor transitions. A 150 Hz pulse train for
about 0.20 s gives roughly 30 transitions, or two recorded excitations per dot
of travel. This is an empirically calibrated relationship, not a motor sequence
specified by Seiko.

Using the optical intervals as a mask against the soundtrack also shows that
paper motion raises the 0.8–1.5 kHz and 4–8 kHz bands. Repeatable broad excesses
are near 1.35 and 3.1 kHz, with a less stable group between 6 and 8 kHz. Salient
wide-band transients last about 7 ms. Those figures are relative: camera gain,
speech and room noise make absolute sound pressure unrecoverable.

## 3. Procedural model

The source/filter split follows established real-time friction synthesis: a
noise-like contact source drives a resonant object model. Paper-sound research
also distinguishes sliding friction from discrete buckling events. This roll is
transported smoothly rather than crumpled, so the model uses continuous friction
and small start/stop flexes instead of a dense field of artificial crumpling
pops. See [Avanzini, Serafin and Rocchesso (2005)](https://doi.org/10.1109/TSA.2005.852984)
and [Cirio et al. (2016)](https://www.cs.columbia.edu/cg/crumpling/).

The implementation in `src/device/prn_cp80.c` has two event-driven motor
oscillators, a paper source and a front-panel control transient:

- **Head carriage:** 420 Hz at full speed, primarily sinusoidal with small
  second and third harmonics matching the measured harmonic falloff. Each motor
  transition excites two damped synthetic case modes at 910 and 2380 Hz. A tiny
  deterministic high-passed noise component represents drive and carriage
  grain without introducing a noise sample. The recordings also have a rapid
  ticking texture beneath the pitch, with weak broadband-envelope energy around
  the 50--53 Hz normal-character cadence. Every eighth transition therefore
  gives the case modes one stronger, short excitation, keeping the ticks
  distinct without adding a sampled click or a second sustained oscillator.
- **Paper feed:** a 150 Hz geared pulse train lasting 30 transitions. Its
  fundamental, second, third and fourth harmonics reproduce the measured
  150/300/450/600 Hz stack. Thirty transitions at 150 Hz produce a 200 ms
  advance for the documented default distance of nine character dots plus six
  spacing dots.
- **Paper:** deterministic noise is high- and low-pass filtered into sliding
  friction whose colour and level follow paper speed. A separate slow noise
  process varies contact pressure, the motor transitions modulate it shallowly,
  and small stochastic fibre releases excite short synthetic modes at 1.35, 3.1
  and 6.9 kHz. Start/stop flexes are larger. No recorded paper, impulse response
  or wavetable is embedded.
- **Tear-off:** a 460 ms independent event models a brisk diagonal pull across
  the documented 112 mm paper width. The first 320 ms combine continuous filtered sheet
  friction with irregular 1.1--3.3 ms fibre-failure bursts separated by
  4.5--12 ms; the final 140 ms move into a lower, 310 Hz-limited release
  flutter. The last connected fibres excite one stronger transient and the
  same short paper modes used by the feed. Burst intervals, strengths and
  texture are deterministic, avoiding both a looped zipper pitch and a sampled
  sound effect. The matching UI movement keeps the rightmost cutter tooth as a
  pivot while the receipt peels diagonally, then releases with a small flex,
  moving shadow and fade. Its bottom silhouette uses an irregular tooth profile
  instead of disappearing as a rectangular widget.
- **Buttons:** pressing either front-panel momentary switch excites short damped
  plastic and contact modes at 680 Hz and 2.85 kHz, with a one-millisecond
  filtered-noise snap. This path is independent of the motor queue, so the
  tactile click is immediate during printing and still occurs while power is
  off. It is procedural rather than derived from a reference recording.

The deliberate press visible near 7.405 s in the Korean printer-test video has
a 5 ms peak of about -15.7 dBFS. The mechanism immediately after it reaches
about -14.7 dBFS, so their transient peaks are within roughly 1 dB even though
the much shorter button sound has far less total energy. Absolute camera gain
does not transfer to the emulator; this relative relationship does. The
synthesized click is therefore gain-matched to approximately the feed model's
peak and weighted toward its low plastic mode, while retaining the brief
broadband edge visible in the recording's spectrogram.

A print event is created only when a rendered line reaches the paper; the FEED
button creates a feed-only event. Carriage duration is eight transitions per
occupied column, so short lines stop sooner. Direction alternates after each
pass to represent bidirectional logical seek. Ink occupancy changes load
slightly, and the existing battery model lowers motor speed as the pack fades.
This keeps the sound locked to the visible paper rather than to serial bytes
that may still be buffered.

The mixer queue is bounded and all pseudo-random grain is seeded, so a given
print sequence is repeatable. Mechanical sound is enabled by default in the
device configuration and can be suppressed for a run with
`PEEPEEBOX_PRN_SOUND=0`.

## 4. Confidence and limits

The 420 Hz maximum head rate, line-duration formula, two-motor structure,
four-drive-output wiring, 15-dot default feed distance, approximately 150 Hz
feed spectrum and approximately 0.20 s feed duration are now high-confidence
results supported by documentation and/or independent recordings. The mapping
of two audible transitions to one dot of paper travel, paper resonances and
relative level remain tuned inferences. The carriage-tick grouping is likewise
a restrained fit to a weaker feature in camera audio: eight transitions per
normal character is documented, but Seiko does not identify a separate contact
at that boundary. It could instead be drive/load modulation. A direct
close-miked electrical and acoustic capture would still improve transient
levels and phase relationships. The tear duration, fibre-burst density and
release level are physically grounded synthesis choices rather than measurements
from the four camera recordings, none of which provides a clean, close-miked
tear across the full bar.
