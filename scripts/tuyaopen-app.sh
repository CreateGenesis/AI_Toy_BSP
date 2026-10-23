#!/usr/bin/env bash
# Shared build steps for Chat Robot T5-E1 apps on the official TuyaOpen SDK.
# Usage: bash scripts/tuyaopen-app.sh <app directory under firmware/apps>
# Links the board adapter and app into TuyaOpen, registers the board choice,
# fetches the pinned T5AI platform and applies the verified DAC patch once.
set -euo pipefail

app_name="${1:?usage: tuyaopen-app.sh <app>}"
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sdk_root="$project_root/TuyaOpen"
app_src="$project_root/firmware/apps/$app_name"
board_src="$project_root/firmware/tuyaopen/CHAT_ROBOT_T5E1"
dac_patch="$project_root/firmware/tuyaopen/patches/tkl_audio_lr_dac.patch"
app_link="$sdk_root/apps/$app_name"
board_link="$sdk_root/boards/T5AI/CHAT_ROBOT_T5E1"
board_kconfig="$sdk_root/boards/T5AI/Kconfig"
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
if [[ ! -f "$app_src/CMakeLists.txt" ]]; then
    printf 'App not found at %s\n' "$app_src" >&2
    exit 2
fi

ln -sfn "$board_src" "$board_link"
ln -sfn "$app_src" "$app_link"

if ! grep -q 'BOARD_CHOICE_CHAT_ROBOT_T5E1' "$board_kconfig"; then
    marker='# <new-board-add: This line cannot be deleted or modified>'
    block='    config BOARD_CHOICE_CHAT_ROBOT_T5E1\n        bool "CHAT_ROBOT_T5E1"\n        if (BOARD_CHOICE_CHAT_ROBOT_T5E1)\n            rsource "./CHAT_ROBOT_T5E1/Kconfig"\n        endif\n'
    grep -qF "$marker" "$board_kconfig" || {
        printf 'Board Kconfig marker not found in %s\n' "$board_kconfig" >&2
        exit 2
    }
    sed -i "s|^$marker|$block\n$marker|" "$board_kconfig"
    printf 'Registered CHAT_ROBOT_T5E1 in %s\n' "$board_kconfig"
fi

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

    if git -C "$platform_dir" cat-file -e "$platform_commit^{commit}" 2>/dev/null; then
        return 0
    fi
    printf 'Resuming T5AI platform fetch.\n'
    git -C "$platform_dir" remote set-url origin "${platform_urls[0]}"
    if ! git -C "$platform_dir" fetch --progress origin; then
        git -C "$platform_dir" remote set-url origin "${platform_urls[1]}"
        git -C "$platform_dir" fetch --progress origin
    fi
}

fetch_platform
# Left-only DAC leaves AUDIO_LN at 0 V. The patch enables both sides.
if git -C "$platform_dir" apply --reverse --check "$dac_patch" 2>/dev/null; then
    printf 'DAC patch already applied.\n'
else
    git -C "$platform_dir" checkout --quiet "$platform_commit"
    git -C "$platform_dir" apply "$dac_patch"
fi

cd "$sdk_root"
# shellcheck disable=SC1091
. ./export.sh
cd "$app_link"
rm -f using.config
tos.py build
printf 'Image: %s/dist/\n' "$app_src"
