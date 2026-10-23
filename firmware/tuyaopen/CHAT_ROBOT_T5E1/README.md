# CHAT ROBOT T5 E1 TuyaOpen Adapter

This directory is a board-adapter source for TuyaOpen or a compatible Wukong
T5 SDK. It is intentionally kept outside the vendor checkout until the exact
SDK revision and Wukong package are available.

The adapter registers:

- T5 audio input/output at 16 kHz, 16-bit, mono with AEC requested.
- Main key on GPIO3.
- No speaker-enable GPIO, because the schematic powers the AW87390FCR without
  a separate enable line.

It initializes the AW87390FCR at I2C address `0x58`, selects Class-D speaker
mode, powers the charge pump, and starts at 18 dB gain. The WS2812 chain still
needs its pixel-driver pin/timing configuration.

To integrate it, add the directory to the SDK's T5 board choice and add
`CHAT_ROBOT_T5E1` to `boards/T5AI/Kconfig`. Do not replace the vendor
`TUYA_T5AI_BOARD` files in place.
