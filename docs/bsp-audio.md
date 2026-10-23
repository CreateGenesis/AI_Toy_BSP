# Tuya Audio BSP Notes

TuyaOpen revision `7a94907` contains a cloud-free reference at
`TuyaOpen/examples/peripherals/audio_codecs/output_speaker`. It uses:

- `board_register_hardware()` to register board audio and button devices.
- `tdl_audio_find()`, `tdl_audio_open(callback)` and `tdl_audio_get_info()` for
  microphone capture.
- `tdl_audio_play()` for PCM playback.
- A ring buffer to store captured frames until button release, then play them.

The official `TUYA_T5AI_BOARD` BSP configures 16 kHz, 16-bit mono capture and
16 kHz playback. Its Kconfig selects `ENABLE_AUDIO_AEC` for the V102 board
revision. The BSP passes that selection as `aec_enable` to `tdd_audio_register`.
These facts prove an API path exists; they do not prove that AEC works on this
project's custom board.

The first custom-board adapter should map the vendor callback and playback
functions to `toy_audio_port.h`. It must report whether the hardware can
provide a valid playback reference for AEC. A record-then-play test checks
capture and playback separately; an AEC test requires concurrent playback and
capture plus real acoustic measurements.

The Wukong SDK audio API cannot be mapped until its actual package is
available. Do not copy the vendor cloud application into the product firmware.

The custom chat-robot board is not electrically identical to the public
`TUYA_T5AI_BOARD` adapter. Its key is GPIO3, its LED data is GPIO9, its battery
ADC is GPIO28, and its AW87390 control bus uses GPIO38/GPIO39. The public
adapter's GPIO12 key and GPIO28 speaker-enable assumptions must not be reused.
See [the schematic mapping](chat-robot-board.md).
