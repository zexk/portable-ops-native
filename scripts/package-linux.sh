#!/usr/bin/env bash
# Build local Linux x86-64 archives, including native game code and modules.
# Usage: scripts/package-linux.sh [po|pop|pop-eu|pop-jp|all] [PortableKit release options...]
# Requires Python 3.11+ and Docker or Podman; generated code must already exist locally.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
    exit 0
fi
selection=${1:-all}
[[ $# -eq 0 ]] || shift
case "$selection" in
    po|pop|pop-eu|pop-jp) editions=("$selection") ;;
    all) editions=(po pop pop-eu pop-jp) ;;
    *) echo 'Usage: scripts/package-linux.sh [po|pop|pop-eu|pop-jp|all] [release options...]' >&2; exit 2 ;;
esac
for edition in "${editions[@]}"; do
    PORTABLEKIT_GAME_EDITION="$edition" \
    PORTABLEKIT_RELEASE_WORK="$root/out/release-linux-$edition" \
        bash "$root/portablekit/scripts/release_linux.sh" \
        --game "$root" --no-flatpak --jobs 2 "$@"
done
