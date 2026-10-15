# Chromebook Audio DSP

An MIT-licensed, independently implemented audio-processing engine for the
Lenovo 14e Chromebook Gen 3 / Pujjoteen5 speaker profile. Includes a portable
C library and an offline WAV processor for Windows and Linux.

This is a working **first replacement core**, not a complete or bit-exact
reimplementation of Waves MaxxAudio 7. It does not install an audio driver,
modify firmware, or automatically process Windows system audio.

## Processing

- The recovered 107 Hz high-pass and 12.51 kHz low-pass filters.
- Ten recovered stereo EQ sections, including the 1,012 Hz / −9.8 dB cut.
- Optional stereo-linked three-band compression with soft knees and
  configurable attack, release, threshold, ratio and makeup gain.
- An independent stereo-linked lookahead sample limiter.
- Output gain, meters, reset, float32 and signed-32 PCM interfaces.

Both channels are processed independently through identical static filters.
Neither channel is muted. Dynamics share their detector/gain between left and
right to preserve stereo balance.

The static coefficients come from numerical analysis of the selected
ChromeOS speaker calibration. The compressor and limiter are newly designed;
their defaults are **not decoded Waves settings**. Compression defaults to
off. The relative placement of the recovered filter banks in the complete
Waves chain is not yet independently verified; the linear profile connects
them in series. Their order commutes only when no nonlinear stages intervene.

Default processing uses the recovered filters, −9 dB output gain (matching
the ChromeOS board curve's maximum), compression off, and a −1 dBFS sample
limiter with 144 frames / 3 ms latency. The limiter is not a true-peak limiter,
an amplifier watchdog, or a model of speaker temperature/excursion.

## Measured ChromeOS preview

```sh
./build/chromebook-dsp input.wav preview.wav --chromeos-volume 75
```

This experimental preset combines measured bass EQ, independently fitted
bass compression, and a normalized cubic harmonic branch. Supported volume
values are 25–100; the measured settings are 25, 50, 75 and 100, with linear
interpolation in dB between them. This is an internal processing setting;
it does not change the Windows volume control. The preset resets processing
settings, so place further overrides after it. `--no-harmonics` allows comparison.

Steady tone fitting achieved 0.33 dB RMS error at training volumes 50/100
and 0.23 dB RMS at held-out volume 75. The third-harmonic ratio approximation
has about 2.0 dB RMS error over the measured bass tones/levels. Transient
response remains approximate, especially around sudden level changes.
These measurements do not establish equivalence on music, physical speaker
limits, or a bit-exact MaxxAudio reproduction. The default limiter stays on.
See [measured dynamics](docs/measured-dynamics.md).

## Measured bass preview

Use `--measured-bass` with the default recovered EQ and −9 dB output gain
to add four independently fitted filters. The 90–1,100 Hz magnitude fit
has 0.30 dB RMS error on the training capture and 0.70 dB RMS error on
a second input level (maximum 1.54 dB). Both captures used ChromeOS
speaker volume 50%; this does not establish equivalence at other volumes.

```sh
./build/chromebook-dsp input.wav bass-preview.wav --measured-bass
```

The option is disabled by default and does not install live audio processing.
It approximates measured linear bass balance, not Waves harmonic synthesis,
phase, dynamics or speaker protection. The downstream sample limiter remains
enabled by default. The fitted response has substantial bass gain: it is an
offline comparison profile, not a validated physical speaker power limit.
See [bass measurement](docs/measured-bass.md).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows, use a CMake-supported C compiler such as MSVC or MinGW. The build
produces `chromebook-dsp.exe` and `libchromebook_dsp.dll` (MinGW names); keep
them in the same directory. Linux produces `chromebook-dsp` and
`libchromebook_dsp.so`.

To cross-compile Windows x64 on Linux with MinGW:

```sh
cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=tools/mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
```

## Process a WAV file

```sh
./build/chromebook-dsp input.wav output.wav
./build/chromebook-dsp input.wav filters-only.wav --linear
./build/chromebook-dsp input.wav compressed.wav --compress
```

Input must be **48 kHz stereo** PCM16/24/32 or float32 WAV. Output is float32
WAV. `--linear` uses recovered filters with unity output gain and no dynamics
or added latency. `--compress` enables the independently designed compressor.
`--gain-db N`, `--no-limiter` and `--no-eq` are also available. Options apply
in command-line order.

The processor appends the limiter's delay to avoid losing the delayed final
frames; it does not append the infinite IIR decay tail. It never plays audio
or overwrites an existing output file. Malformed and truncated inputs are
rejected; a failed conversion removes its incomplete output.

## Integrate the library

```c
#include "chromebook_dsp.h"

cb_config config;
cb_state state;
cb_default_config(&config);
if (cb_init(&state, &config) != 0) { /* handle invalid configuration */ }
cb_process_f32(&state, interleaved_input, interleaved_output, frame_count);
```

Allocate the state once per stream, outside the audio callback. Processing
allocates no memory and performs no file I/O or locking. In-place processing
is supported; other overlapping input/output arrangements are not supported.
Reset or configure only while processing is stopped. Query
`cb_latency_frames()` for delay compensation. The API is for normal
user-space C environments with floating-point/math support; it is not
ready to run directly inside a Windows kernel driver or the Xtensa firmware.

The three-band compressor uses LR4 crossovers with a phase-matched low-band
branch. With compression at ratio 1 and no makeup, the recombined magnitude
is flat, but the all-pass phase response differs from a bypassed crossover.
The entire crossover/compression path is bypassed when compression is off.

## Verification

The test harness uses NumPy and SciPy as an independent filter reference:

```sh
python3 -m pip install -r tests/requirements.txt
ctest --test-dir build --output-on-failure
```

Tests cover the recovered 14-section response, block boundaries, in-place
processing, reset, channel isolation, signed PCM extrema, atomic rejection
of invalid settings, limiter ceilings/latency, NaN/Inf containment, compressor
curves, crossover reconstruction, stereo linkage, WAV formats, malformed
inputs and output overwrite protection. Address/undefined-behavior sanitizer
checks are available with `-DCB_SANITIZE=ON`.

See [validation](docs/validation.md) and
[recovery status](docs/recovery-status.md) for tested behavior and the limits
of comparison with ChromeOS.

No Waves executable, proprietary firmware, decompiled vendor source or
credentials are included. This project is not affiliated with Waves, Intel,
Google, or Lenovo.
