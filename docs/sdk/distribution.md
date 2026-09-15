# SDK: Distribution and troubleshooting

[« Back to overview](../overview.md) · [Getting started](getting-started.md)

## Shipping a mod

The `sdk/` folder is committed and self-contained. A mod author needs nothing
else.

```
sdk/
    scripts/build_mod.py   wxlm.py
            ppc_relocs.py       wxlm_mod.ld           PowerPC
            aarch64_relocs.py   wxlm_mod_aarch64.ld   AArch64
    include/wiixlaunch/imports/*.h        one per surface, generated
    include/wiixlaunch/mod_runtime.h      memcpy and friends
    include/wiixlaunch/mod_log.h          WIIXL_LOG
    include/wiixlaunch/mod_math.h         sqrt, sin, cos (there is no libm)
    include/wiixlaunch/mod_config.h       key = value settings reader
    include/wiixlaunch/mod_version.h      per-build offset tables
    include/wiixlaunch/format.hpp         the .wxlm layout
    include/wiixlaunch/patch_decl.hpp     WIIXL_DECLARE_PATCH / _CROSS
    sdk.json  README.md                   which host this was cut from, surface list
```

Build with:

```bash
python sdk/scripts/build_mod.py --source their_mod
python sdk/scripts/build_mod.py --source their_mod --target switch
```

`build_mod.py` derives its root from its own location; the framework tree
does not need to exist.

Inside a full checkout, a mod builds into `<wiixlaunch>/build`. From an
assembled SDK (detected by `sdk.json`), it builds into `<mod>/build` instead,
so a downloaded SDK never writes build output back into itself.

On the framework side, `test.bat`/`test.sh` runs a gate that builds a probe
module using only the assembled SDK and diffs it byte-for-byte against the
same probe built from the full checkout.

## Reading the boot log

Every subsystem logs through the same mechanism, prefixed by component:
`[loader:<id>]`, `Hook:`, `Arena:`, `Surface:`, `Patch:`, `Tick:`. Read
top-down and find the first missing or wrong line.

| symptom | look for |
|---|---|
| mod not in the list at all | `[loader] N module(s) found`. Is the filename `.wxlm` and in `mods/`? |
| `MISSING-SURFACE` | you called a symbol from a surface this host does not publish |
| loads, entry never runs | the `phase 0 reached` line for your id |
| entry runs, nothing happens | is the subsystem [armed](hooks-patches-and-runtime.md#arming)? |
| a write "succeeds" and does nothing | is something [pumping the tick](hooks-patches-and-runtime.md#picking-a-tick)? |
| game freezes | the in-flight tick record names the module and a sequence number. Frozen = hung inside a callback. Advancing = the game stopped calling the tick dispatcher. |

Load order is lexical by filename, and load order is hook install order (see
[Framework: Hooks](../framework/hooks.md#several-hooks-on-one-function)).
`a_first.wxlm` hooks before `b_second.wxlm`. Rename to reorder.

## Gates, iterating against a local checkout

```
build_all.bat                          all three hosts
test.bat                               every gate, plus the example modules
python scripts\surface_coverage.py     every public module function is reachable or excused
```

They do not check whether a surface's behavior matches the game module's,
whether a wrapper drops an argument, or whether anything actually happens in
the game.
