# Project Decisions

## Fixed

- Product platform: owned by this project. Tuya cloud conversation, agent
  binding and product PID are not application dependencies.
- Hardware: custom T5-E1 board. No official development-board pin map is
  applied to the product until its schematic is reviewed.
- Initial BSP candidate: TuyaOpen T5AI audio interfaces, because their source
  and examples are available now. Wukong BSP remains an option if it provides
  audio features that the open SDK cannot deliver on the custom board.
- Device-to-platform protocol: undecided. Keep the transport behind a small
  interface and do not hardcode a server URL, codec or authentication scheme.

## Evidence Needed Before A Firmware Build

1. Board schematic and BOM: microphone(s), ADC or codec, DAC or amplifier,
   speaker enable, AEC playback-reference path, buttons and power rails.
2. Actual BSP APIs and license terms for the selected SDK on this board.
3. Where speech recognition runs: device, owner's server, or another service.
4. Transport protocol, sample format, frame size, authentication and reconnect
   behavior for the owner's platform.

## Validation Order

1. Confirm stable microphone capture and speaker playback on the custom board.
2. Verify the AEC reference wiring and measure capture quality during playback.
3. Feed captured audio to the chosen recognition path and check recognition
   quality with real speech and device acoustics.
4. Add the transport adapter and end-to-end conversation flow.

Do not erase the module's existing authorization or boot data during bring-up.
