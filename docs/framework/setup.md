# Framework: Setup

[« Back to overview](../overview.md)

For building or changing the host, a game module, or the loader. To write a
`.wxlm` mod against an already-built host, see
[SDK: Getting started](../sdk/getting-started.md) instead.

## Prerequisites

* Python 3. Runs `scripts/generate_config.py` and `scripts/deploy.py`.
* devkitPro, with devkitPPC and devkitA64. From
  [devkitpro.org](https://devkitpro.org/wiki/Getting_Started). On Windows, if
  devkitPro is not at `C:\devkitPro`, set `DEVKITPRO_WIN`.
* Visual Studio (Windows only), for host-side tools like
  `tools/ring_log_reader`. Console builds do not need it.
* For Wii U: WUPS, libfunctionpatcher and libnotifications, installed into
  devkitPro. All three are submodules under `vendor/`. On Linux,
  `scripts/setup_wiiu_deps.sh` builds and installs them; otherwise `make
  install` per their READMEs into `/opt/devkitpro`.

## Getting the source

```bash
git clone --recurse-submodules https://github.com/TKVSC-Team/WiiXLaunch
```

Already cloned without it:

```bash
git submodule update --init --recursive
```

## Configuring the host

One file per game in `targets/<game>.json`. A `.wxlm` needs none of it.

```
targets/botw.json     Breath of the Wild   (the default)
targets/totk.json     Tears of the Kingdom
```

Pick a target by argument or `WIIXL_TARGET`:

```bash
build_switch.bat          # botw, the default
build_switch.bat totk     # targets/totk.json
```

### Target file contents

* `project`: name, version, author, description, `debug` (controls
  `EXL_DEBUG` on Switch).
* `memory`: heap/JIT/inline-pool sizes and the Cemu debug log buffer size.
  `jit_size` is hook trampolines; default `0x1000` covers about twenty hooks.
* `modules`: which `vendor/wiixlaunch-*` game modules to compile in. `[]`
  means base surfaces only.
* `patches`: `persist` decides whether declared patches survive the boot
  that applied them.
* `samples`: whether the framework's example mods belong on this host.
* `switch`: title ID, subsdk name, NPDM settings, `load_point`.
* `wiiu`: the plugin's `.wps` filename and target title IDs.
* `cemu`: entry hook address, graphic pack path/version, `module_matches`.

A section a target does not declare is not built. `targets/totk.json` has no
`wiiu` or `cemu` block, since there is no Wii U TotK.

`scripts/generate_config.py` turns the active target into headers under
`build/generated/`, regenerated every build. Edit the target file, not the
generated output.

### Which build of the game is this

A host built against one game build installs hooks at addresses meaning
something else on another build. The `identity` block fingerprints the
running game:

```json
"identity": {
  "switch": { "offset": "0x1000",     "length": 4096 },
  "wiiu":   { "address": "0x02000030", "length": 4096 },
  "known": [
    { "name": "1.5.0", "platform": "switch", "fingerprint": "0x1A2B3C4D" }
  ]
}
```

The host CRCs that slice and matches it against `known`. Switch declares an
offset (NSOs relocate per launch); Wii U declares an address.

Enroll a build from a dump rather than by hand:

```bash
python scripts/enrol_build.py --target totk --name 1.2.1 --nso <dump>/exefs/main
python scripts/enrol_build.py --target botw --name v208  --rpx <path>/U-King.rpx
```

Pointed at a directory of dumps, it enrolls a whole version history in one
pass. For a build you can run but have no dump of, the boot log prints the
fingerprint and `--fingerprint 0x… --platform switch` takes it directly.

An unrecognized build is reported as unrecognized rather than guessed. A
target with no `identity` block says that instead.

Pick the offset against the smallest build you support. TOTK 1.0.0-1.3.0
carry ~23.6 MB of read-only data; 1.4.0-1.4.3 only ~10.1 MB. An offset chosen
against the larger builds is past the end on 1.4.x.

Mods see build identity through `wiixl.version`; see
[SDK: one mod, several game versions](../sdk/imports-and-versioning.md#one-mod-several-game-versions).

### When the loader runs (Switch)

`switch.load_point`:

* `exl_main`: as soon as exlaunch hands over. Fine on BotW's nnSdk 4.4.0.
* `fs_ready`: at the first file the game opens. Required on nnSdk 15.x
  (TotK): `nn::fs` has no allocator until nnSdk installs one during init, so
  mounting the SD card earlier crashes inside `nn::fs::fsa::Register`.

## Building

```bash
build_cemu.bat      # the Cemu host
build_wiiu.bat      # the Wii U host
build_switch.bat    # the Switch host
build_all.bat       # all three hosts
```

Each script builds one host for one game. It does not run gates, build
example modules, package, or deploy. Pass a target to pick the game:
`build_switch.bat totk`.

CMake directly, Switch only:

```bash
cmake -B build/switch -DPLATFORM=SWITCH
cmake --build build/switch
```

## Verifying

```bash
test.bat            # or ./test.sh
```

Runs every gate: the loader fuzzer, host and format tests, surface coverage,
the import-header freshness check, an SDK cut verified by building a module
from it (see
[SDK: Distribution](../sdk/distribution.md#shipping-a-mod)), and the example
modules built for both machine types.

`test_switch_module` needs `build/switch/wiixlaunch-switch.elf` to exist
first; it says so if missing rather than skipping silently.

## Deploying

```bash
python scripts/deploy.py --target botw
```

Writes `deploy/` and copies into any emulator directories it finds:

* Switch: `deploy/switch/atmosphere/contents/<title_id>/exefs/`
* Wii U: `deploy/wiiu/wiiu/environments/aroma/plugins/<mod_name>.wps`
* Cemu: `deploy/cemu/graphicPacks/<graphic_pack_name>/`

To cut the two things a mod author needs from a built tree:

```bash
python scripts/make_sdk.py --host
```

## Known gaps

* The host is not published as a release. `sdk/` is committed and
  downloadable; the host pack still requires building this repo.
* Wii U has never been run outside a build. Cemu cannot run WUPS plugins, so
  the Cemu target proves nothing about it. Treat it as untested.
* The Switch host has no hook-probe target, so
  `wiixl.core:HookProbeTarget` returns null there and the hook-collision demo
  reports "not hooking." Loading, relocation, imports, `.init_array`, and
  arena accounting are all exercised on Switch; hook chaining is not.
* A Switch mod is built separately (`--target switch`,
  `build/switch-mods/`). The Wii U and Cemu module is the same file.
