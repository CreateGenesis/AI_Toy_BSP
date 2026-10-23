# T5-E1 AI Toy Device

The device uses Tuya T5-E1 board support for microphone capture, speaker
playback and, where the hardware allows it, acoustic echo cancellation. AI,
device services and product data belong to the owner's platform. The device
application must not depend on Tuya's cloud conversation or product PID flow.

## Current State

- Hardware: custom T5-E1 board; schematic and audio wiring are pending.
- BSP candidate: the official open-source TuyaOpen T5AI audio layer already
  checked out in `TuyaOpen/`. Wukong SDK can be evaluated as another BSP source
  when it is available, but its cloud AI demo is not the product application.
- Transport: intentionally undecided. `firmware/include/toy_transport_port.h`
  reserves the boundary to the owner's platform.
- Firmware: the `CHAT_ROBOT_T5E1` TuyaOpen adapter is in use. Speaker
  playback is verified on the board with `bash scripts/build-tone.sh`.
  Microphone, key and AEC are tested push-to-talk (hold the key to record,
  release to play) with `bash scripts/build-aec-loopback.sh`; see
  [the test](docs/aec-loopback-test.md). `firmware/apps/voice_gateway/`
  is the owner's gateway client. LEDs, battery ADC and the conversation
  flow are not verified on the board.

Run `bash scripts/check.sh` on the remote host for current dependencies and
blockers. `bash scripts/build.sh` refuses to build a cloud demo as the product.

## Layout

- `firmware/include/`: vendor-independent audio and transport contracts.
- `docs/architecture.md`: device and platform responsibilities.
- `docs/bsp-audio.md`: verified TuyaOpen audio APIs and validation steps.
- `docs/bringup.md`: custom-board bring-up and flashing precautions.
- `TuyaOpen/`: external SDK checkout, excluded from project version control.
- `WukongSDK/`: optional vendor SDK, currently empty and excluded from version
  control.
- `references/`: supplied T5AI-Board guide, private and excluded from version
  control.

The TuyaOpen `output_speaker` example can be built only as a **reference-board
audio smoke test** with `bash scripts/build-reference-audio.sh v102`. It records
then plays audio; it does not validate AEC, speech recognition or the custom
board. See [project decisions](docs/project-decisions.md) for open inputs.
Use the [acceptance checklist](docs/acceptance.md) for playback, capture, AEC
and recognition validation on the actual board.

The supplied schematic is summarized in [the board mapping](docs/chat-robot-board.md).
The first TuyaOpen adapter template is in
`firmware/tuyaopen/CHAT_ROBOT_T5E1`; integration steps are in
`docs/tuyaopen-adapter.md`.
