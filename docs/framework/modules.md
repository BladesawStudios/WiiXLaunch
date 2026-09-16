# Framework: Game modules

[« Back to overview](../overview.md)

## What a module is

Base WiiXLaunch is a generic, game-agnostic hooking framework. A game module
is game-specific knowledge (offsets, vtable slots, actor spawn plumbing)
promoted into a high-level API and published as its own repository. Add the
modules you need, per project.

The first is [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw),
for Breath of the Wild.

A game module is reached two ways:

* Host-built code includes the module's headers directly
  (`#include <wiixlaunch/botw/botw.hpp>`) and calls its classes. Everything
  is available, nothing is versioned.
* A `.wxlm` never sees those headers. The module republishes its public API
  as versioned surfaces (`botw.player`, `botw.gfx`, ...) and a mod imports
  symbols by name. See
  [SDK: Imports and versioning](../sdk/imports-and-versioning.md).

## Adding a module

```bash
git submodule add https://github.com/TKVSC-Team/wiixlaunch-botw vendor/wiixlaunch-botw
git submodule update --init --recursive
```

Name it in the target file:

```json
"modules": ["wiixlaunch-botw"]
```

`[]` means base surfaces only. A target with no `modules` key builds every
`vendor/wiixlaunch-*` found; every build prints which it chose and which it
found but left out.

No build script edits needed. Every build path scans `vendor/wiixlaunch-*`
and wires up the include path: `scripts/generate_config.py` for the Switch
`config.mk`, `build_cemu.bat`/`.sh` via `-I`, `build_wiiu.bat`/`.sh` staged
under `modules/<name>/include` (`scripts/wiiu/Makefile`'s `INCLUDES` globs
it), and `CMakeLists.txt` for the direct-CMake Switch path.

The host reaches the module through one fixed path: a module provides
`<wiixlaunch/module.hpp>` declaring `WiiXLaunch::GameModule::Register()`, and
`src/main.cpp` includes it under `__has_include`. This is why a tree with no
module in `vendor/` still compiles, and why it's a plain call rather than a
weak symbol (see
[Cemu code cave relocation](cemu-relocation.md#weak-symbols-do-not-work-here)).

## Naming convention

A module must live at `vendor/wiixlaunch-<name>` for auto-discovery. Other
`vendor/` dependencies (`wut`, `wups`, `libfunctionpatcher`) also have their
own `include/` dirs but are wired up through their own Makefile flow, not
this scan.

## Available modules

* [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw): Breath
  of the Wild. Host-built classes for the player, actors, pouch, armour,
  game time, weather, climate, map/region state, the controller, the
  camera, and `NVN`/`GX2` graphics injection (see
  [Graphics Injection](graphics-injection.md)). Publishes eighteen `botw.*`
  surfaces (`botw.player`, `botw.actor`, `botw.gfx`, `botw.gui`, `botw.vfx`,
  `botw.flyt`, `botw.region`, `botw.camera`, `botw.display`, `botw.events`,
  `botw.sound`, `botw.memory`, `botw.gamedata`, `botw.world`, `botw.input`,
  `botw.map`, `botw.pouch`, `botw.armour`). See its README for per-platform
  coverage and `sdk/include/wiixlaunch/imports/` for what each surface
  publishes.

## Writing your own module

* Resolve WiiXLaunch's core via `<wiixlaunch/...>`, not `"../..."`. A module
  isn't nested inside a project's `include/wiixlaunch/` tree once pulled in
  as a submodule; only angle-bracket includes resolve regardless of where
  the module's files sit on disk.
* Header-only, matching the framework: no separate `.cpp` to link into every
  consumer.
* Self-installing hooks. Give the module a static `Init()` the host calls
  once from `WiiXLaunch_Init()`; everything after that is typed getters.
  See [Hooks](hooks.md).
* Keep the module's own diagnostics on its own logger. `WIIXL_LOG` is for
  host and mod code; `wiixlaunch-botw` uses `BotW::OSLog` for internals so
  they don't compete for the 200-character Cemu ring. See
  [Debugging](debugging.md#botwoslog-is-a-different-thing).
* OS-level shims belong in base, not your module. On Cemu the payload has
  no import table, so every OS call goes through an `import.<lib>.<Name>`
  shim in a `src/cemu/*.asm` table. Put a shim in your module only if it's
  game-specific. `coreinit` (filesystem, logging, time, memory) is present
  in every Wii U title and lives in base WiiXLaunch
  (`src/cemu/cemu_fs.asm`, `cemu_logging.asm`, `cemu_mem.asm`,
  `cemu_time.asm`). `wiixlaunch-botw` keeps only `gx2_imports.asm`. Full
  rule: [Framework: wiixl.net](net.md#the-static-import-rule).
* Nominate, don't assume. Base doesn't know where the game's entry is or
  what a frame is. A module nominates those facts: the Cemu load point
  with `WIIXL_DECLARE_LOAD_POINT`, the frame source via `Tick::RunAll()`
  from its swap hook. See [the module loader](loader.md).
* Capability flags, not runtime checks. Expose platform-only features as
  `constexpr bool SupportsX`, not a function taking a platform argument.

### A game module is host code; a mod is scoped

A game module (`vendor/wiixlaunch-*`) is compiled into the payload: its
memory is the host's (`Arena::AllocHost`), its hooks are attributed to
`host`, and it reads game content the way the host does. A mod (`.wxlm`) is
loaded at runtime: a bounded arena grant, hooks and patches attributed to
its own id, resources under `content/WiiXLaunch/mods/<id>/` via the scoped
read. `wiixlaunch-botw` never uses the mod-scoped call. It has no id and
no directory. If you're writing a game module and reaching for
`ModReadFile`, that's the wrong one.

---

## Standing rules

Each rule below came from a real failure, stated with it. They apply to
base, to game modules, and to the test gates.

### 1. Anything referenced only from outside the compiler's view needs `__attribute__((used))`

GCC emits an `inline` definition only when some translation unit odr-uses
it. A reference the compiler can't see doesn't count. Bitten both ways: a
data global written by `scripts/deploy.py` and read by a `src/cemu/*.asm`
shim table (`g_CemuMemShimTableOffset`, shipped unreachable), and an inline
function whose only caller was a hand-written `asm()` block
(`WiiXLaunch_LoadPointProbe`, failed to link). Mark both:

```cpp
__attribute__((section(".data"), used)) inline uint32_t g_Whatever = 0;
extern "C" __attribute__((used)) inline void Whatever() {}
```

`used` cannot rescue a header nobody included; some translation unit must
include it (`src/cemu/bootstrap.cpp` pulls in the umbrella).
`deploy.py` refuses to build a pack whose base `WIIXL_OFFSET_SYMBOL` did not
resolve.

### 2. A self-check the optimizer can answer is not testing anything

A `static uint32_t probe[16];` that nothing writes is provably all zero, so
"did the loader zero my .bss?" folds to `true` and the array is dropped
from `.bss` entirely: the check passes with no loader present. A
`static Fn p = &undefined_import;` is provably that address, so an indirect
call through it becomes a direct branch with a relocation that can't reach
a host address. Both compile, both report success, neither runs the thing
under test.

Fix: `volatile` on anything another agent writes at runtime (loader,
relocator, hook, host). Ask of every "did X really happen?" test whether
the compiler can answer without X happening. If it can, the test is
decoration.

### 3. A test that encodes the implementation's assumptions can only confirm them

The `.wxlm` fuzzer's first header sweep judged mutations by which field
changed, which is the loader's own model of the format restated. It
reported 12 failures, mostly wrong: a flip turning phase 0 into phase 1
produces a different but still valid module. Replacing the field list with
an oracle that re-derives well-formedness from the bytes turned 12
arguments into 1088 agreements and left the real defects standing out.

Same shape elsewhere: `scripts/test_wxlm.py` parses the `static_assert`s out
of `wxlm.hpp` rather than restating the layout; `tools/format_test` extracts
`FormatText` verbatim rather than copying it. If a test needs to know how
the code works to judge it, it's checking consistency, not correctness.

When the modeled thing changes, re-derive the oracle from the
specification, never reconcile it against the implementation. When declared
patches turned `declaredPatch*` from reserved into a real section, the
fuzzer's oracle disagreed with the loader on 32 flips, correctly, because
it still held the old rule. Re-deriving the section's bounds from the same
rule every other section obeys kept it an oracle; copying what the loader
now does would have turned it into a mirror that agrees with a wrong
implementation as readily as a right one.

A heuristic that looks equivalent to the property is not the property.
`scripts/audit_gates.py` searched whole files for a gate's name, so its own
comment in `build_cemu.bat` counted as an invocation. Its shell guard used a
line window to infer whether `set -e` was in force, and no window size got
both `loader_fuzz`'s `exit 1` and its `set +e` right. Fix: strip comments
and track actual `errexit` state from the top of the file, rather than
approximate it.

### 4. Every check must be able to fail

Where a check could pass because the thing it watches never ran, pair it
with a positive assertion that it did run.

When the module image moved onto `Arena::AllocIn`, `Loader::AllocFn` stayed
installed but was never called. `tools/loader_fuzz`'s red-zoned allocator
went through that hook, so its canaries became bytes nothing could reach,
and `CheckRedZones()` passed on every case without inspecting anything the
loader touched. The case count didn't move; all three gates stayed green.
Fix: the fuzzer now poisons the whole arena reservation and asserts both
that the loader wrote only inside its grant (containment) and that it did
write inside it (liveness). Containment alone passes trivially when
nothing writes at all.

Guards deserve the same suspicion. An `if os.path.exists(...)` around a
check means the check ceases to exist the day the file moves. Auditing the
gates found `test_wxlm.py` printing "CRC32 and FNV-1a verified" with
`surface.hpp` renamed away, and a pinned-offset comparison iterating an
empty parse to print "0 pinned offsets, exit 0."

The limit case is a gate nothing invokes. `tools/format_test` was written
so `WIIXL_LOG`'s formatter would have a test, and no build script
referenced it until the audit. It passed by hand and never ran as part of
a build. `scripts/audit_gates.py` exists for exactly two properties: every
gate is invoked, and every invocation's exit code is checked.

A check that died is not a check that passed. cmd's `if errorlevel N`
means "errorlevel is greater than or equal to N." An access violation
leaves `ERRORLEVEL` at `-1073741819`, which fails `if errorlevel 1`, so a
crash exits "successfully." Fix: test against zero,
`if %ERRORLEVEL% NEQ 0`, swept across 33 sites.

The buffered half doubles it: a crashing process with block-buffered
`stdout` discards its output, producing no output and no failure. Test
binaries here call `setvbuf(stdout, nullptr, _IONBF, 0)`.

### 5. Writing a rule down is half the job. Sweep for every existing instance in the same commit

The unbuffered-stdout rule was applied to one of three test binaries before
`hook_test` crashed with no output at all, the exact symptom the rule was
meant to prevent. The same sweep found 33 `if errorlevel 1` sites and four
gates with aggregate-only floors. After naming a failure class, grep for
it.

### 6. A floor on an aggregate does not constrain its composition

`tools/loader_fuzz` floors its case count. That floor held while the suite
broke: the loader's module table went uncleared between cases, so after
eight accepted loads every later valid module was refused for an unrelated
reason. The run still executed 1171 cases and reported PASS; the
accepted/rejected split moved from 293/878 to 8/1163. A gate reporting a
split must assert the split, not the sum. `hook_test`'s 53 checks and
`format_test`'s 23 now count and floor per section instead of as one total.

### 7. When a failure has a visible symptom and an invisible one, assert the invisible one

`wiixl.net`'s stale-handle case: the visible symptom is a send through a
closed handle returning an error; the invisible damage is the bytes
reaching the socket that inherited that descriptor, another mod's live
connection. `tools/net_test`'s fake transport records which descriptor each
write landed on, so the assertion is that nothing was written to the
recycled descriptor, paired with a positive control that the descriptor's
real owner can still write to it.

Same form elsewhere: `Patches::VerifyApplied` reads bytes back rather than
trusting `Apply`'s return value; `tools/hook_test` decodes emitted
instructions rather than checking `InstallHook`'s return; tick tests read
the in-flight owner from inside the callback. If a check would still pass
on an implementation that reports the error and does the harm, it's
watching the report.

### 8. A distinction that exists only in a log string is not a distinction the code makes

`Surface::Require` returned a bare `bool` for three different diagnoses (no
such surface, incompatible major, older minor), the difference living only
in a `WIIXL_LOG` line compiled out on the host test. No test could tell
them apart. `Surface::Check` now returns
`Compat::{Ok, NotPresent, MajorMismatch, MinorTooOld}`. Same reasoning
behind `Patches::Result`'s named refusals and `wiixl.core`'s scoped-read
`NO-MODULE`/`PARENT-ESCAPE`/`BAD-CHAR` family.

Test: could code tell two cases apart without reading text? If not, it
only describes them.

### 9. Gate policy

* Every gate prints what it checked, not just that it passed.
* No gate exits 0 on a missing input (absent `readelf`, `surface.hpp`,
  MSVC are hard failures).
* Each gate self-checks its own liveness (case-count floors, parse-count
  floors, per-section floors). `scripts/audit_gates.py` covers only that a
  gate is invoked and its exit code checked.
* Test binaries are unbuffered; every exit-code check tests against zero.

`test.bat` / `test.sh` run every gate in one place; see
[Setup](setup.md#verifying).
