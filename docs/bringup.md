# Custom T5-E1 Board Bring-up

The supplied T5AI-Board guide is a reference for toolchain, board revisions
and flashing precautions. Its pin mapping and Tuya cloud demo settings do not
describe this project's custom board.

## Board Audio Checklist

- Identify microphone type, channel count and wiring to the T5-E1 audio input.
- Identify DAC or codec, amplifier, speaker enable polarity and playback path.
- Trace the playback reference into the capture path. The official T5AI-Board
  V102 has a loopback circuit for AEC; V101 does not support the same modes.
  The custom board's AEC support cannot be inferred from its T5-E1 module.
- Confirm sampling clocks, power sequencing, reset pins and button wiring.

## Reference Software

TuyaOpen provides `examples/peripherals/audio_codecs/output_speaker`, a
cloud-free record-then-play example. Its T5AI-Board config is only a reference
and must not be flashed to this custom PCB. The example does not test AEC
under simultaneous playback and capture. See [audio BSP notes](bsp-audio.md).

The Wukong SDK can be downloaded through Tuya Wind IDE after access approval.
Its `tuyaos_demo_ai_toy` application contains Tuya cloud logic, so use it for
BSP inspection only. The selected SDK's redistribution and device-license
requirements need review before product integration.

## Host And Flashing

The remote host is Ubuntu 26.04 with CMake, Ninja, Doxygen and i386 runtimes.
The supplied guide's older `libncurses5-dev` and `python3.12-venv` package
names are unavailable on this release. Validate the actual toolchain against
the selected SDK; use a vendor-supported environment if compatibility fails.

The remote host has no attached T5-E1 serial device. Before any flash, verify
the exact partition map and protect boot and authorization areas. The supplied
guide uses QIO for its reference board and 460800 baud for logs; neither value
should be blindly applied to an unverified custom board.
