# Stefan's PebbleOS fork

This checkout (`~/dev/PebbleOS`, `github.com/stefanheule/PebbleOS`) is a
personal fork of `coredevices/PebbleOS` for one watch: Stefan's Pebble Time 2
(board `obelix@pvt`). It carries a few small patches on top of an upstream
release tag; everything else is upstream's.

## Branches and versions

- `upstream` is `coredevices/PebbleOS`; `origin` is the fork.
- One branch per upstream release, `stefan/<tag>` (e.g. `stefan/v4.38.4`),
  holding the patches on top of that tag. Releases are tags on `v4.xx-branch`
  upstream, not on `main`, so always base on a release tag.
- A branch is never rewritten. Moving to a new release is
  `stefan/rebase.sh <tag>`, which starts `stefan/<tag>` and leaves the old
  branch alone. Never force-push.
- Fork builds are tagged `<upstream tag>-stefan.N`. The firmware takes its
  version from `git describe`, so this tag is what the watch's About screen
  and the Core app show.

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

## Setup on a new Mac

```shell
curl -LsSf https://github.com/coredevices/PebbleOS-SDK/releases/latest/download/pebbleos-sdk-installer.sh | sh -s -- --defaults
brew install python openocd $(cat requirements-brew.txt)
git submodule update --init --depth 1 --recursive
python3 -m venv .venv && . .venv/bin/activate && pip install -r requirements.txt
```

Testing on the emulator: `pbl configure --board qemu_emery && pbl build
qemu_image_micro qemu_image_spi && pbl qemu`, then `pebble install x.pbw
--qemu localhost:12344`.
