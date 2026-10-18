#!/usr/bin/env bash
# Dumps every stage on the disc for recompilation, without playing to them.
#
# usage: scripts/sweep-stages.sh po|pop DUMP_DIR
#
# One run of the game per stage, inside a private Xvfb display and without
# audio, so nothing reaches the desktop or the speakers: it boots to the first
# stage the game unloads, loads the stage with the engine's own loader
# (common/host/stage_sweep.hpp), dumps it and stops. Then:
#   python3 scripts/add-modules.py <edition> DUMP_DIR
set -euo pipefail
cd "$(dirname "$0")/.."
edition=$1
dumps=$(realpath -m "$2")
case $edition in
  po)  game_env=MGSPO;  program=PortableOpsNative;     data=PortableOpsNative/ULUS10202 ;;
  pop) game_env=MGSPOP; program=PortableOpsPlusNative; data=PortableOpsPlusNative/ULUS10290 ;;
  *) echo "usage: $0 po|pop DUMP_DIR" >&2; exit 2 ;;
esac
disc=$(sed -n 's/^disc_image=//p' "$HOME/.local/share/$data/settings.ini")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# The stage directories, from the disc image's listing.
nix shell nixpkgs#cdrtools --command isoinfo -f -i "$disc" 2>/dev/null |
  sed -n 's|^/PSP_GAME/USRDIR/stage/\([^/]*\)/\1\.prx$|\1|p' | sort -u > "$work/stages"
echo "$(wc -l < "$work/stages") stages"
mkdir -p "$dumps"
sweep_one() {
  env SDL_AUDIO_DRIVER=dummy "${game_env}_NO_RENDER=1" "${game_env}_NO_AUDIO=1" "${game_env}_SWEEP_STAGE=$1" \
      "${game_env}_DUMP_MODULES=$dumps" timeout -s KILL 120 "./out/$edition/bin/$program" > "$work/$1.log" 2>&1 || true
  grep -qE 'stage sweep done|already has recompiled code' "$work/$1.log" && echo "$1" || echo "$1: FAILED"
}
export -f sweep_one
export game_env edition program dumps work
nix develop --command nix shell nixpkgs#xvfb-run --command \
  xvfb-run -a -s '-screen 0 640x480x24' bash -c "xargs -P 4 -I{} bash -c 'sweep_one {}' < '$work/stages'"
