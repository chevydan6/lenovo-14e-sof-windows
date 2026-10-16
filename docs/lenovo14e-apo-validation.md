# Lenovo 14e APO integration validation

Development preview, updated 2026-10-16.

- The independent DSP core has the recorded Linux, sanitizer and numeric
  calibration checks in `audio-dsp/docs/validation.md`.
- Windows x64 binaries cross-compile with MinGW. Import inspection confirms
  Windows system DLL dependencies only.
- Native contract tests pass on the Lenovo laptop, including standalone and
  aggregated activation, controlling IUnknown identity/reference lifetimes,
  format negotiation, locks, processing, silence, bounds, latency and reset.
- The old EFX registration was discovered but was not inserted into the
  streaming graph on this ACX host. Default-mode MFX registration processes
  shared-mode playback. The driver exposes RAW and DEFAULT processing modes.
  This behavior is consistent with the mode-mix constraint described in
  [Microsoft's APO registration guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects#registering-apos-for-processing-modes-and-effects-in-the-inf-file).
- AudioDG's DLL preflight required a version resource; it previously mapped
  the resource-less DLL as data and skipped activation. The corrected DLL
  includes VERSIONINFO, is deployed to System32, and is test-signed with page
  hashes and the PE integrity-check flag. Normal and signature-enforced loader
  checks pass. No audio-protection or boot-policy change remains applied.
- AudioDG aggregates the streaming APO. The original factory rejected this
  with `CLASS_E_NOAGGREGATION` (0x80040110). The corrected factory implements
  delegating interfaces and a separate nondelegating IUnknown. Regression
  tests cover aggregation identity and lifetimes.
- Two consecutive hardware playback runs processed 149,760 and 299,520
  cumulative frames at 48 kHz stereo, with zero DSP errors and advancing call
  counters (312 and 624). These counters were read from AudioDG PID 2160,
  rather than the direct contract-test process.
- `--verify-render` fails if the effect is absent or no new frames are
  processed. `install.ps1 -VerifyPlayback` uses this check and rolls back on
  failure. It prevents silent bypass from being reported as successful DSP
  integration.
- Final acceptance verifies rollback removes the effect and managed system
  DLL, and that live verification then rejects bypass. Reinstallation passes
  again, including interactive desktop playback: 300,000 cumulative frames,
  625 calls, zero errors, AudioDG PID 8588. Endpoint registry owner/group/DACL
  restoration also matches the recorded descriptor. No temporary task or
  audio-protection override remains.
- The existing right-only kernel driver remains oem70.inf, version
  6.52.50.105. The amplifier watchdog and channel routing are unchanged.

The new native tests and hardware checks supersede the earlier VM result,
which showed DLL discovery with zero processing counters. Volume-dependent
ChromeOS matching, acoustic quality, speaker temperature and sleep/resume
have not been validated by these quiet software playback tests. The MFX
preview leaves Windows endpoint volume under Windows' control.

Machine-specific packages, signatures and installation evidence remain in
ignored local `build/` directories. Only the public signing certificate is
included in the release; the laptop signing private key was removed during
development cleanup.

## Requested higher output — 2026-10-16

At the owner's request, version 0.3.1.0 removes the preview's extra 6 dB
output attenuation, retaining the DSP core's calibrated output gain. The
sample limiter ceiling rises from -6 to -1 dBFS (5 dB more peak output).
The limiter, kernel driver, right-only routing and amplifier watchdog remain
in place. The native bounds test now uses an over-range impulse and checks
finite output within the new limiter ceiling, plus channel isolation.

The signed package installed successfully. Native aggregation/DSP contracts
and quiet live AudioDG playback passed: 159,360 frames, 332 calls, zero errors,
PID 2712. Both audio services are running. SHA-256 of the installed DLL is
`27519C8BF7D8AFFEF6F15891279616E80DE2293B17C954DAF1C05B280E9882CE`.
The previous package is preserved in the development workstation's ignored
`build/hardware-dsp-20261016` directory. Listening and temperature feedback at higher output are pending.
