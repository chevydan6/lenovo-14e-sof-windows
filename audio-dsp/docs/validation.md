# Validation

The Linux Release and address/undefined-behavior sanitizer builds pass the independent NumPy/SciPy verification suite. Windows x64 builds with MinGW. The original engine was exercised natively on Windows; this updated preview has been cross-compiled but its new preset has not yet been exercised natively on Windows.

Checks cover recovered EQ against SciPy, streaming/in-place/reset/channel isolation, invalid configuration rejection, signed PCM extrema, limiter ceilings and latency, non-finite input containment, compressor curves, LR4 reconstruction, WAV formats and malformed input/output overwrite handling. Added checks cover bass correction stability/reference response, measured preset configuration/held-out tone levels, full-scale limiter stress across volume presets, and the measured 80 Hz third harmonic.

See [measured dynamics](measured-dynamics.md) for comparison metrics and known transient errors, and [measured bass](measured-bass.md) for the earlier magnitude-only fit. [Transient comparison](engine-transients.png) and [measured level curves](level-curves.png) show the difference between steady and changing levels.
