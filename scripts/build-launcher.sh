#!/usr/bin/env bash
# Builds the launch payload for PPSA99577 (ps5-homebrew-dev-protocol's controllers/launch.c, title id compiled in) with the
# payload SDK that scripts/setup-deps.sh put in third_party/PS5SD/.deps. Run in the container, repository at /work.
# PS5SD's own scripts/build-launcher.sh is not used: its env.sh pins the title id to PPSA99576.
#   scripts/build-launcher.sh      -> build/tools/launch-mg.elf
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
deps=$root/third_party/PS5SD/.deps
title=PPSA99577
protocol=$deps/src/ps5-homebrew-dev-protocol
sdk=${PS5_PAYLOAD_SDK:-$deps/ps5-payload-sdk}
[ -f "$protocol/scripts/send-controller.sh" ] || { echo "build-launcher: $protocol missing (run setup-deps.sh)" >&2; exit 2; }
[ -x "$sdk/bin/prospero-clang" ] || { echo "build-launcher: $sdk/bin/prospero-clang missing (run setup-deps.sh)" >&2; exit 2; }
mkdir -p "$root/build/tools"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
elf=$(TMPDIR="$tmp" PS5_PAYLOAD_SDK="$sdk" PS5_CONTROLLER_COMPILE_ONLY=1 \
    bash "$protocol/scripts/send-controller.sh" launch "$title" localhost 9021 | tail -n 1)
[ -f "$elf" ] || { echo "build-launcher: the launch payload was not built" >&2; exit 1; }
cp "$elf" "$root/build/tools/launch-mg.elf"
echo "build-launcher: build/tools/launch-mg.elf ($(wc -c < "$elf" | tr -d ' ') bytes, title $title)"
