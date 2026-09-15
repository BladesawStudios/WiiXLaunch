# Modules

[« Back to overview](overview.md)

## What a module is

Base WiiXLaunch stays a generic, game-agnostic hooking framework: it has no idea
what game you are modding. A **game module** is game-specific knowledge -
offsets, vtable slots, actor spawn plumbing - promoted into a high-level API and
published as its own repository, so it does not bloat every clone of the
framework. You add the modules you actually need, per project.

The first one is [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw),
for Breath of the Wild.

A game module is reached two ways, and the distinction runs through the rest of
this page:

* **Host-built code** includes the module's headers directly
  (`#include <wiixlaunch/botw/botw.hpp>`) and calls its classes. Everything is
  available and nothing is versioned.
* **A `.wxlm`** never sees those headers. The module republishes its public API
  as versioned **surfaces** (`botw.player`, `botw.gfx`, ...) and a mod imports
  symbols from them by name. Both paths reach the same code; only the second
  survives the module changing underneath it. See
  [Writing a mod](writing-mods.md).

## Adding a module

Modules are plain git submodules, same as `vendor/exlaunch`, `vendor/wut`, etc.:

```bash
git submodule add https://github.com/TKVSC-Team/wiixlaunch-botw vendor/wiixlaunch-botw
git submodule update --init --recursive
```

Then name it in the target file's `modules` list (`targets/<game>.json`):

```json
"modules": ["wiixlaunch-botw"]
```

`[]` is a complete answer - a host with no game module publishes the base
surfaces only. A target with no `modules` key at all builds every
`vendor/wiixlaunch-*` it finds, and every build prints which modules it chose
and which it found but left out.

No build script edits are needed. Every build path scans `vendor/wiixlaunch-*`
and wires up the include path:

* `scripts/generate_config.py` adds it to the generated Switch `config.mk`.
* `build_cemu.bat`/`.sh` pass it straight through as `-I`.
* `build_wiiu.bat`/`.sh` stage it into the temp build dir under
  `modules/<name>/include`; `scripts/wiiu/Makefile`'s `INCLUDES` picks it up via
  `$(wildcard modules/*/include)`.
* `CMakeLists.txt` globs it too, for the direct-CMake Switch path.

The host reaches the module through one fixed path: a module provides
`<wiixlaunch/module.hpp>` declaring `WiiXLaunch::GameModule::Register()`, and
`src/main.cpp` includes it under `__has_include` and calls it. That is how a
tree with no module in `vendor/` still compiles, and why it is a plain call
rather than a weak symbol (see
[Cemu code cave relocation](cemu-relocation.md#weak-symbols-do-not-work-here)).

## Naming convention

A module must live at `vendor/wiixlaunch-<name>` for the auto-discovery to find
it - the `wiixlaunch-` prefix is required. This is deliberately narrow:
`vendor/wut`, `vendor/wups`, and `vendor/libfunctionpatcher` also have their
own top-level `include/` dirs, but they are Wii U SDK dependencies wired up
through their own dedicated Makefile flow, not something you want silently
added to every platform's include path.

## Available modules

* **[wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw)** - Breath
  of the Wild. Host-built classes for the player, actors, the pouch, armour,
  game time, weather, climate, map and region state, the controller, the
  camera, and `NVN`/`GX2` graphics injection (see
  [Graphics Injection](graphics-injection.md)); published to mods as eighteen
  `botw.*` surfaces (`botw.player`, `botw.actor`, `botw.gfx`, `botw.gui`,
  `botw.vfx`, `botw.flyt`, `botw.region`, `botw.camera`, `botw.display`,
  `botw.events`, `botw.sound`, `botw.memory`, `botw.gamedata`, `botw.world`,
  `botw.input`, `botw.map`, `botw.pouch`, `botw.armour`). See its README for
  per-platform coverage, and the generated headers under
  `sdk/include/wiixlaunch/imports/` for what each surface publishes.

## Writing your own module

A module is headers plus, on Cemu, any game-specific import shims. Conventions
worth following, taken from how `wiixlaunch-botw` is built:

* **Resolve WiiXLaunch's core via `<wiixlaunch/...>`, not `"../..."`.** A module
  does not live physically nested inside a project's `include/wiixlaunch/` tree
  once it is pulled in as a submodule. Only angle-bracket includes, resolved
  through the consuming project's own `-I include`, work regardless of where
  your module's files actually sit on disk.
* **Header-only**, matching the framework itself: no separate `.cpp` to build or
  link into every consumer.
* **Self-installing hooks.** If your module needs a hook, give it a static
  `Init()` the host calls once from `WiiXLaunch_Init()`; everything after that
  should be typed getters, not something the caller has to hook itself. See
  [Hooks](hooks.md) for the underlying `WIIXL_HOOK_DEFINE_TRAMPOLINE` mechanics.
* **Keep the module's own diagnostics on its own logger.** `WIIXL_LOG` is for
  host and mod code; `wiixlaunch-botw` uses `BotW::OSLog` for its internals so
  they do not compete for the 200-character Cemu ring. See
  [Debugging](debugging.md#botwoslog-is-a-different-thing).
* **OS-level shims belong in base, not in your module.** On Cemu the payload is
  a raw code-cave blob with no import table, so every OS call has to go through
  an `import.<lib>.<Name>` tail-call shim in a `src/cemu/*.asm` table. Put a shim
  in your module only if the thing it reaches is *game*-specific. Anything from
  `coreinit` - filesystem, logging, time, memory - is present in every Wii U
  title, needs no game address, and lives in base WiiXLaunch (`src/cemu/cemu_fs.asm`,
  `cemu_logging.asm`, `cemu_mem.asm`, `cemu_time.asm`, resolved through
  `<wiixlaunch/cemu/*.hpp>`); base's own logger and module loader depend on
  those, so they cannot be behind "is a game module installed". `wiixlaunch-botw`
  keeps only `gx2_imports.asm`, because assuming a graphics API is exactly the
  kind of thing a game-agnostic framework must not do. The full rule, and what
  to do for a library the host cannot guarantee, is in
  [wiixl.net](net.md#the-static-import-rule).
* **Nominate, don't assume.** Base has no idea where the game's entry is or what
  a frame is, so a module *nominates* those facts: the Cemu load point with
  `WIIXL_DECLARE_LOAD_POINT`, the frame source by calling `Tick::RunAll()` from
  its swap hook. See [the module loader](loader.md).
* **Capability flags, not runtime checks.** Every WiiXLaunch build targets
  exactly one platform at compile time. If a feature only works on some
  platforms, expose it as a `constexpr bool SupportsX`, not a function that
  takes a platform argument.

### A game module is HOST code; a mod is scoped

This distinction runs through several subsystems and blurs easily, because both
are "things that were not in base WiiXLaunch". A game module
(`vendor/wiixlaunch-*`) is compiled INTO the payload: its memory is the host's
(`Arena::AllocHost`), its hooks are attributed to `host`, and it reads game
content the way the host does. A mod (`.wxlm`) is loaded at runtime: it gets a
bounded arena grant, its hooks and patches are attributed to its own id, and its
resources live in `content/WiiXLaunch/mods/<id>/` reachable only through the
scoped read. **`wiixlaunch-botw` therefore never uses the mod-scoped call** - it
has no id and no directory, because it is not a module in that sense at all. If
you are writing a game module and reaching for `ModReadFile`, that is the signal
you have the wrong one.

---

## Standing rules

Everything below was found by being bitten, and each rule is stated with the
failure that produced it, because the failure is the argument. They apply to
base, to game modules and to the test gates alike.

### 1. Anything referenced only from outside the compiler's view needs `__attribute__((used))`

Data or code. GCC emits an `inline` definition only when some translation unit
odr-uses it, and a reference the compiler cannot see does not count. This has
bitten from both directions: a *data* global written by `scripts/deploy.py` and
read by a `src/cemu/*.asm` shim table (`g_CemuMemShimTableOffset`, which
shipped unreachable), and an *inline function* whose only caller was a
hand-written `asm()` block (`WiiXLaunch_LoadPointProbe`, which failed to link).
The data case fails silently at runtime; the code case fails loudly at link.
Mark both:

```cpp
__attribute__((section(".data"), used)) inline uint32_t g_Whatever = 0;
extern "C" __attribute__((used)) inline void Whatever() {}
```

`used` cannot rescue a header nobody included. If the symbol has to exist in
every build, some translation unit has to include the header, which is why
`src/cemu/bootstrap.cpp` pulls in the umbrella. `deploy.py` refuses to build a
pack whose base `WIIXL_OFFSET_SYMBOL` did not resolve.

### 2. A self-check the optimizer can answer is not testing anything

The sibling of the `used` rule and a different failure: `used` is about a symbol
not existing, this is about the compiler *deriving your test's result
statically* and deleting the mechanism you meant to exercise. Both were found in
one sitting while writing `examples/sample_mod/mod.cpp`.

A `static uint32_t probe[16];` that nothing writes is provably all zero, so "did
the loader zero my .bss?" folded to `true` and the array was dropped from `.bss`
entirely - the check would have passed with no loader present at all. A
`static Fn p = &undefined_import;` is provably that address, so the indirect
call through it folded into a direct branch, emitting a relocation kind that
cannot reach a host address. In both cases the code compiled, the test reported
success, and nothing under test had run.

The fix is `volatile` on anything another agent writes at runtime - the loader,
the relocator, a hook, the host. That is not a workaround but a statement of
what is true: this value changes behind the compiler's back. **Whenever you
write a test that asks "did mechanism X really happen?", ask first whether the
compiler can work out the answer without X happening. If it can, the test is
decoration.**

### 3. A test that encodes the implementation's assumptions can only confirm them

Where a property can be independently re-derived, derive it independently.
Otherwise the test and the code fail together, and agree while doing it.

The `.wxlm` fuzzer learned this the hard way. Its first header sweep judged each
mutation by *which field had changed*, classifying fields as load-bearing or
descriptive. That is the loader's own model of the format, restated, so the
sweep could only ever confirm what the loader already believed - and it reported
12 failures that were mostly the sweep being wrong, because a flip turning phase
0 into phase 1, or moving `entryOffset` elsewhere inside the payload, produces a
*different but still valid* module. The property is not which field changed; it
is whether the result is well-formed. Replacing the field list with an oracle
that re-derives well-formedness from the bytes, with no reference to the
loader's code, turned 12 arguments into 1088 agreements and left the real
defects standing out.

The same shape appears elsewhere: `scripts/test_wxlm.py` parses the
`static_assert`s out of `wxlm.hpp` rather than restating the layout, so it
cannot drift from the header independently; `tools/format_test` extracts
`FormatText` verbatim rather than copying it, because a copy would test the
copy. **If your test needs to know how the code works in order to judge it, it
is checking consistency, not correctness.**

**When the thing being modelled changes, the oracle is re-derived from the
specification, never reconciled against the implementation.** When declared
patches turned `declaredPatch*` from a reserved field into a real section, the
fuzzer's oracle immediately disagreed with the loader on 32 header flips -
correctly, because the oracle still held the old rule. There were two ways to
make them agree: copy what the loader now does, or re-derive the section's
bounds from the same rule every other section in the format obeys. Only the
second is still an oracle; the first turns it into a mirror, and a mirror agrees
with a wrong implementation exactly as readily as with a right one. **A
disagreement after a deliberate change is the oracle working, not the oracle
being stale.**

**A heuristic that looks equivalent to the property is not the property.**
`scripts/audit_gates.py` had two bugs on its first two runs, and both were this.
It searched whole files for a gate's name, so its own explanatory comment in
`build_cemu.bat` counted as an invocation - prose reads exactly like code to a
substring search. Then its shell guard used a window of lines around the
invocation to decide whether `set -e` was in force: too small and it could not
see `loader_fuzz`'s `exit 1` ten lines past a banner, too large and it saw
`loader_fuzz`'s `set +e` from six lines above `test_wxlm`. Two window sizes, two
false answers in opposite directions. The fix in both cases was to stop
approximating: strip comments and track the actual `errexit` state by scanning
from the top of the file. A window approximates the property; the state IS the
property - the same distinction as "which field changed" versus "is the result
well-formed".

### 4. Every check must be able to fail

Where a check could pass because the thing it watches never ran, pair it with a
positive assertion that the thing DID run. A test that cannot distinguish
"correct" from "never happened" is not a test.

This is the one nothing else could have caught. The `used` rule, the optimizer
rule and the oracle rule were all found *by* something - a link error, a build,
the fuzzer disagreeing with itself. This one was found by reading code that had
no reason to be suspected, because the failure mode is that everything stays
green. When the module image moved onto `Arena::AllocIn`, `Loader::AllocFn` was
left installed but never called; `tools/loader_fuzz` was installing a red-zoned
allocator through that hook, so its canaries became bytes nothing could reach
and `CheckRedZones()` passed on every case without inspecting anything the
loader had touched. **The case count did not move.** All three gates stayed
green. A refactor in one file silently disarmed a check in another, and a
disarmed check is indistinguishable from a passing one.

The fix is the shape to copy: the fuzzer now poisons the whole arena reservation
and asserts two things that are worthless apart - the loader wrote ONLY inside
its grant (containment), and it DID write inside it (liveness). Containment
alone passes trivially when nothing writes at all, which is exactly the state a
dead hook leaves behind. **Ask of every check: what would it do if the mechanism
it watches were deleted? If the answer is "pass", it is not watching anything.**

Guards deserve the same suspicion. An `if os.path.exists(...)` around a check
means the check silently ceases to exist the day the file moves, and a loop over
a dict that parsing left empty runs zero assertions and reports success.
Auditing the gates against this rule found one of each: `test_wxlm.py` printed
"CRC32 and FNV-1a verified" with `surface.hpp` renamed away - not a skipped
check but a gate asserting confidence it had not earned - and its pinned-offset
comparison iterated an empty parse to print "0 pinned offsets, exit 0",
triggerable by an ordinary reformat, because the regexes did not span lines.

**The limit case is a gate nothing invokes**, and it was real: `tools/format_test`
was written (commit `73596ed`) specifically so `WIIXL_LOG`'s formatter would
have a test that runs, and no build script referenced it until the audit - it
passed whenever anyone ran it by hand and had never once executed as part of a
build. That is the one thing a gate cannot detect about itself, and it is why
`scripts/audit_gates.py` exists and why its scope is exactly two properties:
every gate is invoked, and every invocation's exit code is checked. Everything a
gate *can* assert about itself, it must - a watcher is exactly as disarmable as
the watched, so the watcher is kept as small as the job allows.

**A check that died is not a check that passed.** cmd's `if errorlevel N` means
*"errorlevel is greater than or equal to N"*. An access violation leaves
`ERRORLEVEL` at `-1073741819`, which is not greater than or equal to 1 - so
`if errorlevel 1` is FALSE for a crash, and every gate's failure branch was
skipped. A tool that segfaulted on its first case exited "successfully" and the
build went green. Every Windows gate here had that hole from the day it was
written: the pass path and the fail path were both covered, and the third way a
process can end never was. The fix is to test against zero rather than interpret
the number - `if %ERRORLEVEL% NEQ 0` - and 33 sites were swept for it. (`if
errorlevel 2` / `else if errorlevel 1` is only safe because the tool scripts now
normalise every result to 0, 1 or 2 before returning it.)

**The buffered half doubles it.** A crashing process with block-buffered
`stdout` discards everything it printed, so the run produced no output *and* no
failure - byte-for-byte indistinguishable from a gate that was never wired in at
all. Test binaries here set `setvbuf(stdout, nullptr, _IONBF, 0)` so a crash
still shows how far it got. **Whenever you check an exit status, ask what the
value is when the process did not exit normally; whenever a test can crash, make
sure its output survives the crash.**

### 5. Writing a rule down is half the job - sweep for every existing instance in the same commit

The unbuffered-stdout rule above was written as a general lesson and then
applied to exactly one of the three test binaries it described. The gap surfaced
the next time something crashed: `hook_test` died and produced no output at all,
which is precisely the symptom the rule had just been written to prevent. A rule
that is documented but only partially applied is worse than one that is neither,
because the documentation makes the remaining instances look already handled.
The same sweep found 33 `if errorlevel 1` sites and four gates with
aggregate-only floors. **After you name a failure class, grep for it.**

### 6. A floor on an aggregate does not constrain its composition

`tools/loader_fuzz` floors its case count, and that floor held perfectly while
the suite broke: a change left the loader's module table uncleared between
cases, so after eight accepted loads every later valid module was refused for a
reason that had nothing to do with it. The run still executed 1171 cases, still
reported PASS - and the accepted/rejected split moved from 293/878 to 8/1163.
The total never moved, because nothing had stopped running; the *answers* had
changed.

**Any gate that reports a split must assert the split, not the sum.** Sweeping
for that shape found more of it: `hook_test`'s 53 checks and `format_test`'s 23
were single totals over sections that could individually empty, so each section
now counts and floors itself; `audit_gates.py` reported three numbers and
floored none of its own, which for a file whose entire job is other gates'
liveness was the first thing to fix.

### 7. When a failure has a visible symptom and an invisible one, assert the invisible one

An implementation can produce the symptom without preventing the damage, so a
test that watches only the symptom passes on exactly the code that hurts.

`wiixl.net`'s stale-handle case is the clean example. The visible symptom is
that a send through a closed handle returns an error; the invisible damage is
that the bytes reach the socket which INHERITED that descriptor - another mod's
live connection. `Send` returning `STALE-HANDLE` and writing anyway would satisfy
the obvious assertion completely, and the failure it leaves behind is one mod's
data arriving on another mod's connection, intermittently, looking like a host
bug. So `tools/net_test`'s fake transport records WHICH descriptor each write
landed on, and the assertion is *nothing was written to the recycled descriptor*
- paired with a positive control that the descriptor's real owner CAN write to
it, so "nothing was written" cannot pass because the fake is dead.

The general form: name the damage, not the complaint. `Patches::VerifyApplied`
reads the bytes back rather than trusting `Apply`'s return value; `tools/hook_test`
decodes the emitted instructions rather than checking that `InstallHook` said
Ok; the tick tests read the in-flight owner from INSIDE the callback, because
after the call is not the moment a freeze can be observed. **If the check would
still pass on an implementation that reports the error and does the harm, it is
watching the report.**

### 8. A distinction that exists only in a log string is not a distinction the code makes

If two outcomes should be distinguishable, they must differ in a *value*
something can assert - not in the message printed about them.

`Surface::Require` returned a bare `bool` for three genuinely different
diagnoses: a surface nobody registered, a major the host cannot satisfy in either
direction, and a minor the host is simply older than. The difference lived
entirely in a `WIIXL_LOG` line, which is compiled out on the host test - so the
version check could have been giving the right answer for the wrong reason and
nothing could tell, and no test could have been written that would notice.
`Surface::Check` returns `Compat::{Ok, NotPresent, MajorMismatch, MinorTooOld}`
now, and `Require` keeps its `bool` and does the logging. The same reasoning
gives `Patches::Result` its named refusals rather than one `false`, and
`wiixl.core`'s scoped read its `NO-MODULE` / `PARENT-ESCAPE` / `BAD-CHAR`
family.

**The test is simple: could a test tell these two cases apart without reading
text? If not, the code does not actually distinguish them - it only describes
them.** Log strings are for humans; values are for assertions, and a diagnosis
nothing can assert will drift out of agreement with the code that produces it.

### 9. Gate policy

Practically, the rules above come to this:

* **Every gate prints what it checked, not just that it passed** - `23 checks`,
  `8 pinned offsets`, `293 liveness checks`, `4/4 probes ran` - because a count
  that can visibly drop to zero is a liveness assertion costing one line.
* **No gate exits 0 on a missing input.** Absent `readelf`, absent
  `surface.hpp`, absent MSVC are all hard failures, because "skipped" is a state
  that has to be seen and a warning banner scrolls past.
* **Each gate self-checks its own liveness** - case-count floors, parse-count
  floors, per-section floors - and `scripts/audit_gates.py` covers only the two
  things a gate cannot know about itself: that something invokes it, and that
  its exit code is checked.
* **Test binaries are unbuffered**, and every exit-code check tests against
  zero.

`test.bat` / `test.sh` run every gate in one place; see
[Setting Up](setup.md#verifying).
