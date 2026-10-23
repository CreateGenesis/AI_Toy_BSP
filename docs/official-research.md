# Official Tuya Sources

Checked on 2026-10-11.

## Wukong And T5-E1

The official Wukong overview describes a TuyaOS AI hardware framework for AI
toys, robots, appliances and consumer electronics. The platform page lists the
T5-E1 and T5-E1-IPEX modules under T5. The example page names
`tuyaos_demo_ai_toy` and describes cloud voice agents, push-to-talk, status
sounds, battery reporting and low-power operation. The newer quick-start also
uses `tuyaos_demo_wukong_ai` as a demo name.

The official quick-start requires Tuya Wind IDE access approval to download
the framework. Linux is supported as a development host. No official T5-E1
Linux firmware image or public Wukong SDK GitHub repository was found. The
T5-E1 datasheet identifies an Armv8-M Cortex-M33F MCU with 8 MB flash and
16 MB PSRAM; Ubuntu on the host does not change the device OS.

## Official Links

- Wukong overview: https://developer.tuya.com/cn/docs/iot-device-dev/ai-hardware?id=Kectwmx9isrgl
- Supported platforms: https://developer.tuya.com/cn/docs/iot-device-dev/module-support?id=Kectxovhai9tx
- AI toy example: https://developer.tuya.com/cn/docs/iot-device-dev/tuyaos-ai-examples?id=Kedm4qo8hlohl
- Wukong quick-start and access process: https://developer.tuya.com/cn/docs/iot-device-dev/quick-start?id=Kectxdshpvsqr
- T5-E1 datasheet: https://developer.tuya.com/en/docs/iot/T5-E1-Module-Datasheet?id=Kdar6hf0kzmfi

## Open BSP Reference

TuyaOpen is a separate open-source SDK. It lists Ubuntu and T5 as separate
targets, including T5-E1. Its T5AI board files and cloud-free
`output_speaker` example provide a starting point for capture and playback.
The cloud-connected `your_chat_bot` and Wukong AI toy demos are not the
application for this project, which uses the owner's platform.

- TuyaOpen: https://github.com/tuya/TuyaOpen
- Audio example: https://github.com/tuya/TuyaOpen/tree/master/examples/peripherals/audio_codecs/output_speaker

TuyaOpen revision cloned here: `7a94907`.

## Third-Party Reference

A public Wukong code reference contains T5AI board variants and an Ubuntu
simulation directory. It is not a Tuya-owned official SDK distribution, and
it was not imported as this project's Wukong framework.

- Reference: https://github.com/HugoSeo/AI_desktop_Tuya_wukong
