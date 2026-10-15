# Measured dynamics, harmonics and volume preview

## Measurements

On 2026-10-15 the ChromeOS Pujjoteen5 speaker path was observed through ALSA EchoRef hw:0,6 while CRAS playback was pinned to speaker device 7. The preferred HDMI output was retained. The script restored the speaker node volume to 75% after testing; no microphone DSP, firmware or Windows audio settings were changed.

Four 39-second captures used volume 25/50/75/100. Signals: 80, 120, 200 and 1,000 Hz at source peaks -60/-48/-36/-24 dBFS, followed by continuous-phase -48/-24 dBFS steps at 200 and 1,000 Hz. Two further captures at 50/100 used a higher-precision 32-bit source and capture. Both playback and capture PCM formats must match: mixing S16 and S32 produced invalid observations. Those exploratory captures were excluded. Complete accepted recordings reported no capture errors.

The S32 controls confirmed real bass harmonics: at 50%, -24 dBFS input, H3/fundamental was approximately -21.6 dB at 80 Hz, -36.8 dB at 120 Hz and -49.0 dB at 200 Hz. They also showed that much of the apparent 1 kHz distortion in S16 observations was quantization. These measurements identify behavior at the capture point, not which proprietary module creates it, and not acoustic distortion.

At 50% the last 12 dB increase in the 200 Hz source produced about 6.2 dB output increase; at 75%, about 5.2 dB; at 100%, about 4.8 dB. The 1 kHz reference was close to linear. Accounting for the board volume curve largely collapses the bass level curves. This supports a model with volume attenuation before nonlinear bass gain control; it does not prove every stage or control is volume independent.

## Independent implementation

The experimental `--chromeos-volume N` preset enables recovered EQ, the fitted bass correction, three-band LR4 compression (crossovers 150/500 Hz), and a third-harmonic approximation. Volume attenuation is applied before processing, referenced to the original 50% fit. The existing sample limiter remains enabled. The high band has ratio 1; the low/mid bands have fitted thresholds, knees, ratios and makeup gains. These are parameters of our implementation, not decoded vendor settings.

The harmonic branch uses a one-pole low-pass near 172 Hz, 30 ms smoothed input/filtered powers, a normalized cubic with steady sinusoidal fundamental cancellation, and a fitted level-dependent strength. This standard independent model approximates third-harmonic magnitude. It does not reconstruct vendor phase, even harmonics, all higher harmonics, intermodulation or the complete MaxxBass algorithm. On complex signals it can generate intermodulation; listening validation remains necessary.

Steady tone model fitting: training 50/100 volume, 21 usable observations, 0.327 dB RMS / 0.750 dB maximum error. Held-out volume 75, 11 observations, 0.234 dB RMS / 0.374 dB maximum. The held-out volume uses the same test tone set, not new program material. Low-resolution observations were excluded based on signal amplitude. At 25%, very quiet S16 inputs became zero; this is not sufficient evidence for a vendor noise gate.

Rendered H3/fundamental ratio error across 24 S32 bass observations: 2.02 dB RMS, 5.47 dB maximum. The harmonic model was fitted using those measurements, so these are fitting errors, not independent generalization scores.

## Timing limits

The measured 200 Hz response has a fast initial change followed by slower settling. The release tail after the first 25 ms fits an effective exponential of about 107–109 ms over these tests. The tiny remaining attack tail does not identify the main attack time. The preview uses a 5 ms attack and 100 ms single release as an approximation; the vendor response is not fully represented by one time constant.

On the full 200 Hz step sequence, excluding the first 25 ms around discontinuities, gain-trace RMS error was 0.82/0.88/0.97 dB for volume 50/75/100. Maximum errors were 6.07/7.83/8.39 dB. Sudden level changes remain materially less accurate than steady tones. These results must not be described as a complete transient match.

No live Windows audio integration or physical speaker protection calibration was performed. Future validation should include independent music/speech, more frequencies and levels, short transient behavior, and acoustic measurements with the lid open.
