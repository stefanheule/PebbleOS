#!/usr/bin/env bash

set -euo pipefail

# Moves Stefan's patches onto a new upstream release.
#
#   stefan/rebase.sh v4.38.5
#
# Starts a new branch stefan/<tag> from the current stefan/<old tag> and
# rebases it onto <tag>. The old branch stays as it was, so nothing is ever
# force-pushed and the previous release can still be rebuilt. On a conflict
# the rebase stops like any other: fix, `git rebase --continue`, then run
# stefan/release.sh.

readonly target="${1:-}"
if [[ -z "${target}" ]]; then
  echo "Usage: $(basename "$0") <upstream tag>" >&2
  echo "Recent upstream releases:" >&2
  git tag --list 'v*' --sort=-v:refname | grep -v -- '-stefan\.' | head -5 >&2
  exit 1
fi

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if ! git diff-index --quiet --ignore-submodules=dirty HEAD --; then
  echo "Commit or discard local changes first." >&2
  exit 1
fi

git fetch -q upstream --tags
if ! git rev-parse -q --verify "refs/tags/${target}" >/dev/null; then
  echo "No upstream tag ${target}." >&2
  exit 1
fi

current="$(git branch --show-current)"
base="$(git describe --tags --abbrev=0 --match 'v[0-9]*' --exclude '*-stefan.*')"
readonly current base
if [[ "${current}" != "stefan/${base}" ]]; then
  echo "Expected to be on stefan/${base}, found ${current}." >&2
  exit 1
fi
if git rev-parse -q --verify "refs/heads/stefan/${target}" >/dev/null; then
  echo "stefan/${target} already exists." >&2
  exit 1
fi

echo "Patches on ${current}:"
git log --oneline "${base}..HEAD"

git checkout -q -b "stefan/${target}"
git rebase --onto "${target}" "${base}"
git submodule update --init --depth 1 --recursive -q

echo
echo "Now on stefan/${target}. Next: stefan/release.sh --notes \"PebbleOS ${target#v} with my patches\""
