# Recovery status

## Implemented from numerical calibration

The profile was selected by the actual ChromeOS machine as
`pujjoteen5.max98357`. Its UCM configuration supplies a speaker coefficient
preset to the SOF Waves adapter. The data has 101 MA7 records at 48 kHz.

The static coefficient layout was inferred mathematically: stereo-interleaved
Q2.30 values `[b1,b2,-a1,-a2,b0,shift]`. The decoded filters have stable poles,
matching channels, zero shifts, and reproduce standard filter equations.
Raw preset parsing reconstructs every original byte. No vendor executable
code was translated into this engine.

| Bank | Type | Frequency (Hz) | Gain (dB) | Q |
|---|---|---:|---:|---:|
| MaxxPEQ | Low shelf | 266 | −1.4 | 0.707107 |
| MaxxPEQ | Peak | 2144 | −2.5 | 2.47 |
| MaxxPEQ | Peak | 285 | −3.0 | 2.78 |
| MaxxPEQ | Peak | 1012 | −9.8 | 2.55 |
| MaxxPEQ | Peak | 1417 | −2.3 | 3.58 |
| MaxxPEQ | Peak | 6257 | −6.0 | 2.63 |
| MaxxPEQ | Peak | 8133 | −3.0 | 2.56 |
| MaxxPEQ | High shelf | 3415 | −2.5 | 0.707107 |
| MaxxFEQ | Peak | 819 | −3.1 | 3.00 |
| MaxxFEQ | Peak | 8680 | −2.0 | 0.85 |

MaxxXEQ's two recovered sections are third-order Butterworth high-pass at
approximately 107 Hz and low-pass at 12,510 Hz. The first- and second-order
stages independently agree on their cutoff frequencies within 0.001 Hz.
The second-order stages have Q=1.

The profile uses the exact recovered numeric coefficients rather than
rebuilding them from rounded frequency/gain labels. The JSON file includes
fit residuals and the numeric coefficients for review.

## New firmware evidence

A Ghidra extension for wide Xtensa branches exposed the coefficient dispatch
routine. For this firmware it selects a module using `(record_id >> 9) & 127`
and a local coefficient using `record_id & 511`. Module names are supported
by assertion/function-name strings referenced at the corresponding calls.

| Record family | Identified module |
|---|---|
| 0x0000 | Main/internal controls |
| 0x0200 | NoiseGate |
| 0x0800 | MaxxBassStereo |
| 0x0a00 | Crossover4 |
| 0x0c00 | CenterGain |
| 0x0e00 | MaxxStereo |
| 0x1200 | MaxxDialog |
| 0x1400 | MaxxTreble |
| 0x1600 | MaxxPEQ |
| 0x1a00 | MaxxXEQ |
| 0x1c00 | MaxxFEQ |
| 0x1e00 | MultiBand |
| 0x2400 | MaxxExciterLight |
| 0x4e00 | MVSurround |
| 0x5400 | BypassCrossfade |
| 0x6800 | MaxxS1 |

This names module families; it does not establish that each effect is enabled,
the exact processing order, or the meaning/scaling of every coefficient.

## Not reproduced yet

- Exact Waves multiband topology, curves, detection, timing, and leveler.
- MaxxBass, exciter, dialogue, stereo, treble and surround processing.
- Master control meanings and every enable/bypass flag.
- Complete routing of nonlinear stages relative to the EQ banks.
- Firmware fixed-point rounding, saturation and sample-exact behavior.
- An acoustic or post-Waves digital comparison on real hardware.
- Live Windows audio integration or an installable replacement driver/APO.

The open compressor/limiter are useful independent building blocks. They are
not presented as the original Waves algorithms. The default preset therefore
does not enable the compressor or invent bass-enhancement settings.

## Public references

- SOF MaxxEffect public interface distinguishes user parameters from computed
  coefficient presets:
  https://github.com/thesofproject/sof/blob/main/src/include/sof/audio/MaxxEffect/MaxxEffect.h
- SOF Waves adapter treats control messages as vendor-defined payloads:
  https://github.com/thesofproject/sof/blob/main/src/audio/module_adapter/module/waves/waves.c
- The board's public configuration is in the ChromiumOS nissa overlay:
  https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/HEAD/overlay-nissa/chromeos-base/chromeos-bsp-nissa/files/pujjoteen5/audio/
