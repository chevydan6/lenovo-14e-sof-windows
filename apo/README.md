# Lenovo 14e Windows speaker effects preview

This DLL implements a Windows endpoint-effects APO around the independent
speaker DSP core. It accepts 48 kHz stereo IEEE float32, supports in-place
processing, and reports the limiter's 3 ms latency. Silent buffers are passed
through the stateful processor to drain filter/limiter tails. The audio callback
performs no allocation, file access, COM calls or registry access.

The EFX design expects Windows' software endpoint volume to precede the
processor. Its fixed 50%-capture calibration reference is compensated once;
it does not poll or change the Windows volume control. The actual integration
order and live behavior require verification on each host. The development
preview adds 6 dB output headroom and uses a -6 dBFS sample limiter ceiling.
That limiter is not a physical speaker excursion/thermal protection model.
The existing kernel driver amplifier watchdog remains independent.

## Build

Use `cmake -S apo -B build/apo` with a Windows SDK/compiler, or use the MinGW
cross-compilation command in the root README. Outputs:

- `lenovo14eapo.dll`: self-contained DSP and APO.
- `lenovo14e-apo-probe.exe`: native contract tests, telemetry, quiet WASAPI render.
- `lenovo14e-apo-worker.exe`: temporary SYSTEM installer service and narrowly scoped registry access helper.

No Windows kernel driver is built or replaced by this CMake project.

## Development installation

Place all three binaries and `install.ps1` together. In elevated 64-bit PowerShell:

```powershell
.\install.ps1
```

The installer only accepts Google Pujjoteen machines and an active supported
SOF speaker endpoint. `-VirtualMachine` is for virtual-machine testing only;
it also verifies that the machine is a VM. It records the exact affected
registry values under `%ProgramData%\Lenovo14eAudio`, registers the COM/APO
DLL under Program Files, enables the speaker effect, and restarts Windows
Audio. It uses a one-shot SYSTEM service, marks the service for deletion
as soon as it starts, and temporarily grants SYSTEM access to the selected
endpoint property keys. Original owner/group/DACL descriptors are saved and
restored in a finally block. It does not change the audio services' configuration
or leave a persistent installer service. It refuses to replace an unrelated existing endpoint APO.

This is a reversible development registration, not a signed componentized
production INF package. No boot/signature policy is altered. Protected audio
or a host's audio security policy may reject a development APO. Do not claim
support unless actual AudioDG processing has been verified.

Rollback in elevated PowerShell:

```powershell
.\install.ps1 -Rollback
```

Rollback restores the recorded values and unregisters the DLL. It leaves
files and the dated backup record available for inspection. The installer
never changes the kernel driver's stereo/right-only routing.

## Native verification

```powershell
.\lenovo14e-apo-probe.exe .\lenovo14eapo.dll
.\lenovo14e-apo-probe.exe --render
.\lenovo14e-apo-probe.exe --status
```

The first command directly tests COM interfaces, float format negotiation,
locks/reset, bounds, silence, channel isolation and latency, without registering
an endpoint effect. `--render` plays three seconds of -60 dBFS 200 Hz stereo
through the default Windows shared-mode output. It is not a maximum-output
test. An optional third argument supplies an explicit Core Audio endpoint ID.
`--status` reads counters from a memory-mapped diagnostic file (with a named shared-memory fallback for standalone tests); compare frame/call counters before
and during playback, and verify that `hostPid` is the relevant `audiodg.exe`.
The diagnostic file contains counters only, with read access for users and write access for the audio service. The callback only updates mapped memory. The direct contract test also creates an APO instance, so its counters alone
do not prove system integration.

See [validation](../docs/lenovo14e-apo-validation.md) for actual test evidence.
