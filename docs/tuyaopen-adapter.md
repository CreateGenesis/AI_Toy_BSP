# TuyaOpen Adapter Integration

The initial custom-board adapter is in
`firmware/tuyaopen/CHAT_ROBOT_T5E1`. It is a source template for the exact
TuyaOpen or Wukong SDK revision, not an independent build target.

To integrate it into a vendor SDK:

1. Copy the directory into `boards/T5AI/CHAT_ROBOT_T5E1`.
2. Add a `BOARD_CHOICE_CHAT_ROBOT_T5E1` branch to `boards/T5AI/Kconfig` that
   sources the new board `Kconfig`.
3. Add an app config with `CONFIG_BOARD_CHOICE_CHAT_ROBOT_T5E1=y` and the
   project's normal audio configuration.
4. Build the audio smoke test first. Confirm the key, microphone and playback
   before adding cloud/platform transport.
5. Add the `aw87390.c/.h` module and initialize the chip at address `0x58`.
   Verify the ID register returns `0x76` before enabling the audio stream.
6. Add the WS2812 driver only after the actual T5 SPI/pinmux route is confirmed.

`firmware/tuyaopen/CHAT_ROBOT_T5E1.config` is the starting config file for
that integration.

The adapter intentionally does not claim that AEC works. The board has the
analog loopback network, but the `MICP2_LB`/`MICN2_LB` polarity and AW87390
startup state still require acoustic testing.
