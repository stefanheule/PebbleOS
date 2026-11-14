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
- `applib/touch_service`: a watchface may `touch_service_subscribe` and then
  gets raw touch events (Graphite opens its calendar card with a tap). A
  touch begun before the watch was woken arrives with `non_navigational`.
  Touch navigation stays reserved for watchapps, and faces that do not
  subscribe get no touch, as upstream.
- `shell/normal/watchface`: Up, Select and Down go to the watchface while its
  focused top window has any click handler for that button, or while it has
  pushed a window over its main one (Graphite's event sheet and the
  ActionMenu it opens). The choice is made at button-down and holds until
  button-up; the app gets the raw press, so its own recognizer does single,
  repeating, long and multi clicks. Unclaimed buttons keep the launcher,
  timeline and quick launch, and combos still count claimed buttons. The
  Back forwarding above follows the same test, so a pushed window pops on
  Back and a face that drops its Back handler gets the peek dismissal back.
- `applib/app_message` + `shell/normal/phone_prefs`: the phone reads and
  writes the watch settings through the Graphite watchface
  (`7e5267ac-798f-4953-a645-1b87a5c29d96`); messages to any other app are
  left alone, and Graphite still receives every key and ignores them. The
  inbox hands a message with reserved keys to
  `sys_app_message_handle_phone_prefs`, which copies them and applies them
  on KernelBG (where the phone's BlobDB writes run):
  - `0x57505201`..`08` (Tap Up, Tap Down, Hold Up, Hold Center, Hold Down,
    Hold Back, Back+Up, Up+Down): a 16-byte app UUID, all `0xff` to
    unassign.
  - `0x57505300`..`3F`: `[u8 nameLen][name][value]` records for any name in
    `settings_blob_db.c`'s two whitelists whose value length matches the
    stored size. `settings_blob_db_set_local` writes it like a change made
    on the watch (in-memory pref reloaded, record left dirty so it syncs to
    the Core app), and nothing when the value is unchanged.
  - `0x57505400`: asks for every synced setting. A message that changed
    anything sends them as well: records on `0x57505401 + i`, split over
    AppMessages of at most 656 bytes from Graphite's UUID, each with the
    dump id on `0x575054FE` and the last with the count on `0x575054FF`.
    The kernel sends them with transaction ids `0xF8`..`0xFF`, which app
    outboxes skip and whose (n)acks never reach the app.

## Setup on Linux

`n2-agents` is provisioned by core's `infra/ansible/playbooks/vega-agent.yml`
(the Ubuntu packages, plus `cmake`, `ninja-build` and, since v4.47.0,
`device-tree-compiler`, the SDK, submodules and `.venv`); Orbit's update key
is in `~/.config/core-keychain` there. Read `SDK_VERSION` after updating and
install that SDK version if needed (bump `vega_agent_pebbleos_sdk_version` in
that role too); the build picks the newest installed SDK that satisfies it.
After a rebase, `pip install -r requirements.txt` in `.venv` and check
`requirements-brew.txt` on the Mac: new releases add host tools. Run heavy builds/tests with
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
