# Stefan's PebbleOS fork

This checkout (`~/dev/PebbleOS`, `github.com/stefanheule/PebbleOS`) is a
personal fork of `coredevices/PebbleOS` for one watch: Stefan's Pebble Time 2
(board `obelix@pvt`). It carries a few small patches on top of an upstream
release tag; everything else is upstream's.

## Branches and versions

- `upstream` is `coredevices/PebbleOS`; `origin` is the fork.
- One branch per upstream release, `stefan/<tag>` (e.g. `stefan/v4.38.4`),
  holding the patches on top of that tag. Always base on a stable upstream
  release tag with a `normal_obelix_pvt_<tag>.pbz` asset. Pick the highest
  three-part version, not publication order or GitHub’s latest flag: older
  maintenance branches also receive releases.
- A branch is never rewritten. Moving to a new release is
  `stefan/rebase.sh <tag>`, which starts `stefan/<tag>` and leaves the old
  branch alone. Never force-push.
- Stefan explicitly authorizes this workflow for future requested firmware
  updates: create/switch to the next `stefan/<tag>` branch and rebase the
  custom patches with `stefan/rebase.sh`, resolve routine conflicts, verify,
  commit, push and publish without asking for approval again. This is a
  scoped exception to core’s main-only/no-rebase rules for this separate
  repository. Preserve previous release branches and never force-push.
- Fork builds are tagged `<upstream tag>-stefan.N`. The firmware takes its
  version from `git describe`, so this tag is what the watch's About screen
  and the Core app show.

## Two checkouts

Vega agents work on this fork both on the Mac (`~/dev/PebbleOS`) and on
Nova's `n2-agents` (same path, Ubuntu, set up by the `nova-vega-agent` role in
core). Nothing fast-forwards either checkout on its own, so start with
`git pull --ff-only` on the current `stefan/<tag>` branch and push what you
commit. After a `stefan/rebase.sh` on one machine, check out the new branch
on the other.

## Releasing

```shell
stefan/release.sh --notes "<one line, shown on the phone>"
```

It tags HEAD, builds slot 0 and slot 1 in release mode, merges them into one
`.pbz` (`tools/merge_pbz.py`, as upstream CI does), and calls
`orbit/scripts/publish-pebble-firmware.sh` in the core checkout. That signs a
manifest with Orbit's update key, uploads to
`downloads.terra0.com/pebbleos-obelix/` and pushes a `pebble_app_update`;
Orbit verifies it and hands the `.pbz` to the Core app, which opens the
Watches tab and asks once before installing. `--local` builds and signs only.

By hand: Core app → Settings → Show debug options → the watch → Firmware
Update Debug → Sideload FW, and pick the `.pbz` from `build/dist/`.

## Official updates

The Core app offers each official release as an update; accepting it replaces
the fork with stock firmware. Instead: `stefan/rebase.sh <new tag>`, then
`stefan/release.sh`. To go back to stock, sideload
`normal_obelix_pvt_<tag>.pbz` from the upstream GitHub release.

A bad build is recoverable: the watch boots the other slot or the recovery
firmware (hold Back+Up+Select), and a stock `.pbz` can be sideloaded from
there.

## The patches

- `shell/normal/watchface`: a short Back press on a watchface that registered
  a Back click handler goes to the watchface instead of dismissing the
  timeline peek. Back-hold quick launch and Back+Up stay the kernel's.
- `applib/voice`: Down stops a dictation recording, like Select (Iris
  records with the system dictation UI).

## Setup on Linux

`n2-agents` is provisioned by core's `infra/ansible/playbooks/vega-agent.yml`
(the Ubuntu packages, plus `cmake` and `ninja-build`, the SDK, submodules and
`.venv`); Orbit's update key is in `~/.config/core-keychain` there.
Read `SDK_VERSION` after updating and install that SDK version if needed;
source its `env.sh` before building. Run heavy builds/tests with
`nova-agent-work` and keep their output under its scratch workspace.
Both firmware slots must build before publishing.

## Setup on a new Mac

```shell
curl -LsSf https://github.com/coredevices/PebbleOS-SDK/releases/latest/download/pebbleos-sdk-installer.sh | sh -s -- --defaults
brew install python openocd $(cat requirements-brew.txt)
git submodule update --init --depth 1 --recursive
python3 -m venv .venv && . .venv/bin/activate && pip install -r requirements.txt
```

Testing on the emulator: `pbl configure --board qemu_emery && pbl build &&
pbl run`. See `docs/development/emulation/emulator.md` and
`docs/development/tests/integration.md` for current emulator and test commands.
