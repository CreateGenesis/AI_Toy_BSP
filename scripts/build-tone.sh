#!/usr/bin/env bash
# Build the custom-board fixed-tone playback test.
# This links the schematic board adapter and does not build a cloud demo.
set -euo pipefail
exec bash "$(dirname "${BASH_SOURCE[0]}")/tuyaopen-app.sh" tone_playback
