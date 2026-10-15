# Measured bass approximation

Four independent standard RBJ peaking filters are fitted against ChromeOS EchoRef PCM16, 48 kHz, stereo, speaker node volume 50%. The target is the -30 dBFS peak deterministic-noise capture. A -40 dBFS capture is held out from fitting. Estimates use two seconds after 0.5 seconds of settling; the two levels use the same noise realization, so this is a level holdout rather than an independent signal test.

The filters correct the existing recovered 14-section EQ at -9 dB output gain. Frequency samples are logarithmically distributed from 90 to 1,100 Hz; a soft constraint favors unity correction above 2 kHz. The fit does not constrain behavior below 90 Hz, identify physical amp gain, or establish the exact location of all volume/dynamics stages. Existing recovered high-pass filtering remains in place.

Training RMS error: 0.299 dB; maximum 1.190 dB. Held-out level RMS error: 0.701 dB; maximum 1.542 dB. These are digital magnitude-response errors in the fitting band, not acoustic errors or a perceptual equivalence claim.

The public JSON profile contains coefficients, fit parameters and metrics. `tools/fit_measured_bass.py <private-capture-directory>` reproduces the fit from local `test.raw` / `echo.wav` research inputs. Original recordings and firmware are not included in this source project. The fit uses newly implemented standard biquads and contains no vendor executable or decompiled source.

New tests compare the complete enabled cascade with SciPy, check pole stability, block partition equivalence, and stress the downstream limiter with large inputs. No live driver configuration was changed. The C configuration/state structures changed; rebuild applications using the library and keep the executable and DLL versions together.

Remaining validation: independent signals, multiple volume settings, attack/release behavior, harmonics, acoustic speaker measurements and physical limits. Do not describe this frequency-response fit as a complete MaxxBass recreation.
