#!/usr/bin/env bash

set -euo pipefail

# Builds Stefan's PebbleOS for his Pebble Time 2 and publishes it so Orbit
# hands it to the Core app, which asks once and then installs it on the watch.
#
#   stefan/release.sh --notes "<what's new>" [--local] [--no-push]
#
# The version is the upstream release this branch sits on plus -stefan.N,
# e.g. v4.38.4-stefan.2, as a git tag on HEAD: the firmware reads its version
# from `git describe`, so the tag is what the watch reports. A HEAD that
# already carries such a tag is rebuilt under it rather than bumped.
#
# obelix runs its firmware in place from one of two flash slots, so a release
# is two builds, one linked per slot, merged into one .pbz the same way
# upstream's CI does (tools/merge_pbz.py). The watch takes whichever slot is
# not running.
#
#   --local    build and sign, but do not upload or tell the phone
#   --no-push  upload, but let Orbit find it on its daily check
#
# Publishing goes through orbit/scripts/publish-pebble-firmware.sh in the core
# checkout (CORE_DIR, default ~/dev/core), which signs with Orbit's update key.

readonly board="obelix@pvt"
readonly board_name="obelix_pvt"
readonly repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly core="${CORE_DIR:-${HOME}/dev/core}"

notes=""
publish_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --notes)
      notes="${2:-}"
      shift 2
      ;;
    --local | --no-push)
      publish_args+=("$1")
      shift
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done
if [[ -z "${notes}" ]]; then
  echo "Usage: $(basename "$0") --notes \"<what's new>\" [--local] [--no-push]" >&2
  exit 1
fi

cd "${repo}"
# shellcheck source=/dev/null
source .venv/bin/activate

if ! git diff-index --quiet --ignore-submodules=dirty HEAD --; then
  echo "The working tree has uncommitted changes; the version would say -dirty." >&2
  exit 1
fi
if [[ ! -x "${core}/orbit/scripts/publish-pebble-firmware.sh" ]]; then
  echo "No core checkout at ${core} (set CORE_DIR)." >&2
  exit 1
fi

# --- Version -----------------------------------------------------------------

base="$(git describe --tags --abbrev=0 --match 'v[0-9]*' --exclude '*-stefan.*')"
readonly base
tag="$(git tag --points-at HEAD --list "${base}-stefan.*" | sort -V | tail -1)"
if [[ -z "${tag}" ]]; then
  last="$(git tag --list "${base}-stefan.*" | sed "s/^${base}-stefan\.//" | sort -n | tail -1)"
  tag="${base}-stefan.$(( ${last:-0} + 1 ))"
  git tag -a "${tag}" -m "${notes}"
  echo "Tagged ${tag}"
else
  echo "Rebuilding ${tag}"
fi
readonly tag

# --- Build both slots --------------------------------------------------------

dist="${repo}/build/dist"
mkdir -p "${dist}"
for slot in 0 1; do
  dir="build/release-slot${slot}"
  pbl configure -b "${dir}" --board "${board}" -DCONFIG_FIRMWARE_SLOT="${slot}" -DCONFIG_RELEASE=y >/dev/null
  pbl build -b "${dir}" >/dev/null
  pbl bundle -b "${dir}" >/dev/null
  built="${dir}/normal_${board_name}_${tag}_slot${slot}.pbz"
  if [[ ! -f "${built}" ]]; then
    echo "Expected ${built}; the build did not pick up ${tag}." >&2
    ls "${dir}"/*.pbz >&2 || true
    exit 1
  fi
done

readonly pbz="${dist}/normal_${board_name}_${tag}.pbz"
python3 tools/merge_pbz.py \
  --slot0-pbz "build/release-slot0/normal_${board_name}_${tag}_slot0.pbz" \
  --slot1-pbz "build/release-slot1/normal_${board_name}_${tag}_slot1.pbz" \
  --output "${pbz}" >/dev/null
echo "Built ${pbz}"

# --- Publish -----------------------------------------------------------------

"${core}/orbit/scripts/publish-pebble-firmware.sh" "${pbz}" \
  --board "${board_name}" --notes "${notes}" "${publish_args[@]+"${publish_args[@]}"}"

# The branch is never rewritten (a rebase starts a new branch), so a plain
# push always fast-forwards.
if [[ " ${publish_args[*]+"${publish_args[*]}"} " != *" --local "* ]]; then
  git push -q origin "$(git branch --show-current)" "refs/tags/${tag}"
fi
