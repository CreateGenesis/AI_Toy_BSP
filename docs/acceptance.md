# Audio And Recognition Acceptance

Run these checks on the actual custom board. Save raw captures, reference
signals, logs and recognition transcripts in the ignored `artifacts/`
directory. Speech recordings may contain private data and should not enter
version control.

| Check | Test | Record |
| --- | --- | --- |
| Capture | Record near-field, far-field and quiet-room speech at the BSP's selected PCM format. | Sample rate, channels, clipping, noise floor, dropped frames and buffer overruns. |
| Playback | Play speech and a calibrated tone at expected volume. | Gain, distortion, interruptions, underruns and amplifier enable behavior. |
| AEC | Capture while the speaker plays a known reference, including double-talk. Test both the schematic polarity and the swapped `MICP2_LB`/`MICN2_LB` interpretation. | Reference-path availability, echo level before/after processing and remaining speech intelligibility. |
| Recognition | Send the same Mandarin test utterances through the chosen recognition path with quiet, noisy and playback-active conditions. | Character error rate, failed utterances and latency, comparing raw and processed audio. |
| Recovery | Interrupt capture, playback and the network session; reconnect. | Audio restart, stale buffers, duplicate utterances and user-visible delay. |

Pass thresholds and the test utterance set must be agreed with the product
owner after the board acoustics and recognition location are known. A successful
firmware build or record-then-play demo is not evidence that recognition or
AEC meets product quality.
