# Intel SOF Audio Driver for Windows

Open-source Windows host-driver development for Intel Sound Open Firmware (SOF).
The first target is an Intel N100 laptop with a PCI audio controller at
`8086:54c8` and an I2C Realtek RT5682 codec (`ACPI\10EC5682`). This repository
contains diagnostic tooling and experimental ACX audio drivers.

An opt-in [right-speaker normal-use trial](docs/n100-right-speaker-trial.md)
supports ordinary Windows applications without a timed cutoff or manual arming.
It retains the amplifier renewal watchdog, switches off on stream stop or
sustained silence, and keeps the damaged/imbalanced left channel silent.
The user confirmed clear sound and cool speaker areas in earlier two-, eight-
and thirty-second tests. Broader normal-use and sleep/resume validation remain
in progress; the speaker trial does not yet integrate headphone-jack switching.
Left-side imbalance also reproduces in Fedora; its hardware cause remains
unresolved. See the [thermal investigation](docs/n100-amplifier-thermal-review.md).

A separate headphone driver supports Windows application playback on the target.
Stream start/stop, repeated playback, physical jack removal/reinsertion, Windows
volume/mute, and acoustic tone capture have been verified. The RT5682 output
powers down when its stream stops. A fixed -9.54 dB PCM ceiling permits
14.54 dB more output than the original headphone build, with Windows volume
available below that ceiling.
See [headphone playback and distortion checks](docs/n100-headphone-playback.md).
The built-in microphones support [bounded diagnostic recording](docs/n100-microphone-capture.md),
but do not yet expose a Windows microphone endpoint.
Earlier successful speaker DMA tests are historical evidence, not a statement
that the current build enables speakers. General power recovery remains unfinished.

The development probe builds with the WDK in Windows CI and its generated INF
passes a separate x64 `InfVerif /w` check. It read the controller's resources,
[capability chain](docs/n100-capability-chain-test.md), and
[DSP status](docs/n100-dsp-status-test.md). Its
[on-demand snapshot](docs/n100-live-snapshot-test.md) works through a device
interface. A guarded, opt-in [core-0 power test](docs/n100-core0-power-test.md)
received hardware acknowledgment and restored the powered-off baseline.
The hardware-test reports record installation and rollback state for each test.

The [firmware boot path](docs/n100-firmware-boot-plan.md) is mapped, the [IPC3 firmware payload can be staged in DMA memory](docs/n100-ipc3-staging-test.md), the [code-loader](docs/n100-stream-setup-test.md) and [ICCMAX capture](docs/n100-capture-setup-test.md) streams both accepted guarded setup and restoration, the [capture RUN/STOP test](docs/n100-capture-dma-test.md) returned cleanly, the [DSP ROM acknowledged entry](docs/n100-rom-entry-test.md), the [firmware reached its entered state](docs/n100-firmware-dma-test.md), the [IPC3 firmware-ready notification arrived](docs/n100-ipc3-ready-test.md), a [host IPC3 request received a valid reply](docs/n100-ipc3-transaction-test.md), and a [managed DSP session handled a second request](docs/n100-ipc3-session-test.md).

The [ACX playback investigation](docs/n100-acx-playback-test.md) now records successful Windows format negotiation and a three-second WAV transfer (301 packets submitted and completed), after correcting packet-buffer alignment and preparing SOF before starting Windows audio services. Audible confirmation of that app test is pending; the earlier [hardware tone](docs/n100-speaker-tone-test.md) was heard. The [automatic startup work](docs/n100-acx-autostart-test.md) moves firmware and speaker setup into the driver and resets PCM between streams. Interrupt-driven IPC service, general topology support, and comprehensive power recovery remain unfinished.

A public 0 dB stereo speaker build with both channels enabled is available as a
[test-signed prerelease](https://github.com/chevydan6/intel-sof-windows/releases/tag/v0.1.0-rc.2).
The right-only build is a separate workaround for the owner’s laptop.
See [package contents and Microsoft signing](docs/driver-release-and-signing.md).

## Why a host driver is needed

SOF firmware runs on the audio DSP. A Windows host driver must claim the PCI
controller, initialize the DSP, load compatible firmware and topology, exchange
SOF IPC messages, manage DMA and power states, and expose audio streams through
Windows audio interfaces. The Realtek codec is a separate I2C device that also
needs a working path to the audio graph.

## First target

The initial target baseline reported Windows PnP problem code 28 for both
`PCI\VEN_8086&DEV_54C8` and `ACPI\10EC5682`. Linux maps `54c8` to Alder Lake-N
SOF (`HDA_ADL_N`) and defaults that platform to IPC3 with `sof-adl-n.ri`.
See [hardware/n100-windows.json](hardware/n100-windows.json) and
[docs/source-map.md](docs/source-map.md).

## Development order

1. Build a KMDF PCI probe that binds only to `8086:54c8`, reports
   controller capabilities, verifies a guarded DSP power request, and unloads cleanly. **Done.**
2. Add DSP boot and firmware-ready handshake using the Alder Lake-N Linux SOF
   code as the hardware reference. Keep a known-good Windows restore path.
   **Guarded session start, request, and stop verified on N100.**
3. Add IPC3 request/reply handling, timeouts, and firmware trace collection.
4. Bring up one simple playback path, then capture and codec controls through
   Windows ACX audio circuits.
5. Add suspend/resume, error recovery, validation, and driver signing for wider
   use.

Run `tools/collect-hardware.ps1` on the Windows target to refresh the PnP
baseline. The Windows Driver Kit is not installed on the target, so initial
builds use Windows CI. Test-signed builds require a controlled test-signing
setup on the target for each hardware test.

## Licensing

This project is GPL-2.0-only so that appropriately attributed Linux SOF host
code can be adapted where useful. Third-party firmware and Windows Driver Kit
components keep their own licenses; neither is bundled here.
