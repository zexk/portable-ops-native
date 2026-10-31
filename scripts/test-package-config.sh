#!/usr/bin/env bash
# Runs without game data or generated corpora; suitable for public CI.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir=$(mktemp -d)
trap 'rm -rf "$repo_dir"' EXIT
for edition in po pop; do
    mkdir -p "$repo_dir/profiles/$edition/overlays/module"
    touch "$repo_dir/profiles/$edition/overlays/module/meta.txt"
    (
        export PORTABLEKIT_GAME_EDITION="$edition"
        source "$root/packaging/release.env"
        [[ "$RELEASE_GENERATED_DIR" == "profiles/$edition/generated" ]]
        [[ "${RELEASE_CMAKE_ARGS[0]}" == "-DMGS_EDITION=$edition" ]]
        [[ "$RELEASE_OVERLAYS" -eq 1 ]]
        case "$edition" in
            po) [[ "$RELEASE_TARGET" == PortableOpsNative ]] ;;
            pop) [[ "$RELEASE_TARGET" == PortableOpsPlusNative ]] ;;
        esac
    )
done
if "$root/scripts/package-linux.sh" invalid >/dev/null 2>&1; then
    echo 'Invalid edition accepted' >&2
    exit 1
fi
echo 'Package edition checks passed'
