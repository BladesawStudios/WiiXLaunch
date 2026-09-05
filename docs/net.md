# wiixl.net — sockets that belong to somebody

`wiixl.net` is the first surface where a mod holds a resource with a
**lifetime**. Hooks and patches are install-once and the host owns the result
forever. A socket is not like that: a mod that opens one and never closes it
holds a system handle for the whole session, and *"the game runs out of sockets
after an hour"* is a report with no owner attached — the exact failure class the
rest of this framework exists to eliminate.

So the host tracks them, and hands out **its own handles** rather than raw
descriptors.

## Why not raw descriptors

Not tidiness. A raw descriptor turns use-after-close into silent cross-mod
corruption:

1. mod A closes fd 5
2. mod B opens a socket and the OS hands it fd 5 — descriptors get reused, that
   is what they do
3. mod A, still holding a stale `5`, sends its response down **mod B's
   connection**

Nothing crashes. B's client receives A's data, intermittently, and the bug reads
like a host bug. A `Handle` carries a **generation** alongside the slot index,
the generation moves every time the slot is reused, and A's stale handle comes
back `STALE-HANDLE` instead of reaching B's socket.

```
bits  0..15   slot index + 1     (so 0 is never a valid handle)
bits 16..31   generation         (moves on every acquisition)
```

The generation is 16 bits, so a slot would have to be recycled 65536 times
before a stale handle could alias a live one. That is a **bound**, not an
impossibility.

## Why per-module quotas — this is fault isolation

Without per-module attribution the only cap that can exist is a global one. A
global cap means the mod that **leaks** exhausts the pool while the mods that get
**refused** are whichever ones happened to ask next: the failure lands on
innocent modules and names none of them.

With a per-module cap (`kMaxPerModule = 8`, out of `kMaxSockets = 24`) the leak
is contained to its owner. The leaking mod hits its own ceiling, is refused by
name, and every other mod keeps working. `tools/net_test` asserts exactly that —
that a second module is *unaffected* — because "the cap works" would be equally
true of a global cap that punishes everyone.

An **accepted** connection goes through the quota too. A server that accepts
every frame and forgets to close is the real leak vector, not `Open`. And when
an accept is refused, the host **closes** the descriptor the platform already
handed over rather than leaving it untracked — the one thing worse than a
tracked leak is an untracked one.

## What tracking does not buy

Tracking buys attribution and containment. It does **not** buy recovery, and
three cases have to be told apart:

| Case | What happens |
|---|---|
| A leak within a session | **Contained.** The quota stops it at 8 and `LogState` names who holds what. |
| A hang inside a tick | **Attribution only.** Nothing reclaims anything, because nothing runs — the game is frozen. What you get is Tick's in-flight record naming the module and this module's socket count beside it. |
| Shutdown | **The process owns the handles.** There is no module-unload path in this system, so a host-side "close everything on the way out" would be a function with no caller. |

That last row is deliberate. `CloseAllFor` exists and its only caller today is a
module closing its **own** sockets through the surface. When module unload or a
tick watchdog exists, that is what will call it — until then this code does not
pretend the host reclaims anything on its own.

## Two different ways there is no network

These are separate questions answered at separate times, and they must not
collapse into one value:

- **The surface is not registered** — this platform has no socket implementation
  at all. Switch. A mod that imports any `wiixl.net` symbol has `wiixl.net` as a
  required surface, so it is refused **at load, by name**, and never runs.
- **`UNAVAILABLE` from a call** — this platform has sockets and they could not be
  reached here. On Cemu, a title whose process has no network stack.

## The static-import rule

**Base may only statically import what the host can guarantee is present.
Anything else is resolved at runtime and degrades to an `UNAVAILABLE` value.**

This is a base-framework rule, not a note about sockets. It exists because the
failure it prevents is the worst shape available to this project, and `wiixl.net`
is simply where it was first noticed.

### Why it is a rule and not a preference

On Cemu, WiiXLaunch ships as a graphic pack, and **Cemu fails the entire pack
when any patch import cannot be resolved**. Not the feature that needed it — the
pack. So a single unresolvable import in base means:

- WiiXLaunch does not load at all
- not the surface that wanted it: *every* subsystem, *every* mod
- silently, because nothing ran, so nothing logged
- on titles that never used the feature in the first place

A capability most mods never touch takes down the whole framework on games it
was never used with, and the user's only evidence is that the pack did not
apply. There is no log line to read and no owner to name — the exact failure
class the rest of this framework is built to eliminate, arriving through the
build system instead of through code.

Runtime resolution converts that into: the host boots normally, everything else
works, one surface reports `UNAVAILABLE`, and the log says which library was
missing and why.

### Which imports the host can guarantee

`coreinit` — it is imported by every Wii U title that exists, so a
`import.coreinit.<Name>` shim will resolve wherever WiiXLaunch runs at all.
That is the whole guaranteed set. `src/cemu/cemu_fs.asm`, `cemu_mem.asm`,
`cemu_time.asm` and `cemu_logging.asm` are all coreinit and all legitimate.

**Everything else is a guess about a particular game.** nsysnet is the case in
hand: BotW v208's import table has 445 entries and not one is a socket call —
checked against the RPX, not assumed. A static `import.nsysnet.socket` in base
would have been a bet on every title WiiXLaunch is ever pointed at.

A *game module* (`vendor/wiixlaunch-*`) is a different matter and may import what
its game demonstrably imports: `gx2_imports.asm` lives in the BotW module
precisely because GX2 is a claim about that game, and `scripts/deploy.py` already
skips a module's shim table when the module is not compiled in.

### How runtime resolution works

coreinit's dynamic loader is itself a coreinit import, so it is always
reachable. `src/cemu/cemu_dynload.asm` shims three calls —
`OSDynLoad_Acquire`, `OSDynLoad_FindExport`, `OSDynLoad_Release` — and every
non-guaranteed library is looked up through them at first use:

```
Acquire the RPL by name  ->  null means "this process has no such library"
FindExport each symbol   ->  null means "the library is there, that entry is not"
```

Both failures are distinct and both are logged, because *"nsysnet is not
loadable"* and *"nsysnet is loaded and missing an export"* are different problems
with the same symptom. Resolution is attempted **once** and the outcome cached
including failure, so a host without the library does not retry an
`OSDynLoad_Acquire` every frame.

**All or nothing.** Every export must resolve or none are used: a partially
resolved table would open sockets it could not close.

### Applying it

When a surface needs a library base cannot guarantee:

1. Do **not** add an `import.<lib>.*` shim table to `src/cemu/`.
2. Resolve through `cemu/cemu_dynload.hpp` at first use, caching the outcome.
3. Give the surface an `Available()` that reports the result, and a distinct
   refusal value for "reachable platform, unreachable library".
4. Register the surface anyway where the platform *could* support it, so
   "unsupported platform" (surface absent, mod refused by name at load) stays
   distinct from "unavailable here" (surface present, call refused at runtime).

Wii U hardware is unaffected either way: the Aroma plugin is a real module with
its own import table, so ordinary linking applies and none of this is needed
there.

## Closing a connection without losing the reply

A server that accepts, sends and closes in one go looks correct and is not.
Two separate things go wrong, and the first working boot of the `d_net` sample
hit both at once — three requests served, `curl` reporting **connection reset by
peer** for every one of them.

1. **The client has usually not sent anything yet when `accept` returns.**
   Closing immediately meets the request with a closed socket.
2. **Closing a socket with unread bytes in its receive buffer sends an RST, not
   a FIN.** An RST tells the client to *discard whatever it has not read* —
   including the reply that was already on the wire.

So the minimum an HTTP server can do and still be one:

```
accept                    -> keep the connection, return
recv until CRLF CRLF     -> the bytes are not needed, but they must be TAKEN
send the reply            -> across as many ticks as it takes; a partial write is normal
Shutdown(handle, 1)       -> "I am done sending": FIN, not RST
Close(handle)
```

`Shutdown` is `wiixl.net` v1.1. Directions are 0 read, 1 write, 2 both.

A connection must also carry a **deadline**: a client that connects and says
nothing would otherwise hold a slot for the rest of the session, and that is a
leak with a well-behaved-looking cause.

## Testing it

`tools/net_test` cannot open a socket, and that is not what needs testing. It
drives the ownership layer against a fake transport that **recycles file
descriptors, lowest free first**, exactly like a real OS — because a
use-after-close is only dangerous once the number has been handed to somebody
else, and no real platform will do that on cue.

The fake records *which descriptor* bytes landed on, so the assertion is not "it
returned an error" but "**and nothing was written to the recycled descriptor**".
An implementation that returned an error and still wrote would pass the first and
fail the second.

Every refusal is paired with the same call succeeding. Without those positive
controls, an `Open` that returned `NO-MODULE` unconditionally would leave half
the assertions green.

The `d_net` sample closes a handle and then uses it **on purpose**, so a boot log
shows the refusal happening rather than a comment claiming it would.
