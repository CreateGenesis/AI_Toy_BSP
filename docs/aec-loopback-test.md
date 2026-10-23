# Microphone And AEC Push-To-Talk Test

`firmware/apps/aec_loopback` checks microphone capture, the `NET_KEY`
button and the `MICP2_LB`/`MICN2_LB` playback reference on the assembled
board. Hold the key to record; release it to hear the recording. It keeps the
verified playback path (AW87390 init, both DAC channels) and plays at volume
50, twice the verified tone test's 25.

## How It Works

The board option `CHAT_ROBOT_T5E1_AEC` (on only for this app) makes the T5 ADC
capture two channels: left = MIC1 (`MIC_P1/N1`, pins 66/67), right = MIC2
(`MICN2_LB` on MP2 pin 69, `MICP2_LB` on MN2 pin 68). The vendor AEC task
passes both to the hook from `tkl_ai_set_vad_aec_algorithm()`. An installed
hook replaces the vendor AEC and passes the raw microphone through; removing
it puts the vendor AEC back in the path. The vendor VAD is disabled so
silence still streams.

`NET_KEY` (GPIO3, active low) is scanned with the internal pull-up. The
vendor IRQ mode enables a pull-down for active-low keys, which reads as
pressed when there is no strong external pull-up.

- Key down: record raw mic + ref, up to 10 s. Presses under 0.3 s are ignored.
- Key up: play the recording. Meanwhile the mic captures the speaker echo,
  raw on odd playbacks and through the vendor AEC on even playbacks.

## Run

Before flashing, meter the mic to pins 66/67, the loopback filter to pins
68/69, and MICBIAS (pin 70) while capture runs.

1. `bash scripts/build-aec-loopback.sh`
2. Flash `firmware/apps/aec_loopback/dist/aec_loopback_1.0.0/aec_loopback_QIO_1.0.0.bin`
   the same way as the tone image. Do not erase all flash.
3. Open UART1 at 460800 8N1 and reset the board. When
   `ready: hold the key to record` appears, hold the key, speak at 30 cm,
   release, and listen.

## Reading The Log

- No `RECORD` line when the key is pressed: GPIO3 or the key is not working.
  A `RECORD` line at boot without a press means the key reads as held.
- `@LB-ERROR AEC hook never ran`: the two-channel capture path is not running.
- `voice.mic`: low `rms`/`peak` means low mic gain or a wiring fault; `clip`
  above 0 means too much gain or speaking too close.
- `voice.ref`: should stay near silence while you speak. If it is loud, L/R
  are swapped.
- `echo.ref` (odd playbacks): the loopback reference. Near-silence means MIC2
  does not see the speaker; clipping breaks AEC.
- `@LB-RESULT` (even playbacks): AEC echo level against the previous raw
  playback. It includes the vendor noise suppression and depends on the two
  recordings being similar in loudness.

This test cannot measure the reference polarity or delay. If the AEC result is
poor while `echo.ref` looks healthy, the next step is to dump the raw mic/ref
pair and measure them. Product thresholds are still to be agreed (see
[acceptance](acceptance.md)).
