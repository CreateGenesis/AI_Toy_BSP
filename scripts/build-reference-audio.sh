#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sdk_root="$project_root/TuyaOpen"
sample_root="$sdk_root/examples/peripherals/audio_codecs/output_speaker"
board_revision="${1:-}"

if [[ "$board_revision" != v101 && "$board_revision" != v102 ]]; then
    printf 'Usage: bash scripts/build-reference-audio.sh v101|v102\n' >&2
    printf 'This builds the official T5AI-Board example, not custom-board firmware.\n' >&2
    exit 2
fi

if [[ ! -f "$sample_root/config/TUYA_T5AI_BOARD.config" ]]; then
    printf 'TuyaOpen audio example not found at %s\n' "$sample_root" >&2
    exit 2
fi

cd "$sdk_root"
. ./export.sh
cd "$sample_root"
tos.py config choice -c TUYA_T5AI_BOARD.config
tos.py config set "CONFIG_TUYA_T5AI_BOARD_VERSION_${board_revision#v}=y"
tos.py build
