# Firmware Source

- `include/`: vendor-independent audio and transport boundaries.
- `tuyaopen/CHAT_ROBOT_T5E1/`: TuyaOpen board adapter and AW87390 driver.
- `tuyaopen/patches/`: verified T5AI platform patch (both DAC channels).
- `apps/tone_playback/`: verified speaker playback test.
- `apps/aec_loopback/`: push-to-talk microphone, key and AEC test, see
  `docs/aec-loopback-test.md`.
- `apps/voice_gateway/`: push-to-talk client for the owner's voice gateway.
  Site Wi-Fi, server and token live in `src/gw_config.h`.
