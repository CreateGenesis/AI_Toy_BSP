#!/usr/bin/env bash
# Build the push-to-talk microphone and AEC test (AEC capture enabled).
# Flash dist/aec_loopback_QIO_1.0.0.bin and watch UART1 at 460800.
set -euo pipefail
exec bash "$(dirname "${BASH_SOURCE[0]}")/tuyaopen-app.sh" aec_loopback
