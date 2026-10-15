# Lenovo 14e Gen 3 Intel SOF Audio for Windows

A Lenovo 14e Chromebook Gen 3 / Google Pujjoteen-specific source-code fork of
[Intel SOF for Windows](https://github.com/chevydan6/intel-sof-windows),
combining the experimental Intel SOF host drivers with independent open-source
speaker processing for Windows. This is a community project, not an official
Lenovo, Intel, Google or Waves driver.

The upstream codebase is `chevydan6/intel-sof-windows`; this repository retains
its Git history and adds Lenovo-specific integration and speaker processing.
It is currently hosted as a standalone GitHub repository, so GitHub does not
display a formal “forked from” relationship.

## Target

- Lenovo 14e **Chromebook Gen 3**, Google Pujjoteen family, Intel Alder Lake-N.
- Intel PCI audio controller `8086:54C8`, RT5682 codec, MAX98357/98360-family
  speaker path as documented in the hardware investigation.
- Windows 11 x64; the effects preview accepts 48 kHz stereo float32.

Other Lenovo 14e generations, other Chromebooks and other SOF topologies have
not been validated. The installer checks the machine model and selects only
the supported SOF speaker endpoint. An explicit VM-only test option exists.

## Components

- `driver/`: inherited experimental ACX/KMDF controller, codec and amplifier
  drivers, with their existing stream-stop and amplifier watchdog behavior.
- `audio-dsp/`: MIT-licensed C engine with recovered numeric speaker EQ,
  independently fitted bass response, bass dynamics, harmonic approximation
  and a sample limiter. No Waves executable or proprietary firmware is included.
- `apo/`: a Windows endpoint-effects APO DLL, native contract/playback probe,
  and a reversible development installer attaching it to the speaker output.

The APO processes both channels. It introduces no channel mute. The separate
right-only hardware workaround on the owner's laptop remains in its existing
kernel driver. Installing the APO does not replace that driver or change its
channel routing, headphones, microphone processing, boot policy or test mode.

## Preview status

Development in progress. The effects build and live installation evidence is
recorded in [APO validation](docs/lenovo14e-apo-validation.md). Installation
requires an elevated shell and temporarily restarts Windows audio services.
The installer records the affected registry values for rollback, and refuses
to silently replace an unrelated existing endpoint effect.

This is an installable development effects package, not a Microsoft-signed
production driver bundle. Native driver signing and firmware redistribution
remain separate requirements; see [upstream signing notes](docs/driver-release-and-signing.md).

## Build the speaker effects

```sh
cmake -S apo -B build/apo -G Ninja -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/apo
```

Windows MSVC builds use the installed Windows SDK. MinGW builds use minimal
API declarations checked against the SDK ABI. The APO statically includes the
DSP core and needs no separate DSP DLL or third-party runtime installation.

See [speaker effects](apo/README.md) for install, bypass/rollback, and test
commands. See [the inherited project overview](docs/upstream-readme.md) for
the kernel driver history and remaining hardware limitations.

## Licensing

Inherited host-driver code remains GPL-2.0-only. The independent DSP engine
and APO integration are MIT licensed within their respective directories.
Windows API declarations follow the documented Windows ABI. Third-party
firmware and SDK components retain their own licenses and are not bundled.
