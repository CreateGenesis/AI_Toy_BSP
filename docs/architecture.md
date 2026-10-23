# Device Architecture

```text
Custom T5-E1 hardware
    -> Tuya audio BSP (capture, playback, optional AEC)
    -> device audio session
    -> transport adapter (protocol pending)
    -> owner's platform (conversation, speech output, device data)
```

The firmware owns hardware lifecycle, capture framing, playback buffering,
interrupts and connection state. The owner's platform owns the conversation
workflow and service integration. Recognition may run on the device or the
owner's platform; that placement remains a decision. Either way,
the audio path must work before recognition quality can be evaluated.

`firmware/include/toy_audio_port.h` is the only interface the application
should use for vendor audio functions. The adapter for the selected BSP will
implement it after the custom board schematic is available.

`firmware/include/toy_transport_port.h` separates audio frames from the wire
protocol. The transport implementation will be selected after the platform
contract defines framing, sample format, identity/authentication, session
control, interruption, error recovery and downlink audio. It converts between
the wire codec and the PCM frames used by the audio port.

No cloud URL, Tuya product ID, or device key belongs in these interfaces.

The first board adapter should implement the mapping in
`firmware/include/chat_robot_t5e1_pins.h`. Keep the AW87390 register table in
the board adapter, not in the transport layer.
