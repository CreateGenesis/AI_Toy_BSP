#!/usr/bin/env bash
# Build the custom-board fixed-tone playback test.
# This links the schematic board adapter and does not build a cloud demo.
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sdk_root="$project_root/TuyaOpen"
app_src="$project_root/firmware/apps/tone_playback"
board_src="$project_root/firmware/tuyaopen/CHAT_ROBOT_T5E1"
app_link="$sdk_root/apps/tone_playback"
board_link="$sdk_root/boards/T5AI/CHAT_ROBOT_T5E1"
platform_dir="$sdk_root/platform/T5AI"
platform_commit="e4286054eccbfb136faa0f857861aaa3592fac0e"
# Gitee is the reachable mirror. GitHub clones of this repo stall around 440 MB.
platform_urls=(
    "https://gitee.com/tuya-open/TuyaOpen-T5AI.git"
    "https://github.com/tuya/TuyaOpen-T5AI.git"
)

if [[ ! -f "$sdk_root/tos.py" ]]; then
    printf 'TuyaOpen SDK not found at %s\n' "$sdk_root" >&2
    exit 2
fi

ln -sfn "$board_src" "$board_link"
ln -sfn "$app_src" "$app_link"

fetch_platform() {
    local url
    mkdir -p "$sdk_root/platform"
    if [[ ! -d "$platform_dir/.git" ]]; then
        rm -rf "$platform_dir"
        for url in "${platform_urls[@]}"; do
            printf 'Cloning T5AI platform from %s\n' "$url"
            if git clone --progress "$url" "$platform_dir"; then
                return 0
            fi
            rm -rf "$platform_dir"
            printf 'Clone failed, trying the next mirror.\n' >&2
        done
        return 1
    fi

    printf 'Resuming T5AI platform fetch.\n'
    git -C "$platform_dir" remote set-url origin "${platform_urls[0]}"
    if ! git -C "$platform_dir" fetch --progress origin; then
        git -C "$platform_dir" remote set-url origin "${platform_urls[1]}"
        git -C "$platform_dir" fetch --progress origin
    fi
}

fetch_platform
git -C "$platform_dir" checkout --quiet "$platform_commit"
# Left-only DAC leaves AUDIO_LN at 0 V. This patch enables both sides.
git -C "$platform_dir" apply "$project_root/firmware/tuyaopen/patches/tkl_audio_lr_dac.patch"

cd "$sdk_root"
# shellcheck disable=SC1091
. ./export.sh
cd "$app_link"
rm -f using.config
tos.py build
