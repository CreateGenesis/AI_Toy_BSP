#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

printf 'Project: %s\n' "$project_root"
if [[ -f "$project_root/WukongSDK/Makefile" && -d "$project_root/WukongSDK/software/TuyaOS/apps" ]]; then
    printf 'Optional Wukong SDK: present\n'
else
    printf 'Optional Wukong SDK: not present\n'
fi

if [[ -f "$project_root/TuyaOpen/tos.py" ]]; then
    revision=$(git -C "$project_root/TuyaOpen" rev-parse --short HEAD)
    printf 'TuyaOpen SDK: present (%s)\n' "$revision"
else
    printf 'TuyaOpen SDK: missing\n'
fi

if [[ -f "$project_root/TuyaOpen/examples/peripherals/audio_codecs/output_speaker/src/example_output_speaker.c" ]]; then
    printf 'Cloud-free audio example: present\n'
else
    printf 'Cloud-free audio example: missing\n'
fi

if [[ -f "$project_root/references/ai-toy.pdf" ]]; then
    printf 'Custom-board schematic: present\n'
else
    printf 'Custom-board schematic: pending\n'
fi
printf 'Own-platform transport protocol: pending\n'

for tool in git cmake ninja make python3; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '%s: available\n' "$tool"
    else
        printf '%s: missing\n' "$tool"
    fi
done

if command -v dpkg-query >/dev/null 2>&1; then
    for package in doxygen lib32z1 libc6:i386 libstdc++6:i386; do
        if [[ $(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true) == 'install ok installed' ]]; then
            printf '%s: installed\n' "$package"
        else
            printf '%s: missing\n' "$package"
        fi
    done
fi
