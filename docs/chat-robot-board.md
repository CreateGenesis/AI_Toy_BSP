# Chat Robot T5 E1 Board Mapping

This mapping is extracted from `references/ai-toy.pdf` on 2026-10-18. It is a
schematic mapping, not proof that the assembled board is electrically correct.
Verify the net names with a meter and the assembled PCB before flashing.

## T5 E1 Signals

| Function | Schematic net | T5 E1 signal | Driver consequence |
| --- | --- | --- | --- |
| Main microphone | `MIC_P1`, `MIC_N1` | Audio MP1/MN1, pins 66/67 | Use the T5AI audio input channel; do not add a GPIO ADC driver. |
| Microphone bias | `MICBIAS` | Audio MBS, pin 70 | Supplied by the audio path. |
| Playback output | `AUDIO_LP`, `AUDIO_LN` | Audio LP/LN, pins 62/61 | Feeds the AW87390FCR input. |
| AEC reference positive | `MICP2_LB` | Audio MN2, pin 68 | Receives the filtered `SP+` reference. |
| AEC reference negative | `MICN2_LB` | Audio MP2, pin 69 | Receives the filtered `SP-` reference. |
| Amp control clock | `I2C1_SCL` | P38, pin 63 | Reserve for AW87390 initialization. |
| Amp control data | `I2C1_SDA` | P39, pin 64 | Reserve for AW87390 initialization. |
| Main key | `NET_KEY` | P3/GPIO3, pin 22 | Register the application/configuration key on GPIO3. |
| Status LEDs | `Light_DO` | P9/GPIO9, pin 16 | Register one WS2812 data output; two LEDs are chained. |
| Battery measurement | `BAT_ADC` | P28/ADC4, pin 11 | Configure ADC channel GPIO28 and apply the schematic divider. |
| USB present | `USB_PRESENT_N` | P6/GPIO6, pin 19 | Active-low charger/USB status input. |
| Charge status | `CHG_STATE` | P5/GPIO5, pin 20 | Read the open-collector charge status through the external transistor. |
| Hardware reset | `MOD_RST_KEY` | T5 reset pin | Keep as hardware reset; do not register it as an ordinary GPIO key. |

## Audio And AEC

The AW87390FCR is powered from `4V7`/`SYS_3V3`, receives `AUDIO_LP/LN`, and
drives `SP+/SP-`. The schematic feeds `SP+/SP-` through an analog filter into
`MICP2_LB/MICN2_LB`; this is the playback reference required by a hardware AEC
path. The board therefore has the expected loopback topology.

There is no amplifier enable GPIO in the schematic. The driver must pass an
unused speaker GPIO sentinel (normally `TUYA_GPIO_NUM_MAX`) and must not toggle
GPIO39. The supplied AW87390FCR V1.3 datasheet identifies address `0x58` when
AD1/AD2 are low, ID `0x76`, reset write `0x00=0xAA`, speaker mode `0x02=0x0C`,
and software power-up through `SYSCTRL` register `0x01` bit 0. These values are
implemented in the adapter template; the ID and acoustic output still need to
be verified on the assembled board.

The T5AI TDD audio configuration should start with 16 kHz, 16-bit, mono,
`TKL_AI_0`, and AEC enabled. The `aec_enable` flag alone is insufficient: the
custom board's loopback gain, polarity, clocks and AW87390 state must be
verified with simultaneous playback and capture.

There is a polarity warning in the schematic: the net named `MICP2_LB` lands on
the T5-E1 `MN2` pin, while `MICN2_LB` lands on `MP2`. Treat the AEC reference
polarity as unverified. The first acoustic test must compare normal and swapped
reference polarity before tuning gains or thresholds.

## Required BSP Changes

1. Add a `CHAT_ROBOT_T5E1` board choice or a private board adapter.
2. Copy the T5AI audio registration, changing the key to GPIO3, LED to GPIO9,
   AEC to enabled and speaker enable to the unused sentinel.
3. Add the AW87390 I2C1 init/mute/volume driver on GPIO38/GPIO39. Do not guess
   register values from the schematic; use the exact AW87390FCR revision
   datasheet or vendor reference code.
4. Add battery ADC GPIO28, USB-present GPIO6 and charge-state GPIO5 only after
   their active levels and ADC calibration are measured.
5. Keep the hardware reset separate from the application key. The download
   connector and log UARTs are already routed in hardware.

The existing official `TUYA_T5AI_BOARD` adapter is not a drop-in match because
it assumes a speaker enable on GPIO28 and a key on GPIO12. GPIO28 is the
battery ADC on this board, and GPIO12 is not the key net.
