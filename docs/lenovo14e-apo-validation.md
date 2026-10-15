# Lenovo 14e APO integration validation

Development preview, 2026-10-15. Validation is in progress.

- The independent DSP core has the previously recorded Linux, sanitizer and
  numeric calibration checks in `audio-dsp/docs/validation.md`.
- The Windows x64 APO DLL and native probe cross-compile with MinGW 13.
- Import inspection confirms only Windows system DLL dependencies.
- A disposable QEMU/KVM overlay of an existing Windows 11 test image is used
  for native APO and installer testing. The base image is unchanged.
- Hardware integration has not yet been applied to the Lenovo laptop.

A compiled DLL is not proof of AudioDG loading or live processing. Final
results and limitations will be recorded here after the native checks.
