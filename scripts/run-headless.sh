#!/usr/bin/env bash
# Run one edition under a virtual X server with software Vulkan (lavapipe), so
# no window opens and screenshots work anywhere. Frames land in out/shots-<edition>.
# usage: scripts/run-headless.sh po|pop|pop-eu|pop-jp [seconds] [extra env assignments...]
set -euo pipefail
edition=$1; seconds=${2:-120}; shift $(( $# > 1 ? 2 : 1 ))
cd "$(dirname "$0")/.."
case $edition in
  po) program=PortableOpsNative; prefix=MGSPO ;;
  pop|pop-eu|pop-jp) program=PortableOpsPlusNative; prefix=MGSPOP ;;
  *) echo "edition must be po, pop, pop-eu or pop-jp" >&2; exit 2 ;;
esac
mesa=$(nix build nixpkgs#mesa --no-link --print-out-paths | head -1)
shots=$PWD/out/shots-$edition
rm -rf "$shots"; mkdir -p "$shots"
env VK_ICD_FILENAMES="$mesa/share/vulkan/icd.d/lvp_icd.x86_64.json" \
    "${prefix}_SCREENSHOT_DIR=$shots" "${prefix}_NO_AUDIO=1" "$@" \
  nix shell nixpkgs#xvfb-run --command \
  xvfb-run -a -s '-screen 0 1280x720x24' \
  nix develop --command timeout -k 5 "$seconds" "./out/$edition/bin/$program" 2>&1 | tee "out/$edition-run.log" | grep -E 'Runtime stopped|\[module\]|hle-stub' || true
